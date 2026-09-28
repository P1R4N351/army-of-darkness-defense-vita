/*
 * aod-vita FMOD boundary evidence core: see fmod_diag.h. Host-testable. Called from any game or FMOD
 * thread; shared counters use __atomic builtins, reports read them field by field.
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#include "aod/fmod_diag.h"
#include "aod/port.h"

#include <string.h>

#define LOAD(p) __atomic_load_n((p), __ATOMIC_RELAXED)
#define INC(p) __atomic_fetch_add((p), 1, __ATOMIC_RELAXED)

static aod_fmoddiag_stats st;
static unsigned create_logged, stream_logged, slow_logged, event_logged;

static const char *tail(const char *name) {   /* keep log lines short: the last path component */
	if (!name) return "(null)";
	const char *s = strrchr(name, '/');
	return s ? s + 1 : name;
}

static void raise_max(unsigned *p, unsigned v) {
	unsigned cur = LOAD(p);
	while (v > cur && !__atomic_compare_exchange_n(p, &cur, v, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

void aod_fmoddiag_create(int kind, const char *name, unsigned mode, int rc, unsigned us) {
	INC(kind == AOD_FMOD_STREAM ? &st.streams : &st.sounds);
	__atomic_fetch_add(kind == AOD_FMOD_STREAM ? &st.stream_total_us : &st.sound_total_us, (unsigned long long)us, __ATOMIC_RELAXED);
	raise_max(&st.max_create_us, us);
	if (rc != 0) INC(&st.create_fails);
	int slow = us >= AOD_FMODDIAG_SLOW_US;
	if (slow) INC(&st.slow_creates);
	const char *what = kind == AOD_FMOD_STREAM ? "createStream" : "createSound";
	if (INC(&create_logged) < AOD_FMODDIAG_CREATE_LOG_MAX || (kind == AOD_FMOD_STREAM && INC(&stream_logged) < AOD_FMODDIAG_STREAM_LOG_MAX) ||
	    (rc != 0 && INC(&event_logged) < AOD_FMODDIAG_EVENT_LOG_MAX) || (slow && INC(&slow_logged) < AOD_FMODDIAG_SLOW_LOG_MAX))
		aod_log("fmod: %s(\"%s\", mode %#x) -> %d in %u.%03u ms%s", what, tail(name), mode, rc, us / 1000, us % 1000,
		        slow ? " (SLOW)" : "");
}

void aod_fmoddiag_play(int rc, int paused) {
	INC(&st.plays);
	if (rc != 0) INC(&st.play_fails);
	if (INC(&event_logged) < AOD_FMODDIAG_EVENT_LOG_MAX)
		aod_log("fmod: playSound(paused %d) -> %d", paused, rc);
}

void aod_fmoddiag_group_volume(const void *group, float v) {
	if (v == 0.0f) INC(&st.zero_volume_sets);
	if (INC(&event_logged) < AOD_FMODDIAG_EVENT_LOG_MAX) aod_log("fmod: ChannelGroup %p setVolume(%.3f)", group, (double)v);
}

void aod_fmoddiag_group_paused(const void *group, int paused) {
	if (paused) INC(&st.group_pauses);
	if (INC(&event_logged) < AOD_FMODDIAG_EVENT_LOG_MAX) aod_log("fmod: ChannelGroup %p setPaused(%d)", group, paused);
}

void aod_fmoddiag_channel_volume(const void *channel, float v) {
	if (v == 0.0f) INC(&st.zero_volume_sets);
	if (INC(&event_logged) < AOD_FMODDIAG_EVENT_LOG_MAX) aod_log("fmod: Channel %p setVolume(%.3f)", channel, (double)v);
}

void aod_fmoddiag_channel_paused(const void *channel, int paused) {
	if (INC(&event_logged) < AOD_FMODDIAG_EVENT_LOG_MAX) aod_log("fmod: Channel %p setPaused(%d)", channel, paused);
}

aod_fmoddiag_stats aod_fmoddiag_get(void) {
	aod_fmoddiag_stats s;
	s.sounds = LOAD(&st.sounds); s.streams = LOAD(&st.streams); s.create_fails = LOAD(&st.create_fails);
	s.slow_creates = LOAD(&st.slow_creates); s.max_create_us = LOAD(&st.max_create_us);
	s.sound_total_us = LOAD(&st.sound_total_us); s.stream_total_us = LOAD(&st.stream_total_us);
	s.plays = LOAD(&st.plays); s.play_fails = LOAD(&st.play_fails);
	s.zero_volume_sets = LOAD(&st.zero_volume_sets); s.group_pauses = LOAD(&st.group_pauses);
	return s;
}

void aod_fmoddiag_report(const char *when) {
	aod_fmoddiag_stats s = aod_fmoddiag_get();
	aod_log("fmod %s: createSound n=%u total=%llums | createStream n=%u total=%llums | fails=%u slow(>=%ums)=%u max=%ums | "
	        "playSound n=%u (fail %u) | zero-volume sets=%u group pauses=%u",
	        when, s.sounds, s.sound_total_us / 1000, s.streams, s.stream_total_us / 1000, s.create_fails, AOD_FMODDIAG_SLOW_US / 1000,
	        s.slow_creates, s.max_create_us / 1000, s.plays, s.play_fails, s.zero_volume_sets, s.group_pauses);
}
