/* Host test for source/aod/opensl_compat.c against a mock of the VitaSDK libOpenSLES whose
 * CreateAudioPlayer rejects a required SL_IID_ANDROIDCONFIGURATION exactly as the SDK's
 * checkInterfaces does (MPH_to_AudioPlayer[48] = -1 -> SL_RESULT_FEATURE_UNSUPPORTED).
 * Replays FMOD's sequence from libfmodex (0xafc14-0xafe00). Built with the SDK's SLES headers. */
#include "aod/opensl_compat.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static char lastlog[300]; static int nlog;
void aod_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(lastlog, sizeof lastlog, fmt, ap); va_end(ap); nlog++; if (getenv("VERBOSE")) puts(lastlog); }

/* IIDs: distinct storage, as the SDK's OpenSLES_IID.o provides; FMOD passes dlsym'd pointers */
static struct SLInterfaceID_ iid_store[5] = { { 1 }, { 2 }, { 3 }, { 4 }, { 5 } };
static SLInterfaceID IID_ENG = &iid_store[0], IID_CFG = &iid_store[1], IID_ASBQ = &iid_store[2], IID_PLAY = &iid_store[3], IID_VOL = &iid_store[4];

/* ---- mock SDK objects ---- */
typedef struct { const struct SLObjectItf_ *itf; SLuint32 state; int kind; int destroyed; const void *bq_itf; } mobj;   /* kind 0 engine 1 mix 2 player */
static mobj objs[32]; static int nobj, destroyed_count;
static SLuint32 last_n; static SLInterfaceID last_ids[8]; static SLboolean last_req[8]; static SLEngineItf last_self;
static int mix_created, fail_engine_itf;
static const void *bq_vtbl_dummy = (const void *)0x5a5a;
static mobj *M(SLObjectItf s) { return (mobj *)s; }
static SLresult m_Realize(SLObjectItf s, SLboolean a) { if (M(s)->state == SL_OBJECT_STATE_REALIZED) return SL_RESULT_PRECONDITIONS_VIOLATED; M(s)->state = SL_OBJECT_STATE_REALIZED; return 0; }
static SLresult m_Resume(SLObjectItf s, SLboolean a) { return 0; }
static SLresult m_GetState(SLObjectItf s, SLuint32 *st) { *st = M(s)->state; return 0; }
static const struct SLEngineItf_ m_eng_vtbl;
static const struct SLEngineItf_ *m_eng_itf = &m_eng_vtbl;
static SLresult m_GetInterface(SLObjectItf s, const SLInterfaceID iid, void *out) {
	mobj *o = M(s);
	if (o->kind == 0 && iid == IID_ENG) {
		if (fail_engine_itf) { *(SLEngineItf *)out = NULL; return SL_RESULT_RESOURCE_ERROR; }   /* wilhelm NULLs *pItf on failure */
		*(SLEngineItf *)out = &m_eng_itf; return 0;
	}
	if (o->kind == 2 && (iid == IID_ASBQ || iid == IID_PLAY)) {
		if (o->state != SL_OBJECT_STATE_REALIZED) return SL_RESULT_PRECONDITIONS_VIOLATED;
		*(const void **)out = &o->bq_itf; return 0;
	}
	return SL_RESULT_FEATURE_UNSUPPORTED;   /* incl. ANDROIDCONFIGURATION on a player */
}
static SLresult m_RegisterCallback(SLObjectItf s, slObjectCallback cb, void *c) { return 0; }
static void m_Abort(SLObjectItf s) {}
static void m_Destroy(SLObjectItf s) { M(s)->destroyed = 1; destroyed_count++; }
static SLresult m_SetPriority(SLObjectItf s, SLint32 p, SLboolean b) { return 0; }
static SLresult m_GetPriority(SLObjectItf s, SLint32 *p, SLboolean *b) { *p = 7; return 0; }
static SLresult m_SetLoC(SLObjectItf s, SLint16 n, SLInterfaceID *i, SLboolean e) { return 0; }
static const struct SLObjectItf_ m_obj_vtbl = { m_Realize, m_Resume, m_GetState, m_GetInterface, m_RegisterCallback, m_Abort, m_Destroy,
	m_SetPriority, m_GetPriority, m_SetLoC };
static SLObjectItf new_mobj(int kind) { if (nobj >= 32) abort(); mobj *o = &objs[nobj++]; memset(o, 0, sizeof *o); o->itf = &m_obj_vtbl; o->kind = kind; o->state = SL_OBJECT_STATE_UNREALIZED; o->bq_itf = bq_vtbl_dummy; return (SLObjectItf)o; }

