/*
 * aod-vita: Android OpenSL ES compatibility over the VitaSDK libOpenSLES. See opensl_compat.h.
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#include "aod/opensl_compat.h"
#include "aod/port.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Android configuration constants. The SDK's OpenSLES_AndroidConfiguration.h defines these only
 * for C++ (its `#ifdef __cplusplus` block is never closed), so they are restated here;
 * tests/opensl_compat_test checks them against that header compiled as C++. */
#define SL_ANDROID_KEY_STREAM_TYPE ((const SLchar *)"androidPlaybackStreamType")
#define SL_ANDROID_STREAM_VOICE ((SLint32)0x00000000)
#define SL_ANDROID_STREAM_MEDIA ((SLint32)0x00000003)
#define SL_ANDROID_STREAM_NOTIFICATION ((SLint32)0x00000005)

#if defined(__arm__)
/* FMOD (armeabi-v7a) passes valueSize 4 for the SLint32 stream type; the SDK's SLint32 is `long`. */
_Static_assert(sizeof(SLint32) == 4 && sizeof(SLuint32) == 4, "OpenSL ES 32-bit types must be 4 bytes on the target");
#endif

static aod_slCreateEngine_fn real_create;
static SLInterfaceID IID_ENGINE, IID_ANDROIDCONFIGURATION;
static aod_opensl_restart_fn restart_output;
static int *output_freq;

void aod_opensl_set_output_hooks(aod_opensl_restart_fn restart, int *user_freq) {
	restart_output = restart;
	output_freq = user_freq;
}

/* sceAudioOutOpenPort accepts these rates for a BGM port. */
int aod_opensl_vita_bgm_rate_ok(SLuint32 hz) {
	static const SLuint32 ok[] = { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 };
	for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++)
		if (ok[i] == hz) return 1;
	return 0;
}

/* The SDK output runs at *output_freq (0 means its default, 44100). Match it to the player's PCM. */
static void follow_player_rate(SLEngineItf sdk_engine, const SLDataSource *src) {
	if (!src || !src->pFormat || !src->pLocator) return;
	SLuint32 locator, fmt_type;
	memcpy(&locator, src->pLocator, sizeof locator);
	memcpy(&fmt_type, src->pFormat, sizeof fmt_type);
	if (fmt_type != SL_DATAFORMAT_PCM) {
		aod_log("opensl: AudioPlayer source format %u is not PCM; output rate unchanged", (unsigned)fmt_type);
		return;
	}
	SLDataFormat_PCM pcm;
	memcpy(&pcm, src->pFormat, sizeof pcm);
	SLuint32 hz = pcm.samplesPerSec / 1000;   /* OpenSL rates are in milliHertz */
	SLuint32 bufs = 0;
	if (locator == SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE) {
		SLDataLocator_AndroidSimpleBufferQueue q;
		memcpy(&q, src->pLocator, sizeof q);
		bufs = q.numBuffers;
	}
	int cur = output_freq && *output_freq > 0 ? *output_freq : 44100;
	aod_log("opensl: AudioPlayer PCM %u Hz, %u ch, %u/%u bit, mask %#x, endian %u, %u buffers; SDK output at %d Hz", (unsigned)hz,
	        (unsigned)pcm.numChannels, (unsigned)pcm.bitsPerSample, (unsigned)pcm.containerSize, (unsigned)pcm.channelMask,
	        (unsigned)pcm.endianness, (unsigned)bufs, cur);
	if (pcm.numChannels != 2 || pcm.bitsPerSample != 16 || pcm.containerSize != 16) {
		aod_log("opensl: the SDK mixer only mixes 16-bit stereo frames; this player's PCM will not play correctly");
		return;
	}
	if ((int)hz == cur) return;
	if (!restart_output || !output_freq || !aod_opensl_vita_bgm_rate_ok(hz)) {
		aod_log("opensl: cannot run the SDK output at %u Hz (%s); pitch will be off by x%.3f", (unsigned)hz,
		        aod_opensl_vita_bgm_rate_ok(hz) ? "no output hook" : "not a Vita BGM port rate", (double)cur / (double)hz);
		return;
	}
	*output_freq = (int)hz;
	restart_output((void *)sdk_engine);   /* SLEngineItf == &IEngine.mItf == the IEngine* SDL_open expects */
	aod_log("opensl: SDK output restarted at %u Hz to match the player (was %d Hz)", (unsigned)hz, cur);
}

void aod_opensl_set_backend(aod_slCreateEngine_fn fn, SLInterfaceID iid_engine, SLInterfaceID iid_cfg) {
	real_create = fn;
	IID_ENGINE = iid_engine;
	IID_ANDROIDCONFIGURATION = iid_cfg;
}

