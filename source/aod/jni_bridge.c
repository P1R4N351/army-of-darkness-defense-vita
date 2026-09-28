/*
 * aod-vita: Java side of Army of Darkness Defense 1.1.1, reimplemented for FalsoJNI.
 *
 * FalsoJNI resolves methods and fields by bare name and drops the receiver object, but this
 * game has same-named methods on different classes (SHA1/SHA256/MD5.update, show, getMessage)
 * and same-named static ints with different values (UIResponse vs InformationResponse
 * REQUEST_IN_FLIGHT). So after jni_init() this file replaces the method/field/object entries
 * of FalsoJNI's JNIEnv table with a dispatcher keyed by (class, name, signature).
 * Strings and arrays stay with FalsoJNI.
 *
 * Every signature below is transcribed from the decompiled Java of APK sha256 4800eac5...6bf4
 * (inspection/java-method-signatures.tsv). Where the Java would talk to Google Play, a network,
 * or an Android Activity, the implementation takes the Java *failure* path that exists in the
 * original code (same domain/message/code), so native never waits on a reply that won't come.
 * Every lookup is logged. An unknown method returns a NULL id and is logged as "JNI MISSING",
 * and that is the first thing to grep for when diagnosing a boot failure.
 *
 * This file is part of aod-vita and is distributed under the MIT license.
 */

#include "aod/jni_bridge.h"
#include "aod/hash.h"
#include "aod/port.h"
#include "aod/text_render.h"

#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_ImplBridge.h>
#include <sha1/sha1.h>

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BF "com/backflipstudios/"
#define S_STR "Ljava/lang/String;"
#define S_ERR "Lcom/backflipstudios/bf_core/error/PlatformError;"

/* ------------------------------------------------------------------ objects */

#define AOD_OBJ_MAGIC 0xA0DD0B1Eu

typedef enum {
	K_SENTINEL, K_AD, K_ERROR, K_CLASS, K_SHA1, K_SHA256, K_MD5, K_TR_PARAMS, K_TR_RESULT
} obj_kind;

typedef struct aod_obj {
	uint32_t magic;
	const char *cls;
	obj_kind kind;
	union {
		struct { char *domain, *message; int code; } err;
		struct { const char *name; } klass;
		SHA1_CTX sha1;
		aod_sha256_ctx sha256;
		aod_md5_ctx md5;
		aod_text_params tp;
		aod_text_prepare_result tr;
		struct { jlong handle; } ad;
	} u;
} aod_obj;

/* Allocation accounting (REVIEW-CODE F5): objects are never freed (see below), so every kind
 * created per call is counted and aod_jni_log_stats() reports growth; a kind that climbs every
 * frame on hardware is a hot path to fix, not a bounded leak. */
static unsigned n_obj_by_kind[16], n_strings;
static aod_obj *obj_new(const char *cls, obj_kind kind) {
	if ((unsigned)kind < 16) n_obj_by_kind[kind]++;
	aod_obj *o = calloc(1, sizeof(*o));
	if (!o) {
		aod_log("JNI: out of memory allocating %s", cls);
		return NULL;
	}
	o->magic = AOD_OBJ_MAGIC;
	o->cls = cls;
	o->kind = kind;
	return o;
}

/* Objects handed out here are never freed: native may hold several global refs to one
 * object, so freeing on the first DeleteGlobalRef would be a use-after-free. The leak is
 * bounded (a few hundred small objects per session). */
static int obj_is_ours(const void *p) {
	/* Every jobject that reaches us is either NULL, a FalsoJNI allocation, a strdup'd class
	 * name, or one of ours. All are readable for 4 bytes at the pointer. */
	return p && ((uintptr_t)p & 3) == 0 && ((const aod_obj *)p)->magic == AOD_OBJ_MAGIC;
}

static aod_obj *as_obj(jobject p, obj_kind kind) {
	if (!obj_is_ours(p) || ((aod_obj *)p)->kind != kind) return NULL;
	return (aod_obj *)p;
}

/* Interned class-name strings (REVIEW-CODE F5): FindClass/GetObjectClass return one stable
 * pointer per class so repeated calls do not allocate, and DeleteGlobalRef never frees them. */
typedef struct interned { char *name; struct interned *next; } interned;
static interned *interned_head;
static pthread_mutex_t intern_lock = PTHREAD_MUTEX_INITIALIZER;
static const char *intern_class(const char *name) {
	pthread_mutex_lock(&intern_lock);
	interned *i = interned_head;
	while (i && strcmp(i->name, name) != 0) i = i->next;
	if (!i && (i = calloc(1, sizeof(*i))) != NULL) {
		i->name = strdup(name);
		i->next = interned_head;
		interned_head = i;
	}
	pthread_mutex_unlock(&intern_lock);
	return i ? i->name : NULL;
}
static int is_interned(const void *p) {
	pthread_mutex_lock(&intern_lock);
	interned *i = interned_head;
	while (i && (const void *)i->name != p) i = i->next;
	pthread_mutex_unlock(&intern_lock);
	return i != NULL;
}

static jstring jstr(const char *s) { n_strings++; return (*(&jni))->NewStringUTF(&jni, s ? s : ""); }

static char *cstr_dup(jstring s) {
	if (!s) return strdup("");
	const char *c = (*(&jni))->GetStringUTFChars(&jni, s, NULL);
	char *r = strdup(c ? c : "");
	if (c) (*(&jni))->ReleaseStringUTFChars(&jni, s, c);
	return r;
}

static jobject platform_error(const char *domain, const char *message, int code) {
	aod_obj *o = obj_new(BF "bf_core/error/PlatformError", K_ERROR);
	if (!o) return NULL;
	o->u.err.domain = strdup(domain);
	o->u.err.message = strdup(message);
	o->u.err.code = code;
	return (jobject)o;
}

static jobject sentinel(const char *cls) { return (jobject)obj_new(cls, K_SENTINEL); }

static jobjectArray empty_array(void) { return (*(&jni))->NewObjectArray(&jni, 0, NULL, NULL); }

static jbyteArray byte_array(const void *data, int len) {
	jbyteArray a = (*(&jni))->NewByteArray(&jni, len);
	if (a && len) (*(&jni))->SetByteArrayRegion(&jni, a, 0, len, (const jbyte *)data);
	return a;
}

/* ------------------------------------------------ deferred native callbacks */
/* Android delivers these from the UI thread / a worker thread after the Java call returned.
 * Replying synchronously would re-enter native while it may hold the request's lock, so they
 * are queued and drained by the main loop after nativeUpdate(), like Handler.post. */

typedef void (*deferred_fn)(void *);
typedef struct deferred { deferred_fn fn; void *arg; struct deferred *next; } deferred;
static deferred *q_head, *q_tail;
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;

static void defer(deferred_fn fn, void *arg) {
	if (!arg) {   /* REVIEW-CODE F8: reply allocation failed */
		aod_log("JNI: out of memory queueing a reply; it is dropped");
		return;
	}
	deferred *d = malloc(sizeof(*d));
	if (!d) { free(arg); aod_log("JNI: out of memory queueing a reply; it is dropped"); return; }
	d->fn = fn; d->arg = arg; d->next = NULL;
	pthread_mutex_lock(&q_lock);
	if (q_tail) q_tail->next = d; else q_head = d;
	q_tail = d;
	pthread_mutex_unlock(&q_lock);
}

/* Java calls some native callbacks inline, before the Java method returns (e.g. IAP.init when no
 * billing service exists, IAP.getPurchases, the "main activity is null" UI paths). Native code
 * is written against that ordering, so those replies are delivered synchronously here too.
 * Hardware boot-01 stopped inside a *deferred* billing reply delivered out of that order. */
static void reply_now(deferred_fn fn, void *arg) {
	if (!arg) {
		aod_log("JNI: out of memory building an inline reply; it is dropped");
		return;
	}
	fn(arg);
}

void aod_jni_run_deferred(void) {
	pthread_mutex_lock(&q_lock);
	deferred *d = q_head;
	q_head = q_tail = NULL;
	pthread_mutex_unlock(&q_lock);
	while (d) {
		deferred *n = d->next;
		aod_log("JNI: deferred reply begin");
		d->fn(d->arg);
		aod_log("JNI: deferred reply end");
		free(d);
		d = n;
	}
}

#define NATIVE(name) aod_game_symbol("Java_com_backflipstudios_" name)

typedef struct { const char *sym; jobject err; jlong handle; jint i; jboolean z; } reply;

static reply *mk_reply(const char *sym, jobject err, jlong handle) {
	reply *r = calloc(1, sizeof(*r));
	if (r) { r->sym = sym; r->err = err; r->handle = handle; }
	return r;
}

static void *reply_fn(const reply *r) {
	void *f = aod_game_symbol(r->sym);
	if (!f) aod_log("JNI: reply target %s not exported by libgame", r->sym);
	else aod_log("JNI: reply %s(handle=%lld)", r->sym, (long long)r->handle);
	return f;
}

