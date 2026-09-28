/*
 * aod-vita: Android OpenSL ES compatibility over the VitaSDK libOpenSLES (boot-11 audio fix).
 *
 * FMOD's OpenSL output requires SL_IID_ANDROIDCONFIGURATION on its AudioPlayer and uses it for one
 * call, SetConfiguration("androidPlaybackStreamType", SL_ANDROID_STREAM_MEDIA) before Realize; any
 * failure disables FMOD output (no fallback). The SDK library does not implement that interface
 * (MPH_to_AudioPlayer[48] = -1), so CreateAudioPlayer was rejected and the game was silent.
 *
 * slCreateEngine is resolved through our dlsym table, so the port hands FMOD a thin proxy engine.
 * Its CreateAudioPlayer removes ANDROIDCONFIGURATION from the list passed to the SDK and wraps the
 * resulting player; the wrapper serves a real AndroidConfiguration (stream type, Android rules) and
 * forwards everything else. On the Vita every player is mixed into the SDK library's single BGM
 * port, so the stream type is recorded and reported but cannot select a different route.
 * See docs/AUDIO-LEDGER.md.
 *
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#ifndef AOD_OPENSL_COMPAT_H
#define AOD_OPENSL_COMPAT_H

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

typedef SLresult (*aod_slCreateEngine_fn)(SLObjectItf *pEngine, SLuint32 numOptions, const SLEngineOption *pEngineOptions,
                                          SLuint32 numInterfaces, const SLInterfaceID *pInterfaceIds,
                                          const SLboolean *pInterfaceRequired);

/* The real library entry point and interface IDs (tests pass mocks). */
void aod_opensl_set_backend(aod_slCreateEngine_fn real_create, SLInterfaceID iid_engine, SLInterfaceID iid_android_configuration);

/* Output-rate follow (boot-12). The SDK mixer (IOutputMixExt_FillBuffer) copies player PCM as
 * 16-bit stereo frames at the port rate and never reads the player's samplesPerSec, so a player at
 * any other rate plays at the wrong pitch. The SDK's audioThread opens its BGM port at
 * *_opensles_user_freq (if > 0, else 44100), and SDL_open(engine) restarts that thread and port.
 * Given both, CreateAudioPlayer reruns the port at the player's rate. */
typedef void (*aod_opensl_restart_fn)(void *sdk_engine);
void aod_opensl_set_output_hooks(aod_opensl_restart_fn restart, int *user_freq);
int aod_opensl_vita_bgm_rate_ok(SLuint32 hz);

/* What FMOD gets from dlsym("slCreateEngine"). */
SLresult aod_slCreateEngine(SLObjectItf *pEngine, SLuint32 numOptions, const SLEngineOption *pEngineOptions,
                            SLuint32 numInterfaces, const SLInterfaceID *pInterfaceIds, const SLboolean *pInterfaceRequired);

#define AOD_OPENSL_MAX_INTERFACES 32   /* larger requests are passed through unchanged */

#endif
