/*
 * Host test for source/aod/jni_bridge.c: drives the patched FalsoJNI JNIEnv the way libgame's
 * CallContext does (FindClass / Get*MethodID with runtime signatures / Call*Method varargs /
 * NewObject / static int fields) and checks results. Also checks that deferred replies reach
 * the exported native callbacks with the Java failure-path arguments.
 */
#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_ImplBridge.h>
#include "aod/jni_bridge.h"
#include "aod/port.h"
unsigned aod_jni_alloc_count(void);

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- port stubs */
static int n_missing_logs;
void aod_log(const char *fmt, ...) {
	char buf[1024];
	va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
	if (strstr(buf, "JNI MISSING")) n_missing_logs++;
	if (getenv("VERBOSE")) printf("%s\n", buf);
}
static int exit_req;
void aod_request_exit(void) { exit_req = 1; }
int aod_prefs_get_int(const char *k, int d) { return d; }
const char *aod_write_path(void) { return "ux0:data/aodd/files"; }
const char *aod_device_model(void) { return "PlayStation Vita"; }
const char *aod_device_id(void) { return "5ce0a0dd00000001"; }
const char *aod_language(void) { return "en"; }
const char *aod_country(void) { return "US"; }
int64_t aod_free_space_bytes(void) { return 1234567890123LL; }
double aod_uptime_seconds(void) { return 42.5; }
static int alert_host_ok = 1, alert_n = -1; static char alert_labels[3][64];
int aod_alert_begin(const char *t, const char *m, const char *const *labels, int n, int64_t h) {
	if (!alert_host_ok) return -1;
	alert_n = n;
	for (int i = 0; i < n; i++) snprintf(alert_labels[i], 64, "%s", labels[i]);
	return 0;
}

/* ---- fake native exports receiving replies */
static int got_billing, got_billing_code; static jlong got_billing_handle;
static int got_purchases, purchases_before_return = -1, got_purchases_code; static jsize got_purchases_len = -1;
static void nativeGetPurchasesFinished(JNIEnv *e, jclass c, jobject err, jobjectArray arr, jlong h) {
	got_purchases++;
	got_purchases_len = arr ? (*e)->GetArrayLength(e, arr) : -1;
	got_purchases_code = err ? (*e)->CallIntMethod(e, err, (*e)->GetMethodID(e, (*e)->GetObjectClass(e, err), "getCode", "()I")) : 0;
}
static jobject billing_iap_inst;
static void nativeBillingServiceAvailable(JNIEnv *e, jclass c, jobject err, jlong h) {
	got_billing++; got_billing_handle = h;
	/* boot-01: native reacts to "Billing Unavailable" by calling IAP.getPurchases from inside this
	 * callback; Java delivers nativeGetPurchasesFinished before getPurchases returns. */
	if (billing_iap_inst) {
		int before = got_purchases;
		(*e)->CallVoidMethod(e, billing_iap_inst, (*e)->GetMethodID(e, (*e)->GetObjectClass(e, billing_iap_inst), "getPurchases", "(J)V"), (jlong)2);
		purchases_before_return = got_purchases - before;
	}
	jmethodID m = (*e)->GetMethodID(e, (*e)->GetObjectClass(e, err), "getCode", "()I");
	got_billing_code = (*e)->CallIntMethod(e, err, m);
}
static int got_adv; static jlong got_adv_handle; static jboolean got_adv_limit = 99; static char got_adv_domain[64];
static void nativeAdvertisingInfoCallback(JNIEnv *e, jclass c, jstring id, jboolean lim, jobject err, jlong h) {
	got_adv++; got_adv_handle = h; got_adv_limit = lim;
	jmethodID m = (*e)->GetMethodID(e, (*e)->FindClass(e, "com/backflipstudios/bf_core/error/PlatformError"), "getDomain", "()Ljava/lang/String;");
	jstring s = (*e)->CallObjectMethod(e, err, m);
	snprintf(got_adv_domain, sizeof(got_adv_domain), "%s", (*e)->GetStringUTFChars(e, s, NULL));
}
static int got_products; static jsize got_products_len = -1;
static void nativeGetProductsFinished(JNIEnv *e, jclass c, jobject err, jobjectArray arr, jlong h) {
	got_products++; got_products_len = arr ? (*e)->GetArrayLength(e, arr) : -1;
}
static int got_alert, got_alert_which; static jobject got_alert_err;
static void nativeAlertDismissedCallback(JNIEnv *e, jclass c, jint which, jobject err, jlong h) {
	got_alert++; got_alert_which = which; got_alert_err = err;
}
void *aod_game_symbol(const char *name) {
	if (strstr(name, "nativeGetPurchasesFinished")) return nativeGetPurchasesFinished;
	if (strstr(name, "nativeAlertDismissedCallback")) return nativeAlertDismissedCallback;
	if (strstr(name, "nativeBillingServiceAvailable")) return nativeBillingServiceAvailable;
	if (strstr(name, "nativeAdvertisingInfoCallback")) return nativeAdvertisingInfoCallback;
	if (strstr(name, "nativeGetProductsFinished")) return nativeGetProductsFinished;
	return NULL;
}