/* (PlatformError, long) */
static void r_err_handle(void *a) {
	reply *r = a;
	void (*f)(JNIEnv *, jclass, jobject, jlong) = reply_fn(r);
	if (f) f(&jni, NULL, r->err, r->handle);
	free(r);
}
/* (PlatformError, Object[], long) */
static void r_err_array_handle(void *a) {
	reply *r = a;
	void (*f)(JNIEnv *, jclass, jobject, jobjectArray, jlong) = reply_fn(r);
	if (f) f(&jni, NULL, r->err, r->z ? empty_array() : NULL, r->handle);
	free(r);
}
/* (int, PlatformError, long) */
static void r_int_err_handle(void *a) {
	reply *r = a;
	void (*f)(JNIEnv *, jclass, jint, jobject, jlong) = reply_fn(r);
	if (f) f(&jni, NULL, r->i, r->err, r->handle);
	free(r);
}
/* (boolean, PlatformError, long) */
static void r_bool_err_handle(void *a) {
	reply *r = a;
	void (*f)(JNIEnv *, jclass, jboolean, jobject, jlong) = reply_fn(r);
	if (f) f(&jni, NULL, r->z, r->err, r->handle);
	free(r);
}
/* (String, boolean, PlatformError, long) */
static void r_str_bool_err_handle(void *a) {
	reply *r = a;
	void (*f)(JNIEnv *, jclass, jstring, jboolean, jobject, jlong) = reply_fn(r);
	if (f) f(&jni, NULL, jstr(""), r->z, r->err, r->handle);
	free(r);
}
/* (long, PlatformError) */
static void r_handle_err(void *a) {
	reply *r = a;
	void (*f)(JNIEnv *, jclass, jlong, jobject) = reply_fn(r);
	if (f) f(&jni, NULL, r->handle, r->err);
	free(r);
}
/* (long, boolean, PlatformError) */
static void r_handle_bool_err(void *a) {
	reply *r = a;
	void (*f)(JNIEnv *, jclass, jlong, jboolean, jobject) = reply_fn(r);
	if (f) f(&jni, NULL, r->handle, r->z, r->err);
	free(r);
}
/* (long, float, String, PlatformError) */
static void r_handle_float_str_err(void *a) {
	reply *r = a;
	void (*f)(JNIEnv *, jclass, jlong, jfloat, jstring, jobject) = reply_fn(r);
	if (f) f(&jni, NULL, r->handle, 0.0f, NULL, r->err);
	free(r);
}
/* (String, long, PlatformError) */
static void r_str_handle_err(void *a) {
	reply *r = a;
	void (*f)(JNIEnv *, jclass, jstring, jlong, jobject) = reply_fn(r);
	if (f) f(&jni, NULL, jstr(""), r->handle, r->err);
	free(r);
}
/* instance native InterstitialAd.nativeInterstitialSingal(long, int, PlatformError) */
typedef struct { jobject thiz; jlong handle; } ad_reply;
static void r_interstitial(void *a) {
	ad_reply *r = a;
	void (*f)(JNIEnv *, jobject, jlong, jint, jobject) =
		NATIVE("bf_1ads_google_1mobile_1ads_InterstitialAd_nativeInterstitialSingal");
	aod_log("JNI: reply InterstitialAd.nativeInterstitialSingal(ERROR)");
	/* INTERSITIAL_SIGNAL_ERROR = 4; InterstitialAd.createPlatformError(1) = "ads" domain. */
	if (f) f(&jni, r->thiz, r->handle, 4, platform_error("ads", "Interstitial not valid", 1));
	free(r);
}

/* ---------------------------------------------------------- method registry */

typedef jvalue (*impl_fn)(jobject thiz, const jvalue *a);

typedef struct {
	const char *cls;   /* full class name, "/" separated */
	const char *name;
	const char *sig;
	impl_fn fn;
} method;

#define RET(field, v) do { jvalue _r; memset(&_r, 0, sizeof(_r)); _r.field = (v); return _r; } while (0)
#define RETV() do { jvalue _r; memset(&_r, 0, sizeof(_r)); return _r; } while (0)

static int g_surface_w = 960, g_surface_h = 544;
void aod_jni_set_surface(int w, int h) { g_surface_w = w; g_surface_h = h; }

/* --- ApplicationContext */
static jvalue ac_getMainAssetManager(jobject t, const jvalue *a) {
	static jobject am;
	if (!am) am = sentinel("android/content/res/AssetManager");
	RET(l, am);
}
static jvalue ac_get(jobject t, const jvalue *a) {
	char *k = cstr_dup(a[0].l);
	aod_log("JNI: ApplicationContext.get(\"%s\") -> null (no Android runtime objects)", k);
	free(k);
	RET(l, NULL);
}
static jvalue ac_set(jobject t, const jvalue *a) { RETV(); }
static jvalue ac_exit(jobject t, const jvalue *a) {
	aod_log("JNI: ApplicationContext.exit()");
	aod_request_exit();
	RETV();
}
static jvalue prefs_getInt(jobject t, const jvalue *a) {
	char *k = cstr_dup(a[0].l);
	int v = aod_prefs_get_int(k, a[1].i);
	free(k);
	RET(i, v);
}

/* --- PlatformError */
static jvalue pe_init(jobject t, const jvalue *a) {
	aod_obj *o = obj_new(BF "bf_core/error/PlatformError", K_ERROR);
	if (o) {
		o->u.err.domain = cstr_dup(a[0].l);
		o->u.err.message = cstr_dup(a[1].l);
		o->u.err.code = a[2].i;
	}
	RET(l, (jobject)o);
}
static jvalue pe_getDomain(jobject t, const jvalue *a) {
	aod_obj *o = as_obj(t, K_ERROR);
	RET(l, jstr(o ? o->u.err.domain : ""));
}
static jvalue pe_getMessage(jobject t, const jvalue *a) {
	aod_obj *o = as_obj(t, K_ERROR);
	RET(l, jstr(o ? o->u.err.message : ""));
}
static jvalue pe_getCode(jobject t, const jvalue *a) {
	aod_obj *o = as_obj(t, K_ERROR);
	RET(i, o ? o->u.err.code : 0);
}
static jvalue pe_toString(jobject t, const jvalue *a) {
	aod_obj *o = as_obj(t, K_ERROR);
	char buf[512];
	/* PlatformError.toString(): domain + "(code:" + code + ") - " + message */
	snprintf(buf, sizeof(buf), "%s(code:%d) - %s", o ? o->u.err.domain : "", o ? o->u.err.code : 0,
	         o ? o->u.err.message : "");
	RET(l, jstr(buf));
}

/* --- java.lang.Object / Class, used by CallContext for class-name checks */
static jvalue obj_getClass(jobject t, const jvalue *a) {
	/* one Class object per class name, created on first use */
	typedef struct cobj { aod_obj *o; struct cobj *next; } cobj;
	static cobj *cache;
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	const char *name = intern_class(obj_is_ours(t) ? ((aod_obj *)t)->cls : "java/lang/Object");
	pthread_mutex_lock(&lock);
	cobj *c = cache;
	while (c && c->o->u.klass.name != name) c = c->next;
	if (!c && (c = calloc(1, sizeof(*c))) != NULL) {
		c->o = obj_new("java/lang/Class", K_CLASS);
		if (c->o) { c->o->u.klass.name = name; c->next = cache; cache = c; }
		else { free(c); c = NULL; }
	}
	pthread_mutex_unlock(&lock);
	RET(l, c ? (jobject)c->o : NULL);
}
static jvalue class_getName(jobject t, const jvalue *a) {
	aod_obj *c = as_obj(t, K_CLASS);
	char buf[256];
	snprintf(buf, sizeof(buf), "%s", c ? c->u.klass.name : "java.lang.Object");
	for (char *p = buf; *p; p++) if (*p == '/') *p = '.';   /* Class.getName() is dotted */
	RET(l, jstr(buf));
}
static jvalue obj_toString(jobject t, const jvalue *a) {
	if (as_obj(t, K_ERROR)) return pe_toString(t, a);
	RET(l, jstr(obj_is_ours(t) ? ((aod_obj *)t)->cls : "java.lang.Object"));
}

/* --- Information (all static except <init> and getAdvertisingInfo) */
static jvalue s_const(const char *s) { RET(l, jstr(s)); }
static jvalue inf_getApplicationVersion(jobject t, const jvalue *a) { return s_const(AOD_VERSION_CODE); }
static jvalue inf_getApplicationVersionShort(jobject t, const jvalue *a) { return s_const(AOD_VERSION_NAME); }
static jvalue inf_getApplicationName(jobject t, const jvalue *a) { return s_const(AOD_PACKAGE); }
static jvalue inf_getRootWritePath(jobject t, const jvalue *a) { return s_const(aod_write_path()); }
static jvalue inf_empty(jobject t, const jvalue *a) { return s_const(""); }
static jvalue inf_getDeviceModel(jobject t, const jvalue *a) { return s_const(aod_device_model()); }
static jvalue inf_getSystemVersion(jobject t, const jvalue *a) { return s_const("4.4.2"); }
static jvalue inf_getDeviceUniqueIdentifier(jobject t, const jvalue *a) { return s_const(aod_device_id()); }
static jvalue inf_getScreenLayout(jobject t, const jvalue *a) { return s_const("normal"); }
static jvalue inf_getDensityDPI(jobject t, const jvalue *a) { return s_const("high"); }
static jvalue inf_getNetworkType(jobject t, const jvalue *a) { return s_const("none"); }
static jvalue inf_getArchitecture(jobject t, const jvalue *a) { return s_const("armeabi-v7a"); }
static jvalue inf_getLanguage(jobject t, const jvalue *a) { return s_const(aod_language()); }
static jvalue inf_getCountryCode(jobject t, const jvalue *a) { return s_const(aod_country()); }
static jvalue inf_getTimeZone(jobject t, const jvalue *a) { return s_const("UTC"); }
static jvalue inf_getOpenGLContextVersion(jobject t, const jvalue *a) { return s_const("2.0"); }
static jvalue inf_getCPUName(jobject t, const jvalue *a) { return s_const("ARMv7 Cortex-A9"); }
static jvalue z_false(jobject t, const jvalue *a) { RET(z, JNI_FALSE); }
static jvalue z_true(jobject t, const jvalue *a) { RET(z, JNI_TRUE); }
static jvalue v_noop(jobject t, const jvalue *a) { RETV(); }
static jvalue l_null(jobject t, const jvalue *a) { RET(l, NULL); }
static jvalue inf_diskSpace(jobject t, const jvalue *a) { RET(j, aod_free_space_bytes()); }
static jvalue j_zero(jobject t, const jvalue *a) { RET(j, 0); }
static jvalue inf_getMemorySizeinMB(jobject t, const jvalue *a) { RET(i, 512); }
static jvalue inf_getDisplayDPI(jobject t, const jvalue *a) { RET(f, 220.0f); }
static jvalue inf_getDisplayScale(jobject t, const jvalue *a) { RET(f, 1.0f); }
static jvalue inf_getDisplayWidth(jobject t, const jvalue *a) { RET(i, g_surface_w); }
static jvalue inf_getDisplayHeight(jobject t, const jvalue *a) { RET(i, g_surface_h); }
static jvalue inf_getCPUCoreCount(jobject t, const jvalue *a) { RET(i, 3); }
static jvalue inf_getCPUSpeedInMhz(jobject t, const jvalue *a) { RET(i, 444); }
static jvalue inf_getSecondsSinceLastBoot(jobject t, const jvalue *a) { RET(d, aod_uptime_seconds()); }
static jvalue inf_init(jobject t, const jvalue *a) { RET(l, sentinel(BF "bf_core/information/Information")); }
static jvalue inf_getAdvertisingInfo(jobject t, const jvalue *a) {
	/* Play Services unavailable: InformationResponse.createError(1) = ("information", "Unavailable", 1) */
	reply *r = mk_reply("Java_com_backflipstudios_bf_1core_information_Information_nativeAdvertisingInfoCallback",
	                    platform_error("information", "Unavailable", 1), a[0].j);
	if (r) r->z = JNI_FALSE;
	reply_now(r_str_bool_err_handle, r);   /* Java: inline when Play Services is unavailable */
	RETV();
}