static int iid_eq(SLInterfaceID a, SLInterfaceID b) { return a == b || (a && b && memcmp(a, b, sizeof *a) == 0); }

/* One proxy per wrapped object. The interface pointers handed out point at the vtable-pointer
 * fields below, so each method recovers its proxy with container_of. */
typedef struct {
	const struct SLObjectItf_ *obj_itf;
	const struct SLEngineItf_ *eng_itf;                  /* engine proxy only */
	const struct SLAndroidConfigurationItf_ *cfg_itf;    /* player proxy only */
	SLObjectItf real;
	SLEngineItf real_engine;
	SLint32 stream_type;
	int is_engine;
} proxy;

#define CONTAINER(ptr, field) ((proxy *)((char *)(ptr) - offsetof(proxy, field)))
static proxy *P(SLObjectItf self) { return CONTAINER(self, obj_itf); }
static proxy *PE(SLEngineItf self) { return CONTAINER(self, eng_itf); }
static proxy *PC(SLAndroidConfigurationItf self) { return CONTAINER(self, cfg_itf); }

/* ---- AndroidConfiguration (per player) ---- */
static SLresult cfg_set(SLAndroidConfigurationItf self, const SLchar *key, const void *value, SLuint32 size) {
	proxy *p = PC(self);
	if (!key || strcmp((const char *)key, (const char *)SL_ANDROID_KEY_STREAM_TYPE) != 0) {
		aod_log("opensl: SetConfiguration(\"%s\") on AudioPlayer: unsupported key -> PARAMETER_INVALID", key ? (const char *)key : "(null)");
		return SL_RESULT_PARAMETER_INVALID;
	}
	if (!value || size < sizeof(SLint32))
		return SL_RESULT_PARAMETER_INVALID;
	SLuint32 state = 0;   /* Android: the stream type can only be set before Realize */
	if ((*p->real)->GetState(p->real, &state) != SL_RESULT_SUCCESS || state != SL_OBJECT_STATE_UNREALIZED)
		return SL_RESULT_PRECONDITIONS_VIOLATED;
	SLint32 v;
	memcpy(&v, value, sizeof v);
	if (v < SL_ANDROID_STREAM_VOICE || v > SL_ANDROID_STREAM_NOTIFICATION)
		return SL_RESULT_PARAMETER_INVALID;
	p->stream_type = v;
	aod_log("opensl: AudioPlayer stream type %d set (Vita: all players mix into the SDK library's BGM port)", (int)v);
	return SL_RESULT_SUCCESS;
}

static SLresult cfg_get(SLAndroidConfigurationItf self, const SLchar *key, SLuint32 *pSize, void *value) {
	proxy *p = PC(self);
	if (!key || !pSize || strcmp((const char *)key, (const char *)SL_ANDROID_KEY_STREAM_TYPE) != 0)
		return SL_RESULT_PARAMETER_INVALID;
	if (!value) {                         /* size query */
		*pSize = sizeof(SLint32);
		return SL_RESULT_SUCCESS;
	}
	if (*pSize < sizeof(SLint32))
		return SL_RESULT_PARAMETER_INVALID;
	memcpy(value, &p->stream_type, sizeof(SLint32));
	*pSize = sizeof(SLint32);
	return SL_RESULT_SUCCESS;
}

static const struct SLAndroidConfigurationItf_ cfg_vtbl = { cfg_set, cfg_get };

/* ---- object proxy: everything forwarded to the real object ---- */
static SLresult o_Realize(SLObjectItf self, SLboolean async) { proxy *p = P(self); return (*p->real)->Realize(p->real, async); }
static SLresult o_Resume(SLObjectItf self, SLboolean async) { proxy *p = P(self); return (*p->real)->Resume(p->real, async); }
static SLresult o_GetState(SLObjectItf self, SLuint32 *st) { proxy *p = P(self); return (*p->real)->GetState(p->real, st); }
static SLresult o_GetInterface(SLObjectItf self, const SLInterfaceID iid, void *pItf) {
	proxy *p = P(self);
	if (!p->is_engine && iid_eq(iid, IID_ANDROIDCONFIGURATION)) {
		if (!pItf) return SL_RESULT_PARAMETER_INVALID;
		*(SLAndroidConfigurationItf *)pItf = &p->cfg_itf;
		return SL_RESULT_SUCCESS;
	}
	if (p->is_engine && iid_eq(iid, IID_ENGINE)) {
		if (!pItf) return SL_RESULT_PARAMETER_INVALID;
		SLEngineItf e = NULL;   /* the SDK writes NULL on failure: never into the live proxy */
		SLresult r = (*p->real)->GetInterface(p->real, iid, &e);
		if (r == SL_RESULT_SUCCESS) {
			p->real_engine = e;
			*(SLEngineItf *)pItf = &p->eng_itf;
		}
		return r;
	}
	return (*p->real)->GetInterface(p->real, iid, pItf);
}
/* The object callback receives the underlying SDK object as `caller`. FMOD does not use it: its only
 * RegisterCallback is the buffer queue's (libfmodex 0xafdf8: vtable +12 of the SDK's own
 * SLAndroidSimpleBufferQueueItf, obtained at 0xafdd0), so its `caller` is the interface FMOD holds. */