static SLresult m_CreateAudioPlayer(SLEngineItf self, SLObjectItf *p, SLDataSource *src, SLDataSink *snk, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req) {
	last_self = self; last_n = n;
	for (SLuint32 i = 0; i < n && i < 8; i++) { last_ids[i] = ids[i]; last_req[i] = req ? req[i] : 0; }
	for (SLuint32 i = 0; i < n; i++)   /* the SDK's checkInterfaces: MPH 48 unavailable for AudioPlayer */
		if (ids[i] == IID_CFG && req && req[i]) return SL_RESULT_FEATURE_UNSUPPORTED;
	*p = new_mobj(2); return 0;
}
static SLresult m_CreateOutputMix(SLEngineItf self, SLObjectItf *o, SLuint32 n, const SLInterfaceID *ids, const SLboolean *req) { last_self = self; mix_created++; *o = new_mobj(1); return 0; }
#define STUB(name, ...) static SLresult name(SLEngineItf s, ##__VA_ARGS__) { last_self = s; return SL_RESULT_FEATURE_UNSUPPORTED; }
STUB(m_LED, SLObjectItf *o, SLuint32 a, SLuint32 b, const SLInterfaceID *c, const SLboolean *d)
STUB(m_Vibra, SLObjectItf *o, SLuint32 a, SLuint32 b, const SLInterfaceID *c, const SLboolean *d)
STUB(m_Rec, SLObjectItf *o, SLDataSource *a, SLDataSink *b, SLuint32 c, const SLInterfaceID *d, const SLboolean *e)
STUB(m_Midi, SLObjectItf *o, SLDataSource *a, SLDataSource *b, SLDataSink *c, SLDataSink *d, SLDataSink *e, SLuint32 f, const SLInterfaceID *g, const SLboolean *h)
STUB(m_Listener, SLObjectItf *o, SLuint32 a, const SLInterfaceID *b, const SLboolean *c)
STUB(m_3D, SLObjectItf *o, SLuint32 a, const SLInterfaceID *b, const SLboolean *c)
STUB(m_Meta, SLObjectItf *o, SLDataSource *a, SLuint32 b, const SLInterfaceID *c, const SLboolean *d)
STUB(m_Ext, SLObjectItf *o, void *a, SLuint32 b, SLuint32 c, const SLInterfaceID *d, const SLboolean *e)
static SLresult m_QNSI(SLEngineItf s, SLuint32 id, SLuint32 *n) { last_self = s; *n = 42; return 0; }
STUB(m_QSI, SLuint32 a, SLuint32 b, SLInterfaceID *c)
STUB(m_QNSE, SLuint32 *a)
STUB(m_QSE, SLuint32 a, SLchar *b, SLint16 *c)
STUB(m_IES, const SLchar *a, SLboolean *b)
static const struct SLEngineItf_ m_eng_vtbl = { m_LED, m_Vibra, m_CreateAudioPlayer, m_Rec, m_Midi, m_Listener, m_3D, m_CreateOutputMix,
	m_Meta, m_Ext, m_QNSI, m_QSI, m_QNSE, m_QSE, m_IES };
static SLresult m_slCreateEngine(SLObjectItf *e, SLuint32 no, const SLEngineOption *o, SLuint32 n, const SLInterfaceID *i, const SLboolean *r) { *e = new_mobj(0); return 0; }

/* output hooks (the SDK's SDL_open and _opensles_user_freq in the real build) */
static int user_freq, restarts; static void *restart_arg;
static void m_restart(void *eng) { restarts++; restart_arg = eng; }
static SLresult make_player(SLEngineItf eng, SLObjectItf *pl, SLuint32 hz, SLuint32 ch, SLuint32 bits) {
	static SLDataLocator_AndroidSimpleBufferQueue loc; static SLDataFormat_PCM fmt; static SLDataSource src;
	loc.locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE; loc.numBuffers = 4;
	fmt.formatType = SL_DATAFORMAT_PCM; fmt.numChannels = ch; fmt.samplesPerSec = hz * 1000; fmt.bitsPerSample = bits;
	fmt.containerSize = bits; fmt.channelMask = ch == 2 ? 3 : 4; fmt.endianness = SL_BYTEORDER_LITTLEENDIAN;
	src.pLocator = &loc; src.pFormat = &fmt;
	SLInterfaceID ids[2] = { IID_ASBQ, IID_CFG }; SLboolean req[2] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };
	return (*eng)->CreateAudioPlayer(eng, pl, &src, NULL, 2, ids, req);
}