/* --- security digests. MessageDigest semantics: digest() returns the hash and resets. */
static jvalue sha1_init_(jobject t, const jvalue *a) {
	aod_obj *o = obj_new(BF "bf_core/security/SHA1", K_SHA1);
	if (o) sha1_init(&o->u.sha1);
	RET(l, (jobject)o);
}
static jvalue sha256_init_(jobject t, const jvalue *a) {
	aod_obj *o = obj_new(BF "bf_core/security/SHA256", K_SHA256);
	if (o) aod_sha256_init(&o->u.sha256);
	RET(l, (jobject)o);
}
static jvalue md5_init_(jobject t, const jvalue *a) {
	aod_obj *o = obj_new(BF "bf_core/security/MD5", K_MD5);
	if (o) aod_md5_init(&o->u.md5);
	RET(l, (jobject)o);
}
static void digest_update(jobject t, const uint8_t *p, int n) {
	if (!obj_is_ours(t) || n <= 0) {
		if (!obj_is_ours(t)) aod_log("JNI: digest update on foreign object %p", t);
		return;
	}
	aod_obj *o = (aod_obj *)t;
	switch (o->kind) {
	case K_SHA1: sha1_update(&o->u.sha1, p, n); break;
	case K_SHA256: aod_sha256_update(&o->u.sha256, p, n); break;
	case K_MD5: aod_md5_update(&o->u.md5, p, n); break;
	default: aod_log("JNI: digest update on non-digest %s", o->cls); break;
	}
}
static jvalue dg_update_all(jobject t, const jvalue *a) {
	JavaDynArray *jda = (JavaDynArray *)a[0].l;
	if (jda) digest_update(t, (const uint8_t *)jda->array, jda->len);
	RETV();
}
static jvalue dg_update_range(jobject t, const jvalue *a) {
	JavaDynArray *jda = (JavaDynArray *)a[0].l;
	int off = a[1].i, len = a[2].i;
	if (jda && off >= 0 && len >= 0 && off + len <= jda->len)
		digest_update(t, (const uint8_t *)jda->array + off, len);
	else
		aod_log("JNI: digest update([BII) out of range off=%d len=%d", off, len);
	RETV();
}
static jvalue dg_update_buffer(jobject t, const jvalue *a) {
	/* Needs GetDirectBufferAddress, which FalsoJNI does not implement. Logged so it shows up. */
	aod_log("JNI UNSUPPORTED: digest update(Ljava/nio/ByteBuffer;)V called; digest will be wrong");
	RETV();
}
static jvalue dg_finish(jobject t, const jvalue *a) {
	uint8_t out[32];
	if (!obj_is_ours(t)) RET(l, byte_array(out, 0));
	aod_obj *o = (aod_obj *)t;
	int n = 0;
	switch (o->kind) {
	case K_SHA1: sha1_final(&o->u.sha1, out); sha1_init(&o->u.sha1); n = 20; break;
	case K_SHA256: aod_sha256_final(&o->u.sha256, out); aod_sha256_init(&o->u.sha256); n = 32; break;
	case K_MD5: aod_md5_final(&o->u.md5, out); aod_md5_init(&o->u.md5); n = 16; break;
	default: break;
	}
	RET(l, byte_array(out, n));
}
static jvalue dg_reset(jobject t, const jvalue *a) {
	if (obj_is_ours(t)) {
		aod_obj *o = (aod_obj *)t;
		if (o->kind == K_SHA1) sha1_init(&o->u.sha1);
		if (o->kind == K_SHA256) aod_sha256_init(&o->u.sha256);
		if (o->kind == K_MD5) aod_md5_init(&o->u.md5);
	}
	RETV();
}
static jvalue sha1_len(jobject t, const jvalue *a) { RET(i, 20); }
static jvalue sha256_len(jobject t, const jvalue *a) { RET(i, 32); }
static jvalue md5_len(jobject t, const jvalue *a) { RET(i, 16); }

/* --- Text rendering (platform UI path; in-game text is native FreeType inside libgame) */
static jvalue trp_init(jobject t, const jvalue *a) {
	aod_obj *o = obj_new(BF "bf_ui/TextRenderingParams", K_TR_PARAMS);
	if (o) {
		aod_text_params *p = &o->u.tp;
		p->text = cstr_dup(a[0].l);
		p->line_width = a[1].f; p->line_height = a[2].f; p->min_line_height = a[3].f;
		p->outline_percentage = a[4].f; p->x_content_scale = a[5].f; p->y_content_scale = a[6].f;
		p->max_lines = a[7].i; p->font_color = (uint32_t)a[8].i; p->outline_color = (uint32_t)a[9].i;
		p->alignment = (a[10].i >= 0 && a[10].i <= 2) ? a[10].i : 0;
	}
	RET(l, (jobject)o);
}
static jvalue trp_opt(jobject t, int bit) {
	aod_obj *o = as_obj(t, K_TR_PARAMS);
	if (o) o->u.tp.options |= bit;
	RETV();
}
static jvalue trp_outline(jobject t, const jvalue *a) { return trp_opt(t, AOD_TEXT_OUTLINE); }
static jvalue trp_serif(jobject t, const jvalue *a) { return trp_opt(t, AOD_TEXT_SERIF); }
static jvalue trp_bold(jobject t, const jvalue *a) { return trp_opt(t, AOD_TEXT_BOLD); }
static jvalue trp_alpha(jobject t, const jvalue *a) { return trp_opt(t, AOD_TEXT_ALPHA_ONLY); }
static jvalue tr_prepare(jobject t, const jvalue *a) {
	aod_obj *p = as_obj(a[0].l, K_TR_PARAMS);
	aod_obj *o = obj_new(BF "bf_ui/TextRenderPrepareResult", K_TR_RESULT);
	if (o && p) aod_text_prepare(&p->u.tp, &o->u.tr);
	RET(l, (jobject)o);
}
static jvalue tr_render(jobject t, const jvalue *a) {
	int w = 0, h = 0, bpp = 0;
	uint8_t *px = aod_text_render(a[0].i, &w, &h, &bpp);
	if (!px) RET(l, NULL);   /* Java also returns null for an invalid handle */
	jbyteArray arr = byte_array(px, w * h * bpp);
	free(px);
	RET(l, arr);
}
#define TR_GETTER(fn, field) \
	static jvalue fn(jobject t, const jvalue *a) { aod_obj *o = as_obj(t, K_TR_RESULT); RET(i, o ? o->u.tr.field : 0); }
TR_GETTER(tr_imageWidth, image_width)
TR_GETTER(tr_imageHeight, image_height)
TR_GETTER(tr_renderWidth, render_width)
TR_GETTER(tr_renderHeight, render_height)
TR_GETTER(tr_bpp, bits_per_pixel)
TR_GETTER(tr_handle, render_handle)

/* --- UI. There is no Android Activity; replies follow each class's Java failure path. */
static jobject ui_error(int code, const char *msg) { return platform_error("ui", msg, code); }
/* PlatformAlert (REVIEW-CODE F3): Java adds a button only for a non-null, non-empty label, in
 * the order yes(-1), no(-2), neutral(-3), and reports the pressed button's own `which`. The
 * cancel listener reports (-1, UIResponse CANCELED) and exists only if cancelable. */
static struct { int active, n, which[3], cancelable; jlong handle; } alert;

void aod_jni_alert_result(int index, int64_t handle) {
	jobject err = NULL;
	int which;
	if (!alert.active || handle != alert.handle) {
		aod_log("JNI: PlatformAlert result for unknown handle %lld", (long long)handle);
		return;
	}
	alert.active = 0;
	if (index >= 0 && index < alert.n) {
		which = alert.which[index];
	} else {
		/* Closed without a button: only a cancelable dialog can do that on Android. A
		 * non-cancelable one has no other exit, so report its first button (or cancel). */
		if (alert.cancelable || alert.n == 0) { which = -1; err = ui_error(2, "User cancelled"); }
		else which = alert.which[0];
	}
	aod_log("JNI: PlatformAlert result index %d -> which %d%s", index, which, err ? " (cancelled)" : "");
	reply *r = mk_reply("Java_com_backflipstudios_bf_1ui_PlatformAlert_nativeAlertDismissedCallback", err, handle);
	if (r) { r->i = which; defer(r_int_err_handle, r); }
	else defer(r_int_err_handle, NULL);
}