static SLresult o_RegisterCallback(SLObjectItf self, slObjectCallback cb, void *ctx) {
	proxy *p = P(self);
	return (*p->real)->RegisterCallback(p->real, cb, ctx);
}
static void o_AbortAsyncOperation(SLObjectItf self) { proxy *p = P(self); (*p->real)->AbortAsyncOperation(p->real); }
static void o_Destroy(SLObjectItf self) { proxy *p = P(self); (*p->real)->Destroy(p->real); free(p); }
static SLresult o_SetPriority(SLObjectItf self, SLint32 prio, SLboolean pre) { proxy *p = P(self); return (*p->real)->SetPriority(p->real, prio, pre); }
static SLresult o_GetPriority(SLObjectItf self, SLint32 *prio, SLboolean *pre) { proxy *p = P(self); return (*p->real)->GetPriority(p->real, prio, pre); }
static SLresult o_SetLossOfControlInterfaces(SLObjectItf self, SLint16 n, SLInterfaceID *ids, SLboolean enabled) {
	proxy *p = P(self);
	return (*p->real)->SetLossOfControlInterfaces(p->real, n, ids, enabled);
}
static const struct SLObjectItf_ obj_vtbl = {
	o_Realize, o_Resume, o_GetState, o_GetInterface, o_RegisterCallback, o_AbortAsyncOperation, o_Destroy,
	o_SetPriority, o_GetPriority, o_SetLossOfControlInterfaces,
};

static proxy *new_proxy(SLObjectItf real, int is_engine) {
	proxy *p = calloc(1, sizeof *p);
	if (!p) return NULL;
	p->obj_itf = &obj_vtbl;
	p->real = real;
	p->is_engine = is_engine;
	p->stream_type = SL_ANDROID_STREAM_MEDIA;   /* Android's default for players */
	if (!is_engine) p->cfg_itf = &cfg_vtbl;
	return p;
}

