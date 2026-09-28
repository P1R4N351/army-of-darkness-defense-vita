/*
 * aod-vita FMOD boundary evidence (boot-12), Vita binding. libgame's imports of these FMOD methods
 * are resolved against libfmodex by so_resolve; aod_fmoddiag_rebind captures each resolved address
 * as the real function and points libgame's slots at a wrapper that forwards unchanged (this in r0;
 * softfp floats in core registers, as FMOD is built; bools as full registers).
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#include "aod/fmod_diag.h"

#include <psp2/kernel/processmgr.h>
#include <stdint.h>
#include <string.h>

typedef int (*create_fn)(void *, const char *, unsigned, void *, void **);
typedef int (*play_fn)(void *, int, void *, int, void **);
typedef int (*vol_fn)(void *, float);
typedef int (*pause_fn)(void *, int);
static create_fn real_createSound, real_createStream;
static play_fn real_playSound;
static vol_fn real_group_setVolume, real_channel_setVolume;
static pause_fn real_group_setPaused, real_channel_setPaused;

static int w_createSound(void *sys, const char *name, unsigned mode, void *ex, void **snd) {
	SceUInt64 t0 = sceKernelGetProcessTimeWide();
	int rc = real_createSound(sys, name, mode, ex, snd);
	aod_fmoddiag_create(AOD_FMOD_SOUND, name, mode, rc, (unsigned)(sceKernelGetProcessTimeWide() - t0));
	return rc;
}
static int w_createStream(void *sys, const char *name, unsigned mode, void *ex, void **snd) {
	SceUInt64 t0 = sceKernelGetProcessTimeWide();
	int rc = real_createStream(sys, name, mode, ex, snd);
	aod_fmoddiag_create(AOD_FMOD_STREAM, name, mode, rc, (unsigned)(sceKernelGetProcessTimeWide() - t0));
	return rc;
}
static int w_playSound(void *sys, int id, void *snd, int paused, void **ch) {
	int rc = real_playSound(sys, id, snd, paused, ch);
	aod_fmoddiag_play(rc, paused & 0xff);
	return rc;
}
static int w_group_setVolume(void *g, float v) { aod_fmoddiag_group_volume(g, v); return real_group_setVolume(g, v); }
static int w_group_setPaused(void *g, int p) { aod_fmoddiag_group_paused(g, p & 0xff); return real_group_setPaused(g, p); }
static int w_channel_setVolume(void *c, float v) { aod_fmoddiag_channel_volume(c, v); return real_channel_setVolume(c, v); }
static int w_channel_setPaused(void *c, int p) { aod_fmoddiag_channel_paused(c, p & 0xff); return real_channel_setPaused(c, p); }

static const struct { const char *sym; void **real; void *wrap; } hooks[] = {
	{ "_ZN4FMOD6System11createSoundEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE", (void **)&real_createSound, (void *)w_createSound },
	{ "_ZN4FMOD6System12createStreamEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE", (void **)&real_createStream, (void *)w_createStream },
	{ "_ZN4FMOD6System9playSoundE17FMOD_CHANNELINDEXPNS_5SoundEbPPNS_7ChannelE", (void **)&real_playSound, (void *)w_playSound },
	{ "_ZN4FMOD12ChannelGroup9setVolumeEf", (void **)&real_group_setVolume, (void *)w_group_setVolume },
	{ "_ZN4FMOD12ChannelGroup9setPausedEb", (void **)&real_group_setPaused, (void *)w_group_setPaused },
	{ "_ZN4FMOD7Channel9setVolumeEf", (void **)&real_channel_setVolume, (void *)w_channel_setVolume },
	{ "_ZN4FMOD7Channel9setPausedEb", (void **)&real_channel_setPaused, (void *)w_channel_setPaused },
};

/* get_slot(name) returns the address libgame's slot currently holds (0 if none); rebind(name, value)
 * repoints every slot for name. A hook is installed only if the real address is known. */
int aod_fmoddiag_rebind(uintptr_t (*get_slot)(const char *), int (*rebind)(const char *, uintptr_t)) {
	int n = 0;
	for (unsigned i = 0; i < sizeof hooks / sizeof hooks[0]; i++) {
		uintptr_t real = get_slot(hooks[i].sym);
		if (!real || real == (uintptr_t)hooks[i].wrap) continue;
		*hooks[i].real = (void *)real;
		n += rebind(hooks[i].sym, (uintptr_t)hooks[i].wrap) > 0;
	}
	return n;
}