static jvalue alert_show(jobject t, const jvalue *a) {
	char *title = cstr_dup(a[0].l), *msg = cstr_dup(a[1].l);
	char *label[3] = { cstr_dup(a[2].l), cstr_dup(a[3].l), cstr_dup(a[4].l) };
	static const int java_which[3] = { -1, -2, -3 };
	const char *present[3];
	int n = 0, which[3];
	for (int i = 0; i < 3; i++)
		if (label[i] && label[i][0]) { present[n] = label[i]; which[n] = java_which[i]; n++; }
	aod_log("JNI: PlatformAlert.show(\"%s\", \"%s\", %d buttons, cancelable=%d)", title, msg, n, a[5].z);
	int started = -1;
	if (!alert.active) {
		alert.active = 1;
		alert.n = n;
		memcpy(alert.which, which, sizeof(which));
		alert.cancelable = a[5].z;
		alert.handle = a[6].j;
		started = aod_alert_begin(title, msg, present, n, a[6].j);
		if (started != 0) alert.active = 0;
	}
	if (started != 0) {
		/* Java path when there is no activity to host the dialog. */
		reply *r = mk_reply("Java_com_backflipstudios_bf_1ui_PlatformAlert_nativeAlertDismissedCallback",
		                    ui_error(3, "ApplicationContext main activity is null"), a[6].j);
		if (r) r->i = -1;
		reply_now(r_int_err_handle, r);   /* Java: inline when there is no activity */
	}
	free(title); free(msg);
	for (int i = 0; i < 3; i++) free(label[i]);
	RETV();
}
static jvalue dialog_show(jobject t, const jvalue *a) {
	reply *r = mk_reply("Java_com_backflipstudios_bf_1ui_PlatformDialog_nativeDialogDismissedCallback",
	                    ui_error(2, "User canceled"), a[3].j);
	if (r) { r->i = 0; defer(r_int_err_handle, r); }
	RETV();
}
static jvalue ui_activity_null(jobject t, const jvalue *a) {
	RET(l, ui_error(1, "ApplicationContext main activity is null."));
}
static jvalue video_show(jobject t, const jvalue *a) {
	char *p = cstr_dup(a[0].l);
	aod_log("JNI: VideoActivity.show(\"%s\") - video playback not implemented", p);
	free(p);
	reply_now(r_err_handle, mk_reply("Java_com_backflipstudios_bf_1ui_VideoActivity_nativeVideoActivityDismissed",
	                             ui_error(3, "ApplicationContext main activity is null"), a[2].j));
	RETV();
}
static jvalue email_send(jobject t, const jvalue *a) {
	reply *r = mk_reply("Java_com_backflipstudios_bf_1ui_EmailCompose_nativeEmailComposeDismissed",
	                    ui_error(3, "ApplicationContext main activity instance is null."), a[0].j);
	if (r) r->z = JNI_FALSE;
	reply_now(r_bool_err_handle, r);   /* Java: inline when there is no activity */
	RETV();
}
static jvalue email_init(jobject t, const jvalue *a) { RET(l, sentinel(BF "bf_ui/EmailCompose")); }
static jvalue url_open(jobject t, const jvalue *a) {
	char *u = cstr_dup(a[0].l);
	aod_log("JNI: URLHandler.openExternalURL(\"%s\") ignored", u);
	free(u);
	RETV();
}
static jvalue url_init(jobject t, const jvalue *a) { RET(l, sentinel(BF "bf_ui/URLHandler")); }
static jvalue webview_init(jobject t, const jvalue *a) { RET(l, sentinel(BF "bf_ui/WebView")); }

/* --- IAP: billing unavailable. */
static const char *iap_msg[] = {
	"Ok", "User Canceled", "Service Unavailable", "Billing Unavailable", "Item unavailable",
	"Devloper Error", "Unknown error", "Item Already Owned", "Item Not Owned",
	"Subscriptions not supported", "Request in flight", "Billing service not available",
};
static jobject iap_error(int code) { return platform_error("iap", iap_msg[code], code); }
static jvalue iap_getInstance(jobject t, const jvalue *a) {
	static jobject inst;
	if (!inst) inst = sentinel(BF "bf_iap/google/IAP");
	RET(l, inst);
}
static jvalue iap_init(jobject t, const jvalue *a) {
	/* Java: no billing service -> nativeBillingServiceAvailable(createPlatformError(3)) inline in init() */
	reply_now(r_err_handle, mk_reply("Java_com_backflipstudios_bf_1iap_google_IAP_nativeBillingServiceAvailable",
	                             iap_error(3), a[0].j));
	RETV();
}
static jvalue iap_getProducts(jobject t, const jvalue *a) {
	reply *r = mk_reply("Java_com_backflipstudios_bf_1iap_google_IAP_nativeGetProductsFinished", iap_error(11), a[2].j);
	if (r) { r->z = 1; defer(r_err_array_handle, r); }
	RETV();
}
static jvalue iap_getPurchases(jobject t, const jvalue *a) {
	/* Java: _getPurchases() with no billing service -> createPlatformError(3); the purchases array is
	 * empty (non-null) and nativeGetPurchasesFinished runs inline before getPurchases returns. */
	reply *r = mk_reply("Java_com_backflipstudios_bf_1iap_google_IAP_nativeGetPurchasesFinished", iap_error(3), a[0].j);
	if (r) r->z = 1;
	reply_now(r_err_array_handle, r);
	RETV();
}
static void r_purchase(void *a) {
	reply *r = a;
	void (*f)(JNIEnv *, jclass, jobject, jobject, jlong) = reply_fn(r);
	if (f) f(&jni, NULL, r->err, NULL, r->handle);
	free(r);
}
static jvalue iap_purchase(jobject t, const jvalue *a) {
	/* Java passes a null Purchase on every error path. */
	/* Java doPurchase(): billing service null -> nativePurchaseFinished(createPlatformError(11), null) inline */
	reply_now(r_purchase, mk_reply("Java_com_backflipstudios_bf_1iap_google_IAP_nativePurchaseFinished",
	                             iap_error(11), a[2].j));
	RETV();
}
static jvalue iap_consume(jobject t, const jvalue *a) { RET(l, iap_error(11)); }

/* --- HTTP: no network. execute() returns PlatformError(errorDomain, msg, noConnectionCode). */
static jvalue http_init(jobject t, const jvalue *a) { RET(l, sentinel(BF "bf_http/HttpClient")); }
static jvalue http_newConnection(jobject t, const jvalue *a) { RET(l, sentinel(BF "bf_http/HttpConnection")); }
static jvalue http_execute(jobject t, const jvalue *a) {
	char *dom = cstr_dup(a[1].l);
	jobject e = platform_error(dom, "No network connection (PS Vita port is offline)", a[2].i);
	free(dom);
	RET(l, e);
}
static jvalue i_zero(jobject t, const jvalue *a) { RET(i, 0); }
static jvalue bytes_empty(jobject t, const jvalue *a) { RET(l, byte_array(NULL, 0)); }
static jvalue arr_empty(jobject t, const jvalue *a) { RET(l, empty_array()); }

/* --- analytics / crash reporting / ads: fire-and-forget in Java, instances are opaque. */
static jvalue flurry_getInstance(jobject t, const jvalue *a) { static jobject o; if (!o) o = sentinel(BF "bf_flurry/FlurryAnalytics"); RET(l, o); }
static jvalue localytics_make(jobject t, const jvalue *a) { static jobject o; if (!o) o = sentinel(BF "bf_localytics/BFLocalytics"); RET(l, o); }
static jvalue hockey_getInstance(jobject t, const jvalue *a) { static jobject o; if (!o) o = sentinel(BF "bf_hockeyapp/HockeyApp"); RET(l, o); }
static jvalue conv_init(jobject t, const jvalue *a) { RET(l, sentinel(BF "bf_google_conversion_tracking/ConversionTracking")); }
static jvalue gva_init(jobject t, const jvalue *a) { RET(l, sentinel(BF "bf_google_video_ads/GoogleVideoAds")); }
static jobject gva_error(int code, const char *msg) { return platform_error("GoogleVideoAds", msg, code); }
static jvalue gva_preload(jobject t, const jvalue *a) {
	defer(r_handle_err, mk_reply("Java_com_backflipstudios_bf_1google_1video_1ads_GoogleVideoAds_nativePreloadAdCallback",
	                             gva_error(2, "Ad Not Available"), a[0].j));
	RETV();
}
static jvalue gva_isAdReady(jobject t, const jvalue *a) {
	reply *r = mk_reply("Java_com_backflipstudios_bf_1google_1video_1ads_GoogleVideoAds_nativeIsAdReadyCallback", NULL, a[0].j);
	if (r) { r->z = JNI_FALSE; defer(r_handle_bool_err, r); }
	RETV();
}
static jvalue gva_show(jobject t, const jvalue *a) {
	defer(r_handle_float_str_err,
	      mk_reply("Java_com_backflipstudios_bf_1google_1video_1ads_GoogleVideoAds_nativeShowVideoCallback",
	               gva_error(2, "Ad Not Available"), a[0].j));
	RETV();
}
static jvalue banner_create(jobject t, const jvalue *a) { RET(l, sentinel(BF "bf_ads/google_mobile_ads/BannerAd")); }
static jvalue inter_init(jobject t, const jvalue *a) {
	aod_obj *o = obj_new(BF "bf_ads/google_mobile_ads/InterstitialAd", K_AD);
	if (o) o->u.ad.handle = a[0].j;
	RET(l, (jobject)o);
}
static jvalue inter_create(jobject t, const jvalue *a) {
	aod_obj *o = as_obj(t, K_AD);
	ad_reply *r = o ? malloc(sizeof(*r)) : NULL;
	if (r) {
		r->thiz = t;
		r->handle = o->u.ad.handle;
		defer(r_interstitial, r);
	}
	RETV();
}