#define BF "com/backflipstudios/"

int main(void) {
	jni_init();
	aod_jni_install();
	JNIEnv *e = &jni;

	/* JavaVM path used by NativeBridge (cached VM) */
	JNIEnv *env2 = NULL;
	CHECK((*(&jvm))->GetEnv(&jvm, (void **)&env2, JNI_VERSION_1_6) == JNI_OK && env2 == e, "GetEnv");

	/* static String getter */
	jclass inf = (*e)->FindClass(e, BF "bf_core/information/Information");
	jmethodID m = (*e)->GetStaticMethodID(e, inf, "getApplicationRootWritePath", "()Ljava/lang/String;");
	CHECK(m != NULL, "getApplicationRootWritePath id");
	jstring s = (*e)->CallStaticObjectMethod(e, inf, m);
	CHECK(s && strcmp((*e)->GetStringUTFChars(e, s, NULL), "ux0:data/aodd/files") == 0, "root write path");
	m = (*e)->GetStaticMethodID(e, inf, "getOpenGLContextVersion", "()Ljava/lang/String;");
	CHECK(strcmp((*e)->GetStringUTFChars(e, (*e)->CallStaticObjectMethod(e, inf, m), NULL), "2.0") == 0, "gl version");
	m = (*e)->GetStaticMethodID(e, inf, "getAvailableDiskSpaceBytes", "()J");
	CHECK((*e)->CallStaticLongMethod(e, inf, m) == 1234567890123LL, "disk space jlong");
	m = (*e)->GetStaticMethodID(e, inf, "getDisplayScale", "()F");
	CHECK((*e)->CallStaticFloatMethod(e, inf, m) == 1.0f, "display scale float");
	m = (*e)->GetStaticMethodID(e, inf, "getSecondsSinceLastBoot", "()D");
	CHECK((*e)->CallStaticDoubleMethod(e, inf, m) == 42.5, "uptime double");
	m = (*e)->GetStaticMethodID(e, inf, "isConnectedToNetwork", "()Z");
	CHECK((*e)->CallStaticBooleanMethod(e, inf, m) == JNI_FALSE, "offline");
	m = (*e)->GetStaticMethodID(e, inf, "getUsableDisplayWidth", "()I");
	CHECK((*e)->CallStaticIntMethod(e, inf, m) == 960, "usable width");

	/* unknown method -> NULL id, never a crash */
	CHECK((*e)->GetStaticMethodID(e, inf, "noSuchMethod", "()V") == NULL, "missing -> NULL");

	/* static ints: same name, different classes */
	jclass uir = (*e)->FindClass(e, BF "bf_ui/UIResponse");
	jclass infr = (*e)->FindClass(e, BF "bf_core/information/InformationResponse");
	jfieldID f1 = (*e)->GetStaticFieldID(e, uir, "REQUEST_IN_FLIGHT", "I");
	jfieldID f2 = (*e)->GetStaticFieldID(e, infr, "REQUEST_IN_FLIGHT", "I");
	CHECK(f1 && f2 && (*e)->GetStaticIntField(e, uir, f1) == 1 && (*e)->GetStaticIntField(e, infr, f2) == 0, "REQUEST_IN_FLIGHT per class");
	jclass pa = (*e)->FindClass(e, BF "bf_ui/PlatformAlert");
	CHECK((*e)->GetStaticIntField(e, pa, (*e)->GetStaticFieldID(e, pa, "NEGATIVE", "I")) == -2, "PlatformAlert.NEGATIVE");

	/* PlatformError via NewObject varargs + getters through GetObjectClass */
	jclass pec = (*e)->FindClass(e, BF "bf_core/error/PlatformError");
	jmethodID ctor = (*e)->GetMethodID(e, pec, "<init>", "(Ljava/lang/String;Ljava/lang/String;I)V");
	jobject pe = (*e)->NewObject(e, pec, ctor, (*e)->NewStringUTF(e, "dom"), (*e)->NewStringUTF(e, "msg"), 7);
	jclass pe_cls = (*e)->GetObjectClass(e, pe);
	CHECK((*e)->CallIntMethod(e, pe, (*e)->GetMethodID(e, pe_cls, "getCode", "()I")) == 7, "PlatformError code");
	jstring ts = (*e)->CallObjectMethod(e, pe, (*e)->GetMethodID(e, pe_cls, "toString", "()Ljava/lang/String;"));
	CHECK(strcmp((*e)->GetStringUTFChars(e, ts, NULL), "dom(code:7) - msg") == 0, "toString: %s", (*e)->GetStringUTFChars(e, ts, NULL));
	/* getClass().getName() as CallContext::objectArrayArg does */
	jobject klass = (*e)->CallObjectMethod(e, pe, (*e)->GetMethodID(e, pe_cls, "getClass", "()Ljava/lang/Class;"));
	jstring nm = (*e)->CallObjectMethod(e, klass, (*e)->GetMethodID(e, (*e)->GetObjectClass(e, klass), "getName", "()Ljava/lang/String;"));
	CHECK(strcmp((*e)->GetStringUTFChars(e, nm, NULL), "com.backflipstudios.bf_core.error.PlatformError") == 0, "getName");

	/* Digests: SHA256 over "abc" via update([BII) with offset, MD5/SHA1 via update([B) */
	const char *payload = "xxabcyy";
	jbyteArray arr = (*e)->NewByteArray(e, 7);
	(*e)->SetByteArrayRegion(e, arr, 0, 7, (const jbyte *)payload);
	struct { const char *cls, *hex; int len; int ranged; } dg[] = {
		{ BF "bf_core/security/SHA256", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32, 1 },
		{ BF "bf_core/security/SHA1", "a9993e364706816aba3e25717850c26c9cd0d89d", 20, 1 },
		{ BF "bf_core/security/MD5", "900150983cd24fb0d6963f7d28e17f72", 16, 1 },
	};
	for (int i = 0; i < 3; i++) {
		jclass c = (*e)->FindClass(e, dg[i].cls);
		CHECK((*e)->CallStaticIntMethod(e, c, (*e)->GetStaticMethodID(e, c, "getDigestLength", "()I")) == dg[i].len, "len %s", dg[i].cls);
		jobject h = (*e)->NewObject(e, c, (*e)->GetMethodID(e, c, "<init>", "()V"));
		jclass hc = (*e)->GetObjectClass(e, h);
		(*e)->CallVoidMethod(e, h, (*e)->GetMethodID(e, hc, "update", "([BII)V"), arr, 2, 3);
		jbyteArray out = (*e)->CallObjectMethod(e, h, (*e)->GetMethodID(e, hc, "finish", "()[B"));
		CHECK(out && (*e)->GetArrayLength(e, out) == dg[i].len, "digest len");
		char hex[65] = "";
		jbyte *b = (*e)->GetByteArrayElements(e, out, NULL);
		for (int k = 0; k < dg[i].len; k++) sprintf(hex + 2 * k, "%02x", (unsigned char)b[k]);
		CHECK(strcmp(hex, dg[i].hex) == 0, "%s(abc) = %s", dg[i].cls, hex);
	}

	/* IAP with no billing service follows Java's inline ordering (boot-01 regression):
	 * init -> nativeBillingServiceAvailable(err 3) before init returns; the callback's
	 * getPurchases -> nativeGetPurchasesFinished(err 3, empty array) before getPurchases returns. */
	jclass iap = (*e)->FindClass(e, BF "bf_iap/google/IAP");
	jobject inst = (*e)->CallStaticObjectMethod(e, iap, (*e)->GetStaticMethodID(e, iap, "getInstance", "()Lcom/backflipstudios/bf_iap/google/IAP;"));
	CHECK(inst != NULL, "IAP instance");
	billing_iap_inst = inst;
	(*e)->CallVoidMethod(e, inst, (*e)->GetMethodID(e, (*e)->GetObjectClass(e, inst), "init", "(J)V"), (jlong)0x1122334455667788LL);
	CHECK(got_billing == 1 && got_billing_code == 3 && got_billing_handle == 0x1122334455667788LL, "billing reply inline %d code %d", got_billing, got_billing_code);
	CHECK(purchases_before_return == 1, "getPurchases reply delivered before return (re-entrant, boot-01)");
	CHECK(got_purchases_code == 3 && got_purchases_len == 0, "getPurchases: Billing Unavailable + empty array (code %d len %d)", got_purchases_code, (int)got_purchases_len);
	billing_iap_inst = NULL;
	(*e)->CallVoidMethod(e, inst, (*e)->GetMethodID(e, (*e)->GetObjectClass(e, inst), "getProducts", "([Ljava/lang/String;[Ljava/lang/String;J)V"), NULL, NULL, (jlong)5);
	CHECK(got_products == 0, "getProducts stays asynchronous (Java worker thread)");
	jobject info = (*e)->NewObject(e, inf, (*e)->GetMethodID(e, inf, "<init>", "()V"));
	(*e)->CallVoidMethod(e, info, (*e)->GetMethodID(e, (*e)->GetObjectClass(e, info), "getAdvertisingInfo", "(J)V"), (jlong)77);
	CHECK(got_adv == 1 && got_adv_handle == 77 && got_adv_limit == JNI_FALSE && strcmp(got_adv_domain, "information") == 0, "advertising reply inline");
	aod_jni_run_deferred();
	CHECK(got_products == 1 && got_products_len == 0, "products reply with empty array");
	CHECK(got_billing == 1 && got_purchases == 1 && got_adv == 1, "no duplicate replies after drain");

	/* boot-01: NotificationManager private ctor; SHA256.update([B)V must not log a false MISSING */
	{
		jclass nm = (*e)->FindClass(e, BF "bf_notification/NotificationManager");
		CHECK((*e)->NewObject(e, nm, (*e)->GetMethodID(e, nm, "<init>", "()V")) != NULL, "NotificationManager.<init>()V");
		int before = n_missing_logs;
		CHECK((*e)->GetMethodID(e, (*e)->FindClass(e, BF "bf_core/security/SHA256"), "update", "([B)V") != NULL, "SHA256.update([B)V resolves");
		CHECK(n_missing_logs == before, "no false JNI MISSING for an exact overload");
	}

	/* HttpConnection.execute returns an error (never NULL = success) */
	jclass hcl = (*e)->FindClass(e, BF "bf_http/HttpClient");
	jobject client = (*e)->NewObject(e, hcl, (*e)->GetMethodID(e, hcl, "<init>", "()V"));
	jobject conn = (*e)->CallObjectMethod(e, client, (*e)->GetMethodID(e, hcl, "newConnection", "(Ljava/lang/String;)Lcom/backflipstudios/bf_http/HttpConnection;"), (*e)->NewStringUTF(e, "http://x"));
	jobject herr = (*e)->CallObjectMethod(e, conn, (*e)->GetMethodID(e, (*e)->GetObjectClass(e, conn), "execute", "(Ljava/lang/String;Ljava/lang/String;III)Lcom/backflipstudios/bf_core/error/PlatformError;"),
	                                      (*e)->NewStringUTF(e, "GET"), (*e)->NewStringUTF(e, "http"), 11, 12, 13);
	CHECK(herr && (*e)->CallIntMethod(e, herr, (*e)->GetMethodID(e, pec, "getCode", "()I")) == 11, "http noConnectionCode");

	/* TextRenderingParams ctor: floats through varargs are promoted to double */
	jclass trp = (*e)->FindClass(e, BF "bf_ui/TextRenderingParams");
	jobject params = (*e)->NewObject(e, trp, (*e)->GetMethodID(e, trp, "<init>", "(Ljava/lang/String;FFFFFFIIII)V"),
	                                 (*e)->NewStringUTF(e, "Hello Ash, hail to the king baby"), 200.0, 30.0, 14.0, 0.1, 1.0, 1.0, 2, (jint)0xFFFFFFFF, (jint)0xFF000000, 1);
	jclass trc = (*e)->FindClass(e, BF "bf_ui/TextRendering");
	jobject res = (*e)->CallStaticObjectMethod(e, trc, (*e)->GetStaticMethodID(e, trc, "prepareToRenderText",
	                                            "(Lcom/backflipstudios/bf_ui/TextRenderingParams;)Lcom/backflipstudios/bf_ui/TextRenderPrepareResult;"), params);
	jclass rc = (*e)->GetObjectClass(e, res);
	int iw = (*e)->CallIntMethod(e, res, (*e)->GetMethodID(e, rc, "getImageWidth", "()I"));
	int ih = (*e)->CallIntMethod(e, res, (*e)->GetMethodID(e, rc, "getImageHeight", "()I"));
	int bpp = (*e)->CallIntMethod(e, res, (*e)->GetMethodID(e, rc, "getBitsPerPixel", "()I"));
	int hnd = (*e)->CallIntMethod(e, res, (*e)->GetMethodID(e, rc, "getRenderHandle", "()I"));
	printf("text prepare: %dx%d bpp %d handle %d\n", iw, ih, bpp, hnd);
	if (getenv("AOD_FONTS")) {
		CHECK(iw == 200 && bpp == 32, "prepare size");
		jbyteArray px = (*e)->CallStaticObjectMethod(e, trc, (*e)->GetStaticMethodID(e, trc, "renderText", "(I)[B"), hnd);
		CHECK(px && (*e)->GetArrayLength(e, px) == iw * ih * 4, "render buffer size");
		if (px && ih > 0) {
			jbyte *b = (*e)->GetByteArrayElements(e, px, NULL);
			long opaque = 0;
			FILE *o = fopen("/w/tools/aod_text.pam", "wb");
			fprintf(o, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", iw, ih);
			for (int y = ih - 1; y >= 0; y--) fwrite(b + (size_t)y * iw * 4, 1, iw * 4, o);   /* un-flip for viewing */
			fclose(o);
			for (int k = 0; k < iw * ih; k++) opaque += (unsigned char)b[4 * k + 3] > 128;
			CHECK(opaque > 200, "text has ink (%ld opaque px)", opaque);
		}
	}

	/* REVIEW-CODE F1: unknown/NULL field ids never yield bogus values */
	CHECK((*e)->GetObjectField(e, pe, NULL) == NULL, "F1 GetObjectField(NULL id)");
	CHECK((*e)->GetStaticObjectField(e, pec, (jfieldID)0x1234) == NULL, "F1 GetStaticObjectField(bogus)");
	CHECK((*e)->GetIntField(e, pe, NULL) == 0, "F1 GetIntField(NULL id)");
	CHECK((*e)->GetByteField(e, pe, NULL) == 0, "F1 GetByteField");
	CHECK((*e)->GetStaticBooleanField(e, pec, NULL) == JNI_FALSE, "F1 GetStaticBooleanField");
	CHECK((*e)->GetStaticLongField(e, pec, NULL) == 0, "F1 GetStaticLongField");
	CHECK((*e)->GetFieldID(e, pec, "m_code", "I") == NULL, "F1 GetFieldID miss");
	CHECK((*e)->GetStaticFieldID(e, uir, "NO_SUCH", "I") == NULL, "F1 GetStaticFieldID miss");
	(*e)->SetStaticIntField(e, uir, f1, 99);
	CHECK((*e)->GetStaticIntField(e, uir, f1) == 1, "F1 static int not writable");

	/* REVIEW-CODE F2: no name-only match with different parameters */
	jclass sha1c = (*e)->FindClass(e, BF "bf_core/security/SHA1");
	CHECK((*e)->GetMethodID(e, sha1c, "update", "(I)V") == NULL, "F2 update(I) rejected");
	CHECK((*e)->GetStaticMethodID(e, inf, "getDisplayWidth", "(J)I") == NULL, "F2 param mismatch rejected");
	CHECK((*e)->GetStaticMethodID(e, inf, "getDisplayWidth", "()J") != NULL, "F2 same params, other return accepted");
	CHECK((*e)->GetMethodID(e, pec, "toString", "(I)Ljava/lang/String;") == NULL, "F2 wildcard requires params");

	/* REVIEW-CODE F5: class handles are interned, stable, and never freed */
	jclass c1 = (*e)->FindClass(e, BF "bf_ui/UIResponse");
	CHECK(c1 == uir, "F5 FindClass interned");
	CHECK((*e)->GetObjectClass(e, pe) == (*e)->GetObjectClass(e, pe), "F5 GetObjectClass interned");
	(*e)->DeleteGlobalRef(e, (*e)->NewGlobalRef(e, c1));
	CHECK(strcmp((const char *)(*e)->FindClass(e, BF "bf_ui/UIResponse"), BF "bf_ui/UIResponse") == 0, "F5 DeleteGlobalRef keeps interned");
	jmethodID gc = (*e)->GetMethodID(e, pec, "getClass", "()Ljava/lang/Class;");
	unsigned before = aod_jni_alloc_count();
	jobject k1 = NULL, k2 = NULL;
	for (int i = 0; i < 1000; i++) {
		(*e)->FindClass(e, BF "bf_core/error/PlatformError");
		(*e)->GetObjectClass(e, pe);
		k2 = (*e)->CallObjectMethod(e, pe, gc);
		if (!k1) k1 = k2;
		(*e)->GetStaticIntField(e, uir, f1);
	}
	CHECK(k1 == k2 && aod_jni_alloc_count() == before, "F5 no per-call allocation (%u -> %u)", before, aod_jni_alloc_count());

	/* REVIEW-CODE F6: referenced no-ops are registered */
	jclass nmc = (*e)->FindClass(e, BF "bf_notification/NotificationManager");
	CHECK((*e)->GetStaticMethodID(e, nmc, "cancel", "(I)V") != NULL, "F6 cancel");
	CHECK((*e)->GetStaticMethodID(e, nmc, "consumeAllReceivedLocalNotifications", "()V") != NULL, "F6 local");
	CHECK((*e)->GetStaticMethodID(e, nmc, "consumeAllReceivedRemoteNotifications", "()V") != NULL, "F6 remote");
	CHECK((*e)->GetMethodID(e, (*e)->FindClass(e, BF "bf_ads/google_mobile_ads/InterstitialAd"), "releaseInterstitial", "()V") != NULL, "F6 releaseInterstitial");

	/* REVIEW-CODE F3: PlatformAlert buttons follow non-empty labels */
	{
		jmethodID show = (*e)->GetStaticMethodID(e, pa, "show",
			"(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ZJ)V");
		jstring E = (*e)->NewStringUTF(e, "");
		/* OK only: yes="OK", no="", neutral="" */
		(*e)->CallStaticVoidMethod(e, pa, show, (*e)->NewStringUTF(e, "T"), (*e)->NewStringUTF(e, "M"),
		                           (*e)->NewStringUTF(e, "OK"), E, E, JNI_FALSE, (jlong)101);
		CHECK(alert_n == 1 && strcmp(alert_labels[0], "OK") == 0, "F3 one button");
		aod_jni_alert_result(0, 101); aod_jni_run_deferred();
		CHECK(got_alert == 1 && got_alert_which == -1 && got_alert_err == NULL, "F3 OK -> POSITIVE");
		/* no + neutral only (yes empty): second button is NEUTRAL (-3) */
		(*e)->CallStaticVoidMethod(e, pa, show, (*e)->NewStringUTF(e, "T"), (*e)->NewStringUTF(e, "M"),
		                           E, (*e)->NewStringUTF(e, "Later"), (*e)->NewStringUTF(e, "Never"), JNI_TRUE, (jlong)102);
		CHECK(alert_n == 2 && strcmp(alert_labels[0], "Later") == 0 && strcmp(alert_labels[1], "Never") == 0, "F3 two buttons");
		aod_jni_alert_result(1, 102); aod_jni_run_deferred();
		CHECK(got_alert == 2 && got_alert_which == -3 && got_alert_err == NULL, "F3 index1 -> NEUTRAL");
		/* cancelable closed without a button -> -1 + UIResponse CANCELED */
		(*e)->CallStaticVoidMethod(e, pa, show, (*e)->NewStringUTF(e, "T"), (*e)->NewStringUTF(e, "M"),
		                           (*e)->NewStringUTF(e, "Y"), (*e)->NewStringUTF(e, "N"), E, JNI_TRUE, (jlong)103);
		aod_jni_alert_result(-1, 103); aod_jni_run_deferred();
		CHECK(got_alert == 3 && got_alert_which == -1 && got_alert_err &&
		      (*e)->CallIntMethod(e, got_alert_err, (*e)->GetMethodID(e, pec, "getCode", "()I")) == 2, "F3 cancel");
		/* stale/duplicate result is ignored */
		aod_jni_alert_result(0, 103); aod_jni_run_deferred();
		CHECK(got_alert == 3, "F3 duplicate result ignored");
		/* no dialog host -> Java 'main activity is null' path, code 3 */
		alert_host_ok = 0;
		(*e)->CallStaticVoidMethod(e, pa, show, (*e)->NewStringUTF(e, "T"), (*e)->NewStringUTF(e, "M"),
		                           (*e)->NewStringUTF(e, "OK"), E, E, JNI_FALSE, (jlong)104);
		aod_jni_run_deferred();
		CHECK(got_alert == 4 && got_alert_which == -1 && got_alert_err &&
		      (*e)->CallIntMethod(e, got_alert_err, (*e)->GetMethodID(e, pec, "getCode", "()I")) == 3, "F3 no host");
		alert_host_ok = 1;
	}

	/* ApplicationContext.exit */
	jclass ac = (*e)->FindClass(e, BF "bf_core/application/ApplicationContext");
	(*e)->CallStaticVoidMethod(e, ac, (*e)->GetStaticMethodID(e, ac, "exit", "()V"));
	CHECK(exit_req == 1, "exit");

	printf("%d/%d checks passed\n", checks - failures, checks);
	return failures != 0;
}