/* ---- engine interface proxy: forwards, except CreateAudioPlayer ---- */
#define RE(self) (PE(self)->real_engine)
static SLresult e_CreateAudioPlayer(SLEngineItf self, SLObjectItf *pPlayer, SLDataSource *src, SLDataSink *snk, SLuint32 n,
                                    const SLInterfaceID *ids, const SLboolean *req) {
	SLEngineItf re = RE(self);
	SLuint32 cfg_at = n;
	for (SLuint32 i = 0; ids && i < n; i++)
		if (iid_eq(ids[i], IID_ANDROIDCONFIGURATION)) { cfg_at = i; break; }
	if (cfg_at == n || n > AOD_OPENSL_MAX_INTERFACES || !pPlayer)   /* not requested (or unusual): SDK behaviour */
		return (*re)->CreateAudioPlayer(re, pPlayer, src, snk, n, ids, req);
	SLInterfaceID ids2[AOD_OPENSL_MAX_INTERFACES];
	SLboolean req2[AOD_OPENSL_MAX_INTERFACES];
	SLuint32 m = 0;
	for (SLuint32 i = 0; i < n; i++) {
		if (iid_eq(ids[i], IID_ANDROIDCONFIGURATION)) continue;   /* served by the port */
		ids2[m] = ids[i];
		req2[m] = req ? req[i] : SL_BOOLEAN_FALSE;
		m++;
	}
	SLObjectItf real = NULL;
	SLresult r = (*re)->CreateAudioPlayer(re, &real, src, snk, m, ids2, req ? req2 : NULL);
	if (r != SL_RESULT_SUCCESS) {
		aod_log("opensl: CreateAudioPlayer (ANDROIDCONFIGURATION served by the port) -> %#x from the SDK", (unsigned)r);
		return r;
	}
	proxy *p = new_proxy(real, 0);
	if (!p) {
		(*real)->Destroy(real);
		return SL_RESULT_MEMORY_FAILURE;
	}
	*pPlayer = &p->obj_itf;
	aod_log("opensl: AudioPlayer created; ANDROIDCONFIGURATION provided by the port (%u of %u interfaces passed to the SDK)",
	        (unsigned)m, (unsigned)n);
	follow_player_rate(re, src);
	return SL_RESULT_SUCCESS;
}
static SLresult e_CreateLEDDevice(SLEngineItf s, SLObjectItf *o, SLuint32 id, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req) {
	return (*RE(s))->CreateLEDDevice(RE(s), o, id, n, ids, req);
}
static SLresult e_CreateVibraDevice(SLEngineItf s, SLObjectItf *o, SLuint32 id, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req) {
	return (*RE(s))->CreateVibraDevice(RE(s), o, id, n, ids, req);
}
static SLresult e_CreateAudioRecorder(SLEngineItf s, SLObjectItf *o, SLDataSource *src, SLDataSink *snk, SLuint32 n, const SLInterfaceID *ids,
                                      const SLboolean *req) {
	return (*RE(s))->CreateAudioRecorder(RE(s), o, src, snk, n, ids, req);
}
static SLresult e_CreateMidiPlayer(SLEngineItf s, SLObjectItf *o, SLDataSource *a, SLDataSource *b, SLDataSink *c, SLDataSink *d, SLDataSink *e,
                                   SLuint32 n, const SLInterfaceID *ids, const SLboolean *req) {
	return (*RE(s))->CreateMidiPlayer(RE(s), o, a, b, c, d, e, n, ids, req);
}
static SLresult e_CreateListener(SLEngineItf s, SLObjectItf *o, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req) {
	return (*RE(s))->CreateListener(RE(s), o, n, ids, req);
}
static SLresult e_Create3DGroup(SLEngineItf s, SLObjectItf *o, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req) {
	return (*RE(s))->Create3DGroup(RE(s), o, n, ids, req);
}
static SLresult e_CreateOutputMix(SLEngineItf s, SLObjectItf *o, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req) {
	return (*RE(s))->CreateOutputMix(RE(s), o, n, ids, req);
}
static SLresult e_CreateMetadataExtractor(SLEngineItf s, SLObjectItf *o, SLDataSource *src, SLuint32 n, const SLInterfaceID *ids,
                                          const SLboolean *req) {
	return (*RE(s))->CreateMetadataExtractor(RE(s), o, src, n, ids, req);
}
static SLresult e_CreateExtensionObject(SLEngineItf s, SLObjectItf *o, void *params, SLuint32 id, SLuint32 n, const SLInterfaceID *ids,
                                        const SLboolean *req) {
	return (*RE(s))->CreateExtensionObject(RE(s), o, params, id, n, ids, req);
}
static SLresult e_QueryNumSupportedInterfaces(SLEngineItf s, SLuint32 id, SLuint32 *n) {
	return (*RE(s))->QueryNumSupportedInterfaces(RE(s), id, n);
}
static SLresult e_QuerySupportedInterfaces(SLEngineItf s, SLuint32 id, SLuint32 i, SLInterfaceID *iid) {
	return (*RE(s))->QuerySupportedInterfaces(RE(s), id, i, iid);
}
static SLresult e_QueryNumSupportedExtensions(SLEngineItf s, SLuint32 *n) { return (*RE(s))->QueryNumSupportedExtensions(RE(s), n); }
static SLresult e_QuerySupportedExtension(SLEngineItf s, SLuint32 i, SLchar *name, SLint16 *len) {
	return (*RE(s))->QuerySupportedExtension(RE(s), i, name, len);
}
static SLresult e_IsExtensionSupported(SLEngineItf s, const SLchar *name, SLboolean *sup) {
	return (*RE(s))->IsExtensionSupported(RE(s), name, sup);
}
static const struct SLEngineItf_ eng_vtbl = {
	e_CreateLEDDevice, e_CreateVibraDevice, e_CreateAudioPlayer, e_CreateAudioRecorder, e_CreateMidiPlayer, e_CreateListener,
	e_Create3DGroup, e_CreateOutputMix, e_CreateMetadataExtractor, e_CreateExtensionObject, e_QueryNumSupportedInterfaces,
	e_QuerySupportedInterfaces, e_QueryNumSupportedExtensions, e_QuerySupportedExtension, e_IsExtensionSupported,
};

SLresult aod_slCreateEngine(SLObjectItf *pEngine, SLuint32 numOptions, const SLEngineOption *opts, SLuint32 n,
                            const SLInterfaceID *ids, const SLboolean *req) {
	if (!real_create || !pEngine) return SL_RESULT_PARAMETER_INVALID;
	SLObjectItf real = NULL;
	SLresult r = real_create(&real, numOptions, opts, n, ids, req);
	if (r != SL_RESULT_SUCCESS) return r;
	proxy *p = new_proxy(real, 1);
	if (!p) {
		(*real)->Destroy(real);
		return SL_RESULT_MEMORY_FAILURE;
	}
	p->eng_itf = &eng_vtbl;
	*pEngine = &p->obj_itf;
	return SL_RESULT_SUCCESS;
}