/* --- notifications */
static jobject notif_error(int code, const char *msg) { return platform_error("notification", msg, code); }
static jvalue notif_notify(jobject t, const jvalue *a) { RET(l, notif_error(2, "Not Available")); }
/* NotificationManager's private no-arg constructor (boot-01: "Unable to find method '<init>'"). */
static jvalue notif_mgr_init(jobject t, const jvalue *a) {
	static jobject o;
	if (!o) o = sentinel(BF "bf_notification/NotificationManager");
	RET(l, o);
}
static jvalue push_init(jobject t, const jvalue *a) { RET(l, sentinel(BF "bf_notification_google/PushNotifications")); }
static jvalue push_register(jobject t, const jvalue *a) {
	defer(r_str_handle_err, mk_reply("Java_com_backflipstudios_bf_1notification_1google_PushNotifications_nativeOnPushTokenReceived",
	                                 notif_error(2, "Not Available: Remote notifications not available."), a[0].j));
	RETV();
}

#define M(c, n, s, f) { c, n, s, f }
static const method methods[] = {
	/* java.lang */
	M("*", "getClass", "()Ljava/lang/Class;", obj_getClass),
	M("java/lang/Class", "getName", "()" S_STR, class_getName),
	M("*", "toString", "()" S_STR, obj_toString),

	/* bf_core/application */
	M(BF "bf_core/application/ApplicationContext", "getMainAssetManager", "()Landroid/content/res/AssetManager;", ac_getMainAssetManager),
	M(BF "bf_core/application/ApplicationContext", "get", "(" S_STR ")Ljava/lang/Object;", ac_get),
	M(BF "bf_core/application/ApplicationContext", "set", "(" S_STR "Ljava/lang/Object;)V", ac_set),
	M(BF "bf_core/application/ApplicationContext", "exit", "()V", ac_exit),
	M(BF "bf_core/application/SharedPreferencesAccess", "getInt", "(" S_STR "I)I", prefs_getInt),

	/* bf_core/error */
	M(BF "bf_core/error/PlatformError", "<init>", "(" S_STR S_STR "I)V", pe_init),
	M(BF "bf_core/error/PlatformError", "getDomain", "()" S_STR, pe_getDomain),
	M(BF "bf_core/error/PlatformError", "getMessage", "()" S_STR, pe_getMessage),
	M(BF "bf_core/error/PlatformError", "getCode", "()I", pe_getCode),
	M(BF "bf_core/error/PlatformError", "toString", "()" S_STR, pe_toString),

	/* bf_core/information/Information */
#define INF BF "bf_core/information/Information"
	M(INF, "<init>", "()V", inf_init),
	M(INF, "getAdvertisingInfo", "(J)V", inf_getAdvertisingInfo),
	M(INF, "getApplicationVersion", "()" S_STR, inf_getApplicationVersion),
	M(INF, "getApplicationVersionShort", "()" S_STR, inf_getApplicationVersionShort),
	M(INF, "getApplicationName", "()" S_STR, inf_getApplicationName),
	M(INF, "getApplicationRootWritePath", "()" S_STR, inf_getRootWritePath),
	M(INF, "getApplicationExternalRootWritePath", "()" S_STR, inf_empty),
	M(INF, "isExternalStorageAvailable", "()Z", z_false),
	M(INF, "getAvailableDiskSpaceBytes", "()J", inf_diskSpace),
	M(INF, "getAvailableExternalDiskSpaceBytes", "()J", j_zero),
	M(INF, "isExternalStorageEmulated", "()Z", z_false),
	M(INF, "getDeviceModel", "()" S_STR, inf_getDeviceModel),
	M(INF, "getDeviceName", "()" S_STR, inf_getDeviceModel),
	M(INF, "getSystemVersion", "()" S_STR, inf_getSystemVersion),
	M(INF, "getSecondsSinceLastBoot", "()D", inf_getSecondsSinceLastBoot),
	M(INF, "getDeviceUniqueIdentifier", "()" S_STR, inf_getDeviceUniqueIdentifier),
	M(INF, "isConnectedToNetwork", "()Z", z_false),
	M(INF, "isTabletDevice", "()Z", z_false),
	M(INF, "getScreenLayout", "()" S_STR, inf_getScreenLayout),
	M(INF, "getDensityDPI", "()" S_STR, inf_getDensityDPI),
	M(INF, "getNetworkTypeAsString", "()" S_STR, inf_getNetworkType),
	M(INF, "hasAccelerometer", "()Z", z_false),
	M(INF, "hasGyroscope", "()Z", z_false),
	M(INF, "hasCompass", "()Z", z_false),
	M(INF, "locationServicesSupported", "()Z", z_false),
	M(INF, "getMemorySizeinMB", "()I", inf_getMemorySizeinMB),
	M(INF, "getDisplayDPI_X", "()F", inf_getDisplayDPI),
	M(INF, "getDisplayDPI_Y", "()F", inf_getDisplayDPI),
	M(INF, "getDisplayScale", "()F", inf_getDisplayScale),
	M(INF, "getDisplayWidth", "()I", inf_getDisplayWidth),
	M(INF, "getDisplayHeight", "()I", inf_getDisplayHeight),
	M(INF, "getUsableDisplayWidth", "()I", inf_getDisplayWidth),
	M(INF, "getUsableDisplayHeight", "()I", inf_getDisplayHeight),
	M(INF, "getCPUCoreCount", "()I", inf_getCPUCoreCount),
	M(INF, "getCPUSpeedInMhz", "()I", inf_getCPUSpeedInMhz),
	M(INF, "getCPUName", "()" S_STR, inf_getCPUName),
	M(INF, "getArchitecture", "()" S_STR, inf_getArchitecture),
	M(INF, "getPowerSaveMode", "()Z", z_false),
	M(INF, "getLanguage", "()" S_STR, inf_getLanguage),
	M(INF, "getLanguageScript", "()" S_STR, inf_empty),
	M(INF, "getCountryCode", "()" S_STR, inf_getCountryCode),
	M(INF, "getTimeZone", "()" S_STR, inf_getTimeZone),
	M(INF, "getOpenGLContextVersion", "()" S_STR, inf_getOpenGLContextVersion),

	/* bf_core/security */
#define SEC BF "bf_core/security/"
	M(SEC "SHA1", "<init>", "()V", sha1_init_),
	M(SEC "SHA1", "getDigestLength", "()I", sha1_len),
	M(SEC "SHA1", "update", "([B)V", dg_update_all),
	M(SEC "SHA1", "update", "([BII)V", dg_update_range),
	M(SEC "SHA1", "update", "(Ljava/nio/ByteBuffer;)V", dg_update_buffer),
	M(SEC "SHA1", "finish", "()[B", dg_finish),
	M(SEC "SHA1", "reset", "()V", dg_reset),
	M(SEC "SHA256", "<init>", "()V", sha256_init_),
	M(SEC "SHA256", "getDigestLength", "()I", sha256_len),
	M(SEC "SHA256", "update", "([B)V", dg_update_all),
	M(SEC "SHA256", "update", "([BII)V", dg_update_range),
	M(SEC "SHA256", "update", "(Ljava/nio/ByteBuffer;)V", dg_update_buffer),
	M(SEC "SHA256", "finish", "()[B", dg_finish),
	M(SEC "SHA256", "reset", "()V", dg_reset),
	M(SEC "MD5", "<init>", "()V", md5_init_),
	M(SEC "MD5", "getDigestLength", "()I", md5_len),
	M(SEC "MD5", "update", "([B)V", dg_update_all),
	M(SEC "MD5", "update", "([BII)V", dg_update_range),
	M(SEC "MD5", "update", "(Ljava/nio/ByteBuffer;)V", dg_update_buffer),
	M(SEC "MD5", "finish", "()[B", dg_finish),
	M(SEC "MD5", "reset", "()V", dg_reset),

	/* bf_ui text rendering */
	M(BF "bf_ui/TextRenderingParams", "<init>", "(" S_STR "FFFFFFIIII)V", trp_init),
	M(BF "bf_ui/TextRenderingParams", "setRenderOutline", "()V", trp_outline),
	M(BF "bf_ui/TextRenderingParams", "setUseSerif", "()V", trp_serif),
	M(BF "bf_ui/TextRenderingParams", "setUseBold", "()V", trp_bold),
	M(BF "bf_ui/TextRenderingParams", "setOutputAlphaOnly", "()V", trp_alpha),
	M(BF "bf_ui/TextRendering", "prepareToRenderText",
	  "(Lcom/backflipstudios/bf_ui/TextRenderingParams;)Lcom/backflipstudios/bf_ui/TextRenderPrepareResult;", tr_prepare),
	M(BF "bf_ui/TextRendering", "renderText", "(I)[B", tr_render),
	M(BF "bf_ui/TextRenderPrepareResult", "getImageWidth", "()I", tr_imageWidth),
	M(BF "bf_ui/TextRenderPrepareResult", "getImageHeight", "()I", tr_imageHeight),
	M(BF "bf_ui/TextRenderPrepareResult", "getRenderWidth", "()I", tr_renderWidth),
	M(BF "bf_ui/TextRenderPrepareResult", "getRenderHeight", "()I", tr_renderHeight),
	M(BF "bf_ui/TextRenderPrepareResult", "getBitsPerPixel", "()I", tr_bpp),
	M(BF "bf_ui/TextRenderPrepareResult", "getRenderHandle", "()I", tr_handle),

	/* bf_ui dialogs etc. */
	M(BF "bf_ui/PlatformAlert", "show", "(" S_STR S_STR S_STR S_STR S_STR "ZJ)V", alert_show),
	M(BF "bf_ui/PlatformDialog", "show", "(" S_STR S_STR "[" S_STR "J)V", dialog_show),
	M(BF "bf_ui/TextInput", "show", "(" S_STR S_STR S_STR S_STR "J)" S_ERR, ui_activity_null),
	M(BF "bf_ui/WebView", "show", "(" S_STR S_STR "IJ)" S_ERR, ui_activity_null),
	M(BF "bf_ui/WebView", "<init>", "(J)V", webview_init),
	M(BF "bf_ui/VideoActivity", "show", "(" S_STR "IJ)V", video_show),
	M(BF "bf_ui/DatePicker", "showDatePicker", "(DDD" S_STR "J)" S_ERR, ui_activity_null),
	M(BF "bf_ui/EmailCompose", "<init>", "()V", email_init),
	M(BF "bf_ui/EmailCompose", "send", "(J)V", email_send),
	M(BF "bf_ui/EmailCompose", "setSubject", "(" S_STR ")V", v_noop),
	M(BF "bf_ui/EmailCompose", "setPlainTextBody", "(" S_STR ")V", v_noop),
	M(BF "bf_ui/EmailCompose", "setHTMLBody", "(" S_STR ")V", v_noop),
	M(BF "bf_ui/EmailCompose", "addToRecipient", "(" S_STR ")V", v_noop),
	M(BF "bf_ui/URLHandler", "<init>", "()V", url_init),
	M(BF "bf_ui/URLHandler", "openExternalURL", "(" S_STR ")V", url_open),

	/* bf_iap */
#define IAP BF "bf_iap/google/IAP"
	M(IAP, "getInstance", "()Lcom/backflipstudios/bf_iap/google/IAP;", iap_getInstance),
	M(IAP, "init", "(J)V", iap_init),
	M(IAP, "canPurchase", "()Z", z_false),
	M(IAP, "getProducts", "([" S_STR "[" S_STR "J)V", iap_getProducts),
	M(IAP, "getPurchases", "(J)V", iap_getPurchases),
	M(IAP, "purchaseProduct", "(" S_STR S_STR "J)V", iap_purchase),
	M(IAP, "consumePurchase", "(Lcom/backflipstudios/bf_iap/google/Purchase;)" S_ERR, iap_consume),

	/* bf_http */
	M(BF "bf_http/HttpClient", "<init>", "()V", http_init),
	M(BF "bf_http/HttpClient", "newConnection", "(" S_STR ")Lcom/backflipstudios/bf_http/HttpConnection;", http_newConnection),
	M(BF "bf_http/HttpConnection", "addHeader", "(" S_STR S_STR ")V", v_noop),
	M(BF "bf_http/HttpConnection", "setTimeoutSec", "(I)V", v_noop),
	M(BF "bf_http/HttpConnection", "setBody", "([B)V", v_noop),
	M(BF "bf_http/HttpConnection", "setBodyFilePath", "(" S_STR ")V", v_noop),
	M(BF "bf_http/HttpConnection", "setResponseBodyFile", "(" S_STR ")V", v_noop),
	M(BF "bf_http/HttpConnection", "execute", "(" S_STR S_STR "III)" S_ERR, http_execute),
	M(BF "bf_http/HttpConnection", "getResponseData", "()[B", bytes_empty),
	M(BF "bf_http/HttpConnection", "getResponseCode", "()I", i_zero),
	M(BF "bf_http/HttpConnection", "getResponseHeaderNames", "()[" S_STR, arr_empty),
	M(BF "bf_http/HttpConnection", "getResponseHeaderValues", "(" S_STR ")[" S_STR, arr_empty),

	/* analytics, crash reporting */
	M(BF "bf_flurry/FlurryAnalytics", "getInstance", "(" S_STR "Z)Lcom/backflipstudios/bf_flurry/FlurryAnalytics;", flurry_getInstance),
	M(BF "bf_flurry/FlurryAnalytics", "startSession", "()V", v_noop),
	M(BF "bf_flurry/FlurryAnalytics", "stopSession", "()V", v_noop),
	M(BF "bf_flurry/FlurryAnalytics", "logEvent", "(" S_STR "[" S_STR "[" S_STR ")V", v_noop),
	M(BF "bf_flurry/FlurryAnalytics", "logError", "(" S_STR S_STR ")V", v_noop),
	M(BF "bf_localytics/BFLocalytics", "makeInstance", "(" S_STR "Z)Lcom/backflipstudios/bf_localytics/BFLocalytics;", localytics_make),
	M(BF "bf_localytics/BFLocalytics", "startSession", "()V", v_noop),
	M(BF "bf_localytics/BFLocalytics", "associateUserId", "(" S_STR ")V", v_noop),
	M(BF "bf_localytics/BFLocalytics", "associateUserInformation", "([" S_STR "[" S_STR ")V", v_noop),
	M(BF "bf_localytics/BFLocalytics", "disassociateUserInformation", "([" S_STR ")V", v_noop),
	M(BF "bf_localytics/BFLocalytics", "tagEvent", "(" S_STR "[" S_STR "[" S_STR "J)V", v_noop),
	M(BF "bf_localytics/BFLocalytics", "tagScreen", "(" S_STR ")V", v_noop),
	M(BF "bf_localytics/BFLocalytics", "setCustomDimensions", "([" S_STR ")V", v_noop),
	M(BF "bf_hockeyapp/HockeyApp", "getInstance", "(" S_STR ")Lcom/backflipstudios/bf_hockeyapp/HockeyApp;", hockey_getInstance),
	M(BF "bf_hockeyapp/HockeyApp", "platformCrash", "()V", v_noop),
	M(BF "bf_hockeyapp/HockeyApp", "delayedExitOnNativeCrash", "()V", v_noop),
	M(BF "bf_applovin/AppLovin", "registerAdMediationAdaptors", "()V", v_noop),
	M(BF "bf_google_conversion_tracking/ConversionTracking", "<init>", "()V", conv_init),
	M(BF "bf_google_conversion_tracking/ConversionTracking", "TrackConversion", "(" S_STR ")V", v_noop),
	M(BF "bf_overmind/Overmind", "isApplicationInstalled", "(" S_STR ")Z", z_false),

	/* ads */
#define GVA BF "bf_google_video_ads/GoogleVideoAds"
	M(GVA, "<init>", "()V", gva_init),
	M(GVA, "enable", "()V", v_noop),
	M(GVA, "dispose", "()V", v_noop),
	M(GVA, "preloadAd", "(J" S_STR ")V", gva_preload),
	M(GVA, "isAdReady", "(J" S_STR ")V", gva_isAdReady),
	M(GVA, "showIncentivizedVideoAd", "(J" S_STR ")V", gva_show),
	M(GVA, "cancelPreload", "()" S_ERR, l_null),
#define BAN BF "bf_ads/google_mobile_ads/BannerAd"
	M(BAN, "createInstance", "(" S_STR "ZI" S_STR ")Lcom/backflipstudios/bf_ads/google_mobile_ads/BannerAd;", banner_create),
	M(BAN, "setTargetsChildren", "(Z)V", v_noop),
	M(BAN, "setTargetingParams", "([" S_STR "[" S_STR ")V", v_noop),
	M(BAN, "show", "()V", v_noop),
	M(BAN, "hide", "()V", v_noop),
	M(BAN, "refresh", "()V", v_noop),
	M(BAN, "destroy", "()V", v_noop),
#define INT BF "bf_ads/google_mobile_ads/InterstitialAd"
	M(INT, "<init>", "(J)V", inter_init),
	M(INT, "dispose", "()V", v_noop),
	M(INT, "isInterstitialValid", "()Z", z_false),
	M(INT, "createInterstitial", "([" S_STR "[" S_STR S_STR "Z)V", inter_create),
	M(INT, "showInterstitial", "()V", v_noop),
	M(INT, "releaseInterstitial", "()V", v_noop),

	/* notifications */
#define NM BF "bf_notification/NotificationManager"
	M(NM, "notifyIn", "(" S_STR S_STR "[" S_STR "J)" S_ERR, notif_notify),
	M(NM, "notifyAt", "(" S_STR S_STR "[" S_STR "J)" S_ERR, notif_notify),
	M(NM, "getScheduledLocalNotifications", "()[Lcom/backflipstudios/bf_notification/Notification;", arr_empty),
	M(NM, "cancelScheduledLocalNotification", "(" S_STR ")V", v_noop),
	M(NM, "consumeReceivedNotification", "(" S_STR ")V", v_noop),
	M(NM, "<init>", "()V", notif_mgr_init),
	M(NM, "dispose", "()V", v_noop),
	M(NM, "cancel", "(I)V", v_noop),
	M(NM, "consumeAllReceivedLocalNotifications", "()V", v_noop),
	M(NM, "consumeAllReceivedRemoteNotifications", "()V", v_noop),
#define PN BF "bf_notification_google/PushNotifications"
	M(PN, "<init>", "()V", push_init),
	M(PN, "areRemoteNotificationsAvailable", "()Z", z_false),
	M(PN, "dispose", "()V", v_noop),
	M(PN, "setGCMNotificationSenderID", "(" S_STR ")V", v_noop),
	M(PN, "registerForRemoteNotifications", "(J)V", push_register),
};
#define N_METHODS ((int)(sizeof(methods) / sizeof(methods[0])))