int main(void) {
	aod_opensl_set_backend(m_slCreateEngine, IID_ENG, IID_CFG);
	aod_opensl_set_output_hooks(m_restart, &user_freq);
	/* calibration: the mock reproduces the boot-10 failure when FMOD's request goes straight to the SDK */
	SLObjectItf raw_engine; m_slCreateEngine(&raw_engine, 0, NULL, 0, NULL, NULL);
	SLEngineItf raw_eng; (*raw_engine)->GetInterface(raw_engine, IID_ENG, &raw_eng);
	SLInterfaceID fids[2] = { IID_ASBQ, IID_CFG }; SLboolean freq[2] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };
	SLObjectItf rawp = NULL;
	CHECK((*raw_eng)->CreateAudioPlayer(raw_eng, &rawp, NULL, NULL, 2, fids, freq) == SL_RESULT_FEATURE_UNSUPPORTED, "calibration: SDK rejects FMOD's request");

	/* ---- FMOD's sequence (libfmodex 0xafb4c...0xafe00) through the port ---- */
	SLObjectItf engine = NULL; SLEngineItf eng = NULL; SLObjectItf mix = NULL, player = NULL;
	CHECK(aod_slCreateEngine(&engine, 0, NULL, 0, NULL, NULL) == 0 && engine, "slCreateEngine");
	CHECK((*engine)->Realize(engine, SL_BOOLEAN_FALSE) == 0, "engine Realize forwarded");
	CHECK((*engine)->GetInterface(engine, IID_ENG, &eng) == 0 && eng && eng != raw_eng, "engine interface is the port's proxy");
	CHECK((*eng)->CreateOutputMix(eng, &mix, 0, NULL, NULL) == 0 && mix_created == 1 && last_self == &m_eng_itf, "CreateOutputMix forwarded with the real engine itf");
	CHECK((*mix)->Realize(mix, SL_BOOLEAN_FALSE) == 0, "mix realize (real object, unwrapped)");
	CHECK((*eng)->CreateAudioPlayer(eng, &player, NULL, NULL, 2, fids, freq) == 0 && player, "CreateAudioPlayer succeeds (%s)", lastlog);
	CHECK(last_n == 1 && last_ids[0] == IID_ASBQ && last_req[0] == SL_BOOLEAN_TRUE, "SDK receives only ANDROIDSIMPLEBUFFERQUEUE, still required");
	SLAndroidConfigurationItf cfg = NULL;
	CHECK((*player)->GetInterface(player, IID_CFG, &cfg) == 0 && cfg, "GetInterface(ANDROIDCONFIGURATION)");
	SLint32 st = 3;
	CHECK((*cfg)->SetConfiguration(cfg, (const SLchar *)"androidPlaybackStreamType", &st, sizeof(SLint32)) == 0, "SetConfiguration(stream MEDIA) before Realize");
	CHECK((*player)->Realize(player, SL_BOOLEAN_FALSE) == 0, "player Realize forwarded");
	void *play = NULL, *bq = NULL;
	CHECK((*player)->GetInterface(player, IID_PLAY, &play) == 0 && play, "GetInterface(PLAY) forwarded");
	CHECK((*player)->GetInterface(player, IID_ASBQ, &bq) == 0 && bq && *(const void **)bq == bq_vtbl_dummy, "GetInterface(ASBQ) returns the SDK's real interface");

	/* ---- Android rules ---- */
	st = 1;
	CHECK((*cfg)->SetConfiguration(cfg, (const SLchar *)"androidPlaybackStreamType", &st, 4) == SL_RESULT_PRECONDITIONS_VIOLATED, "after Realize: PRECONDITIONS_VIOLATED");
	SLuint32 sz = 0; SLint32 got = -1;
	CHECK((*cfg)->GetConfiguration(cfg, (const SLchar *)"androidPlaybackStreamType", &sz, NULL) == 0 && sz == 4, "size query");
	CHECK((*cfg)->GetConfiguration(cfg, (const SLchar *)"androidPlaybackStreamType", &sz, &got) == 0 && got == 3, "value is MEDIA (3), unchanged by the rejected set");
	sz = 2;
	CHECK((*cfg)->GetConfiguration(cfg, (const SLchar *)"androidPlaybackStreamType", &sz, &got) == SL_RESULT_PARAMETER_INVALID, "short buffer rejected");
	CHECK((*cfg)->GetConfiguration(cfg, (const SLchar *)"androidPlaybackStreamType", NULL, &got) == SL_RESULT_PARAMETER_INVALID, "NULL size rejected");
	/* a fresh player for the pre-Realize rules */
	SLObjectItf p2 = NULL; SLAndroidConfigurationItf c2 = NULL;
	CHECK((*eng)->CreateAudioPlayer(eng, &p2, NULL, NULL, 2, fids, freq) == 0 && (*p2)->GetInterface(p2, IID_CFG, &c2) == 0, "second player");
	sz = 4; got = -1;
	CHECK((*c2)->GetConfiguration(c2, (const SLchar *)"androidPlaybackStreamType", &sz, &got) == 0 && got == 3, "default stream type is MEDIA");
	SLint32 bad = 6, neg = -1, voice = 0;
	CHECK((*c2)->SetConfiguration(c2, (const SLchar *)"androidPlaybackStreamType", &bad, 4) == SL_RESULT_PARAMETER_INVALID, "stream 6 rejected");
	CHECK((*c2)->SetConfiguration(c2, (const SLchar *)"androidPlaybackStreamType", &neg, 4) == SL_RESULT_PARAMETER_INVALID, "stream -1 rejected");
	CHECK((*c2)->SetConfiguration(c2, (const SLchar *)"androidPlaybackStreamType", &voice, 2) == SL_RESULT_PARAMETER_INVALID, "short value rejected");
	CHECK((*c2)->SetConfiguration(c2, (const SLchar *)"androidPlaybackStreamType", NULL, 4) == SL_RESULT_PARAMETER_INVALID, "NULL value rejected");
	CHECK((*c2)->SetConfiguration(c2, (const SLchar *)"androidRecordingPreset", &voice, 4) == SL_RESULT_PARAMETER_INVALID, "recorder key on a player rejected");
	CHECK((*c2)->SetConfiguration(c2, (const SLchar *)"androidPlaybackPerformanceMode", &voice, 4) == SL_RESULT_PARAMETER_INVALID, "unknown key rejected");
	CHECK((*c2)->SetConfiguration(c2, NULL, &voice, 4) == SL_RESULT_PARAMETER_INVALID, "NULL key rejected");
	CHECK((*c2)->SetConfiguration(c2, (const SLchar *)"androidPlaybackStreamType", &voice, 4) == 0, "VOICE accepted before Realize");
	sz = 4;
	CHECK((*c2)->GetConfiguration(c2, (const SLchar *)"androidPlaybackStreamType", &sz, &got) == 0 && got == 0, "per-player state (VOICE)");
	sz = 4; (*cfg)->GetConfiguration(cfg, (const SLchar *)"androidPlaybackStreamType", &sz, &got);
	CHECK(got == 3, "first player still MEDIA");
	/* unaligned value pointer (UBSan build) */
	unsigned char raw[8] = { 0 }; SLint32 alarm = 4; memcpy(raw + 1, &alarm, 4);
	SLObjectItf p3; SLAndroidConfigurationItf c3;
	(*eng)->CreateAudioPlayer(eng, &p3, NULL, NULL, 2, fids, freq); (*p3)->GetInterface(p3, IID_CFG, &c3);
	CHECK((*c3)->SetConfiguration(c3, (const SLchar *)"androidPlaybackStreamType", raw + 1, 4) == 0, "unaligned value accepted");

	/* ---- request shapes ---- */
	SLInterfaceID only_asbq[1] = { IID_ASBQ }; SLboolean t1[1] = { SL_BOOLEAN_TRUE };
	SLObjectItf plain = NULL;
	CHECK((*eng)->CreateAudioPlayer(eng, &plain, NULL, NULL, 1, only_asbq, t1) == 0 && (void *)plain == (void *)&objs[nobj - 1], "no ANDROIDCONFIGURATION requested: SDK object returned unwrapped");
	SLInterfaceID many[4] = { IID_CFG, IID_VOL, IID_CFG, IID_ASBQ }; SLboolean mreq[4] = { 1, 0, 1, 1 };
	SLObjectItf p4 = NULL;
	CHECK((*eng)->CreateAudioPlayer(eng, &p4, NULL, NULL, 4, many, mreq) == 0 && last_n == 2 && last_ids[0] == IID_VOL && last_req[0] == 0 &&
	      last_ids[1] == IID_ASBQ && last_req[1] == 1, "all ANDROIDCONFIGURATION entries removed, order and required flags of the rest kept");
	SLInterfaceID cfg_only[1] = { IID_CFG }; SLObjectItf p5 = NULL;
	CHECK((*eng)->CreateAudioPlayer(eng, &p5, NULL, NULL, 1, cfg_only, NULL) == 0 && last_n == 0, "NULL required array handled");
	struct SLInterfaceID_ cfg_copy = iid_store[1];   /* same IID value at a different address */
	SLInterfaceID byval[2] = { IID_ASBQ, &cfg_copy }; SLObjectItf p6 = NULL; SLAndroidConfigurationItf c6 = NULL;
	CHECK((*eng)->CreateAudioPlayer(eng, &p6, NULL, NULL, 2, byval, freq) == 0 && last_n == 1 && (*p6)->GetInterface(p6, &cfg_copy, &c6) == 0,
	      "IIDs compared by value, not only by pointer");
	/* engine forwarding of a query method */
	SLuint32 q = 0;
	CHECK((*eng)->QueryNumSupportedInterfaces(eng, 0, &q) == 0 && q == 42 && last_self == &m_eng_itf, "query methods forwarded with the real engine itf");
	/* R3: a failing repeat GetInterface(ENGINE) must not clobber the engine proxy already handed out */
	SLEngineItf eng2 = (SLEngineItf)0x1;
	fail_engine_itf = 1;
	CHECK((*engine)->GetInterface(engine, IID_ENG, &eng2) == SL_RESULT_RESOURCE_ERROR && eng2 == (SLEngineItf)0x1, "failed repeat GetInterface(ENGINE) reported, output untouched");
	fail_engine_itf = 0;
	q = 0;
	CHECK((*eng)->QueryNumSupportedInterfaces(eng, 0, &q) == 0 && q == 42, "engine proxy still usable after the failed repeat");
	SLint32 prio = 0; SLboolean pre = 0;
	CHECK((*player)->GetPriority(player, &prio, &pre) == 0 && prio == 7, "object methods forwarded");
	void *vol = NULL;
	CHECK((*engine)->GetInterface(engine, IID_CFG, &vol) == SL_RESULT_FEATURE_UNSUPPORTED, "engine does not grow an ANDROIDCONFIGURATION");

	/* ---- boot-12: SDK output rate follows the player's PCM rate ---- */
	CHECK(aod_opensl_vita_bgm_rate_ok(24000) && aod_opensl_vita_bgm_rate_ok(48000) && aod_opensl_vita_bgm_rate_ok(44100) &&
	      !aod_opensl_vita_bgm_rate_ok(11111) && !aod_opensl_vita_bgm_rate_ok(96000), "Vita BGM port rates");
	SLObjectItf r1 = NULL, r2 = NULL, r3 = NULL, r4 = NULL, r5 = NULL;
	restarts = 0; user_freq = 0;
	CHECK(make_player(eng, &r1, 44100, 2, 16) == 0 && restarts == 0 && user_freq == 0, "44100 stereo: output already matches, no restart");
	CHECK(make_player(eng, &r2, 24000, 2, 16) == 0 && restarts == 1 && user_freq == 24000 && restart_arg == (void *)&m_eng_itf,
	      "24000 stereo: output rerun at 24000 with the SDK engine (IEngine*) (%d restarts, freq %d)", restarts, user_freq);
	CHECK(strstr(lastlog, "restarted at 24000 Hz") != NULL, "restart logged (%s)", lastlog);
	CHECK(make_player(eng, &r3, 24000, 2, 16) == 0 && restarts == 1, "same rate again: no second restart");
	CHECK(make_player(eng, &r4, 22050, 1, 16) == 0 && restarts == 1 && user_freq == 24000, "mono: not supported by the SDK mixer, no restart");
	CHECK(make_player(eng, &r5, 11111, 2, 16) == 0 && restarts == 1 && user_freq == 24000 && strstr(lastlog, "not a Vita BGM port rate"),
	      "invalid Vita rate refused (%s)", lastlog);
	(*r1)->Destroy(r1); (*r2)->Destroy(r2); (*r3)->Destroy(r3); (*r4)->Destroy(r4); (*r5)->Destroy(r5);

	/* ---- lifetime: Destroy destroys the SDK object and frees the proxy (ASan checks the frees) ---- */
	int d0 = destroyed_count;
	(*player)->Destroy(player); (*p2)->Destroy(p2); (*p3)->Destroy(p3); (*p4)->Destroy(p4); (*p5)->Destroy(p5); (*p6)->Destroy(p6);
	(*plain)->Destroy(plain); (*mix)->Destroy(mix); (*engine)->Destroy(engine);
	CHECK(destroyed_count - d0 == 9, "every SDK object destroyed exactly once (%d)", destroyed_count - d0);
	printf("%d/%d checks passed (opensl compat)\n", checks - fails, checks);
	return fails != 0;
}
