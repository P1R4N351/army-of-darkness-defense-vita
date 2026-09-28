/*
 * aod-vita FMOD boundary evidence (boot-12): what the game asks FMOD for, and how long it takes.
 * Passive and bounded; always on (a few lines per run). Discriminates "no BGM" (stream created?
 * played? muted?) and "lag on every sfx" (synchronous createSound cost on the game's threads).
 *
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#ifndef AOD_FMOD_DIAG_H
#define AOD_FMOD_DIAG_H

#define AOD_FMODDIAG_CREATE_LOG_MAX 32   /* first creates logged with name/mode/rc/time */
#define AOD_FMODDIAG_SLOW_US 50000       /* a create longer than this is a visible hitch at 57 fps */
#define AOD_FMODDIAG_SLOW_LOG_MAX 24
#define AOD_FMODDIAG_EVENT_LOG_MAX 32    /* play / volume / pause lines */
#define AOD_FMODDIAG_STREAM_LOG_MAX 16   /* streams (music) are always logged, up to this many */

enum { AOD_FMOD_SOUND = 0, AOD_FMOD_STREAM = 1 };

typedef struct {
	unsigned sounds, streams, create_fails, slow_creates, max_create_us;
	unsigned long long sound_total_us, stream_total_us;
	unsigned plays, play_fails, zero_volume_sets, group_pauses;
} aod_fmoddiag_stats;

void aod_fmoddiag_create(int kind, const char *name, unsigned mode, int rc, unsigned us);
void aod_fmoddiag_play(int rc, int paused);
void aod_fmoddiag_group_volume(const void *group, float v);
void aod_fmoddiag_group_paused(const void *group, int paused);
void aod_fmoddiag_channel_volume(const void *channel, float v);
void aod_fmoddiag_channel_paused(const void *channel, int paused);
aod_fmoddiag_stats aod_fmoddiag_get(void);
void aod_fmoddiag_report(const char *when);
/* Vita binding (fmod_diag_vita.c): instrument libgame's FMOD imports. Returns entry points hooked. */
#include <stdint.h>
int aod_fmoddiag_rebind(uintptr_t (*get_slot)(const char *), int (*rebind)(const char *, uintptr_t));

#endif