/* ---------------------------------------------------------- static ints */

typedef struct { const char *cls, *name; int value; } static_int;
static const static_int static_ints[] = {
#include "aod/gen_static_ints.h"
};
#define N_STATIC_INTS ((int)(sizeof(static_ints) / sizeof(static_ints[0])))

/* ---------------------------------------------------------- lookup */

static const char *class_of(jclass clazz) {
	if (!clazz || (uintptr_t)clazz == 0x42424242) return "java/lang/Object";
	if (obj_is_ours(clazz)) return ((aod_obj *)clazz)->cls;
	return (const char *)clazz;   /* FalsoJNI FindClass: strdup of the class name */
}

static int sig_params_equal(const char *a, const char *b) {
	/* compare up to and including ')' */
	while (*a && *a == *b) { if (*a == ')') return 1; a++; b++; }
	return 0;
}

static jmethodID find_method(jclass clazz, const char *name, const char *sig, const char *api) {
	const char *cls = class_of(clazz);
	const method *exact = NULL, *by_params = NULL, *by_name = NULL, *wild = NULL;
	int n_by_name = 0;
	for (int i = 0; i < N_METHODS; i++) {
		const method *m = &methods[i];
		if (strcmp(m->name, name) != 0) continue;
		if (strcmp(m->cls, "*") == 0) { if (!wild) wild = m; continue; }
		if (strcmp(m->cls, cls) == 0) {
			if (sig && strcmp(m->sig, sig) == 0) exact = m;
			else if (sig && sig_params_equal(m->sig, sig)) by_params = m;
			else if (!by_name) by_name = m;
		}
		n_by_name++;
	}
	/* REVIEW-CODE F2: a name-only match with different parameters would decode the caller's
	 * va_list with the wrong layout; accept it only when no signature was given. */
	if (by_name && sig) {
		/* only a problem when no exact/param-equal overload exists (boot-01 logged a false
		 * MISSING for SHA256.update([B)V, which then resolved exactly) */
		if (!exact && !by_params)
			aod_log("JNI MISSING %s %s.%s %s (only overload: %s)", api, cls, name, sig, by_name->sig);
		by_name = NULL;
	}
	const method *r = exact ? exact : by_params ? by_params : by_name;
	/* instance call through GetObjectClass on a foreign object: accept a unique name+sig */
	if (!r && strcmp(cls, "java/lang/Object") == 0 && n_by_name >= 1) {
		for (int i = 0; i < N_METHODS; i++)
			if (strcmp(methods[i].name, name) == 0 && sig && strcmp(methods[i].sig, sig) == 0) { r = &methods[i]; break; }
	}
	if (!r && wild && (!sig || sig_params_equal(wild->sig, sig))) r = wild;
	if (!r) {
		aod_log("JNI MISSING %s %s.%s %s", api, cls, name, sig ? sig : "(null)");
		return NULL;
	}
	if (sig && strcmp(r->sig, sig) != 0)
		aod_log("JNI %s %s.%s native-sig %s != java-sig %s (using java impl %s.%s)", api, cls, name, sig, r->sig, r->cls, r->name);
	else
		aod_log("JNI %s %s.%s %s", api, cls, name, sig ? sig : "");
	return (jmethodID)r;
}

static jmethodID j_GetMethodID(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
	return find_method(clazz, name, sig, "GetMethodID");
}
static jmethodID j_GetStaticMethodID(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
	return find_method(clazz, name, sig, "GetStaticMethodID");
}

static void field_miss(const char *op, jfieldID f);
static jfieldID j_GetStaticFieldID(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
	const char *cls = class_of(clazz);
	for (int i = 0; i < N_STATIC_INTS; i++)
		if (strcmp(static_ints[i].cls, cls) == 0 && strcmp(static_ints[i].name, name) == 0) {
			aod_log("JNI GetStaticFieldID %s.%s %s = %d", cls, name, sig, static_ints[i].value);
			return (jfieldID)&static_ints[i];
		}
	aod_log("JNI MISSING GetStaticFieldID %s.%s %s", cls, name, sig);
	return NULL;
}
static int is_static_int(jfieldID f) {
	return (const static_int *)f >= static_ints && (const static_int *)f < static_ints + N_STATIC_INTS;
}
static jint j_GetStaticIntField(JNIEnv *env, jclass clazz, jfieldID f) {
	if (!is_static_int(f)) { field_miss("GetStaticIntField", f); return 0; }
	return ((const static_int *)f)->value;
}
static jfieldID j_GetFieldID(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
	aod_log("JNI MISSING GetFieldID %s.%s %s", class_of(clazz), name, sig);
	return NULL;
}

/* REVIEW-CODE F1: every field accessor is ours. FalsoJNI's would map unknown or NULL ids to
 * its sample table (id 0 = the C string "window") or to non-null defaults (0x42424242, 1, 'a'),
 * i.e. a false success. Here only ids from static_ints[] have values; anything else is logged
 * and reads as 0/NULL/false, writes are dropped. */
static void field_miss(const char *op, jfieldID f) {
	static int n;
	if (n++ < 200) aod_log("JNI MISSING field %s id=%p", op, (void *)f);
}
#define FIELD_GET(Type, jtype, zero) \
	static jtype j_Get##Type##Field(JNIEnv *e, jobject o, jfieldID f) { field_miss("Get" #Type "Field", f); return zero; } \
	static jtype j_GetStatic##Type##Field(JNIEnv *e, jclass c, jfieldID f) { field_miss("GetStatic" #Type "Field", f); return zero; }
#define FIELD_SET(Type, jtype) \
	static void j_Set##Type##Field(JNIEnv *e, jobject o, jfieldID f, jtype v) { field_miss("Set" #Type "Field", f); } \
	static void j_SetStatic##Type##Field(JNIEnv *e, jclass c, jfieldID f, jtype v) { field_miss("SetStatic" #Type "Field", f); }
FIELD_GET(Object, jobject, NULL)
FIELD_GET(Boolean, jboolean, JNI_FALSE)
FIELD_GET(Byte, jbyte, 0)
FIELD_GET(Char, jchar, 0)
FIELD_GET(Short, jshort, 0)
FIELD_GET(Long, jlong, 0)
FIELD_GET(Float, jfloat, 0.0f)
FIELD_GET(Double, jdouble, 0.0)
FIELD_SET(Object, jobject)
FIELD_SET(Boolean, jboolean)
FIELD_SET(Byte, jbyte)
FIELD_SET(Char, jchar)
FIELD_SET(Short, jshort)
FIELD_SET(Int, jint)
FIELD_SET(Long, jlong)
FIELD_SET(Float, jfloat)
FIELD_SET(Double, jdouble)
static jint j_GetIntField(JNIEnv *e, jobject o, jfieldID f) {
	if (is_static_int(f)) return ((const static_int *)f)->value;
	field_miss("GetIntField", f);
	return 0;
}

static jclass j_GetObjectClass(JNIEnv *env, jobject obj) {
	return (jclass)intern_class(obj_is_ours(obj) ? ((aod_obj *)obj)->cls : "java/lang/Object");
}

static void (*falso_DeleteGlobalRef)(JNIEnv *, jobject);
static void j_DeleteGlobalRef(JNIEnv *env, jobject obj) {
	if (obj_is_ours(obj) || is_interned(obj)) return;   /* see obj_new / intern_class: never freed */
	falso_DeleteGlobalRef(env, obj);
}

static jboolean j_IsInstanceOf(JNIEnv *env, jobject obj, jclass clazz) {
	if (!obj) return JNI_TRUE;   /* JNI spec: null is an instance of every class */
	if (obj_is_ours(obj)) return strcmp(((aod_obj *)obj)->cls, class_of(clazz)) == 0;
	return JNI_FALSE;
}

/* ---------------------------------------------------------- calls */

#define MAX_ARGS 16

static int decode_va(const char *sig, va_list ap, jvalue *out) {
	int n = 0;
	const char *p = sig && *sig == '(' ? sig + 1 : "";
	while (*p && *p != ')' && n < MAX_ARGS) {
		switch (*p) {
		case 'Z': case 'B': case 'C': case 'S': case 'I': out[n].i = va_arg(ap, jint); break;
		case 'J': out[n].j = va_arg(ap, jlong); break;
		case 'F': out[n].f = (jfloat)va_arg(ap, double); break;
		case 'D': out[n].d = va_arg(ap, double); break;
		case 'L': out[n].l = va_arg(ap, jobject); while (*p && *p != ';') p++; break;
		case '[':
			out[n].l = va_arg(ap, jobject);
			while (*p == '[') p++;
			if (*p == 'L') while (*p && *p != ';') p++;
			break;
		default: return n;
		}
		n++; p++;
	}
	return n;
}

static int copy_a(const char *sig, const jvalue *in, jvalue *out) {
	int n = 0;
	const char *p = sig && *sig == '(' ? sig + 1 : "";
	while (*p && *p != ')' && n < MAX_ARGS) {
		switch (*p) {
		case 'Z': out[n].i = in[n].z; break;
		case 'B': out[n].i = in[n].b; break;
		case 'C': out[n].i = in[n].c; break;
		case 'S': out[n].i = in[n].s; break;
		default: out[n] = in[n]; break;
		}
		if (*p == 'L') while (*p && *p != ';') p++;
		if (*p == '[') { while (*p == '[') p++; if (*p == 'L') while (*p && *p != ';') p++; }
		n++; p++;
	}
	return n;
}

/* Ads, analytics, crash reporting, push, cross-promotion and network services are ablated:
 * the bridge answers them from the offline/unavailable Java paths and never reaches a network.
 * The first few calls of each are logged as evidence. */
static const char *const ablated_prefixes[] = {
	BF "bf_ads/", BF "bf_google_video_ads/", BF "bf_applovin/", BF "bf_flurry/", BF "bf_localytics/",
	BF "bf_hockeyapp/", BF "bf_google_conversion_tracking/", BF "bf_notification", BF "bf_overmind/",
	BF "bf_http/", BF "bf_iap/",
};
/* boot-01 showed that cached method ids make the lookup log go silent; trace the calls themselves. */
static void trace_call(const method *m) {
	static unsigned n;
	if (n < 4000) aod_log("JNI CALL #%u %s.%s%s", n, m->cls, m->name, m->sig);
	else if (n == 4000) aod_log("JNI CALL trace limit reached");
	n++;
}

static void note_ablated(const method *m) {
	static int seen;
	for (size_t i = 0; i < sizeof(ablated_prefixes) / sizeof(ablated_prefixes[0]); i++)
		if (strncmp(m->cls, ablated_prefixes[i], strlen(ablated_prefixes[i])) == 0) {
			if (seen++ < 200) aod_log("ABLATED %s.%s%s", m->cls, m->name, m->sig);
			return;
		}
}

static jvalue invoke_v(jobject thiz, jmethodID id, va_list ap) {
	jvalue args[MAX_ARGS];
	memset(args, 0, sizeof(args));
	if (!id) {
		aod_log("JNI call with NULL method id");
		RETV();
	}
	const method *m = (const method *)id;
	trace_call(m);
	note_ablated(m);
	decode_va(m->sig, ap, args);
	return m->fn(thiz, args);
}

static jvalue invoke_a(jobject thiz, jmethodID id, const jvalue *a) {
	jvalue args[MAX_ARGS];
	memset(args, 0, sizeof(args));
	if (!id) {
		aod_log("JNI call with NULL method id");
		RETV();
	}
	const method *m = (const method *)id;
	trace_call(m);
	note_ablated(m);
	if (a) copy_a(m->sig, a, args);
	return m->fn(thiz, args);
}

#define DEF_CALLS(Type, jtype, field) \
	static jtype j_Call##Type##MethodV(JNIEnv *e, jobject o, jmethodID id, va_list ap) { return invoke_v(o, id, ap).field; } \
	static jtype j_Call##Type##Method(JNIEnv *e, jobject o, jmethodID id, ...) { \
		va_list ap; va_start(ap, id); jtype r = invoke_v(o, id, ap).field; va_end(ap); return r; } \
	static jtype j_Call##Type##MethodA(JNIEnv *e, jobject o, jmethodID id, const jvalue *a) { return invoke_a(o, id, a).field; } \
	static jtype j_CallStatic##Type##MethodV(JNIEnv *e, jclass c, jmethodID id, va_list ap) { return invoke_v(NULL, id, ap).field; } \
	static jtype j_CallStatic##Type##Method(JNIEnv *e, jclass c, jmethodID id, ...) { \
		va_list ap; va_start(ap, id); jtype r = invoke_v(NULL, id, ap).field; va_end(ap); return r; } \
	static jtype j_CallStatic##Type##MethodA(JNIEnv *e, jclass c, jmethodID id, const jvalue *a) { return invoke_a(NULL, id, a).field; } \
	static jtype j_CallNonvirtual##Type##MethodV(JNIEnv *e, jobject o, jclass c, jmethodID id, va_list ap) { return invoke_v(o, id, ap).field; } \
	static jtype j_CallNonvirtual##Type##Method(JNIEnv *e, jobject o, jclass c, jmethodID id, ...) { \
		va_list ap; va_start(ap, id); jtype r = invoke_v(o, id, ap).field; va_end(ap); return r; } \
	static jtype j_CallNonvirtual##Type##MethodA(JNIEnv *e, jobject o, jclass c, jmethodID id, const jvalue *a) { return invoke_a(o, id, a).field; }

DEF_CALLS(Object, jobject, l)
DEF_CALLS(Boolean, jboolean, z)
DEF_CALLS(Byte, jbyte, b)
DEF_CALLS(Char, jchar, c)
DEF_CALLS(Short, jshort, s)
DEF_CALLS(Int, jint, i)
DEF_CALLS(Long, jlong, j)
DEF_CALLS(Float, jfloat, f)
DEF_CALLS(Double, jdouble, d)

static void j_CallVoidMethodV(JNIEnv *e, jobject o, jmethodID id, va_list ap) { invoke_v(o, id, ap); }
static void j_CallVoidMethod(JNIEnv *e, jobject o, jmethodID id, ...) { va_list ap; va_start(ap, id); invoke_v(o, id, ap); va_end(ap); }
static void j_CallVoidMethodA(JNIEnv *e, jobject o, jmethodID id, const jvalue *a) { invoke_a(o, id, a); }
static void j_CallStaticVoidMethodV(JNIEnv *e, jclass c, jmethodID id, va_list ap) { invoke_v(NULL, id, ap); }
static void j_CallStaticVoidMethod(JNIEnv *e, jclass c, jmethodID id, ...) { va_list ap; va_start(ap, id); invoke_v(NULL, id, ap); va_end(ap); }
static void j_CallStaticVoidMethodA(JNIEnv *e, jclass c, jmethodID id, const jvalue *a) { invoke_a(NULL, id, a); }
static void j_CallNonvirtualVoidMethodV(JNIEnv *e, jobject o, jclass c, jmethodID id, va_list ap) { invoke_v(o, id, ap); }
static void j_CallNonvirtualVoidMethod(JNIEnv *e, jobject o, jclass c, jmethodID id, ...) { va_list ap; va_start(ap, id); invoke_v(o, id, ap); va_end(ap); }
static void j_CallNonvirtualVoidMethodA(JNIEnv *e, jobject o, jclass c, jmethodID id, const jvalue *a) { invoke_a(o, id, a); }

static jobject j_NewObjectV(JNIEnv *e, jclass c, jmethodID id, va_list ap) { return invoke_v(NULL, id, ap).l; }
static jobject j_NewObject(JNIEnv *e, jclass c, jmethodID id, ...) {
	va_list ap; va_start(ap, id); jobject r = invoke_v(NULL, id, ap).l; va_end(ap); return r;
}
static jobject j_NewObjectA(JNIEnv *e, jclass c, jmethodID id, const jvalue *a) { return invoke_a(NULL, id, a).l; }

static jclass j_FindClass(JNIEnv *env, const char *name) {
	/* FalsoJNI's FindClass strdup()s on every call; intern instead (same semantics: the
	 * class handle is the class name, and GetMethodID("<init>") keys on it). */
	static int logged;
	jclass c = (jclass)intern_class(name ? name : "");
	if (logged++ < 400) aod_log("JNI FindClass %s", name);
	return c;
}

/* ---------------------------------------------------------- install */

void aod_jni_install(void) {
	struct JNINativeInterface *t = (struct JNINativeInterface *)jni;
	falso_DeleteGlobalRef = t->DeleteGlobalRef;
	t->FindClass = j_FindClass;
	t->GetMethodID = j_GetMethodID;
	t->GetStaticMethodID = j_GetStaticMethodID;
	t->GetStaticFieldID = j_GetStaticFieldID;
	t->GetStaticIntField = j_GetStaticIntField;
	t->GetFieldID = j_GetFieldID;
	t->GetIntField = j_GetIntField;
#define SETF(Type) \
	t->Get##Type##Field = j_Get##Type##Field; t->GetStatic##Type##Field = j_GetStatic##Type##Field; \
	t->Set##Type##Field = j_Set##Type##Field; t->SetStatic##Type##Field = j_SetStatic##Type##Field;
	SETF(Object) SETF(Boolean) SETF(Byte) SETF(Char) SETF(Short) SETF(Long) SETF(Float) SETF(Double)
	t->SetIntField = j_SetIntField;
	t->SetStaticIntField = j_SetStaticIntField;
	t->GetObjectClass = j_GetObjectClass;
	t->DeleteGlobalRef = j_DeleteGlobalRef;
	t->IsInstanceOf = j_IsInstanceOf;
	t->NewObject = j_NewObject;
	t->NewObjectV = j_NewObjectV;
	t->NewObjectA = j_NewObjectA;
#define SET(Type) \
	t->Call##Type##Method = j_Call##Type##Method; t->Call##Type##MethodV = j_Call##Type##MethodV; t->Call##Type##MethodA = j_Call##Type##MethodA; \
	t->CallStatic##Type##Method = j_CallStatic##Type##Method; t->CallStatic##Type##MethodV = j_CallStatic##Type##MethodV; t->CallStatic##Type##MethodA = j_CallStatic##Type##MethodA; \
	t->CallNonvirtual##Type##Method = j_CallNonvirtual##Type##Method; t->CallNonvirtual##Type##MethodV = j_CallNonvirtual##Type##MethodV; t->CallNonvirtual##Type##MethodA = j_CallNonvirtual##Type##MethodA;
	SET(Object) SET(Boolean) SET(Byte) SET(Char) SET(Short) SET(Int) SET(Long) SET(Float) SET(Double) SET(Void)
	aod_log("JNI bridge installed: %d methods, %d static ints", N_METHODS, N_STATIC_INTS);
}

void aod_jni_log_stats(void) {
	static const char *names[] = { "sentinel", "ad", "error", "class", "sha1", "sha256", "md5", "text_params", "text_result" };
	char buf[512];
	int n = snprintf(buf, sizeof(buf), "JNI alloc totals: strings=%u", n_strings);
	for (unsigned k = 0; k < sizeof(names) / sizeof(names[0]) && n < (int)sizeof(buf); k++)
		n += snprintf(buf + n, sizeof(buf) - n, " %s=%u", names[k], n_obj_by_kind[k]);
	aod_log("%s", buf);
}

/* test hook */
unsigned aod_jni_alloc_count(void) {
	unsigned t = n_strings;
	for (int k = 0; k < 16; k++) t += n_obj_by_kind[k];
	return t;
}
