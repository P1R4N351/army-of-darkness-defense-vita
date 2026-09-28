/*
 * aod-vita audio evidence core: see audio_diag.h. Host-testable.
 * Ownership: open/output/volume are called from libOpenSLES's audioThread (SDL_open joins the old
 * thread before starting a new one); aod_audiodiag_get/report run on the main thread. Every field
 * shared between them is accessed only with __atomic builtins; the port table publishes each slot
 * with a release store of its ready flag, read with acquire. stderr_write needs caller serialisation
 * (the Vita writer holds an LwMutex).
 *
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#include "aod/audio_diag.h"
#include "aod/port.h"

#include <stdint.h>
#include <string.h>

#define LOAD(p) __atomic_load_n((p), __ATOMIC_RELAXED)
#define LOAD_ACQ(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define STORE_REL(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define STORE(p, v) __atomic_store_n((p), (v), __ATOMIC_RELAXED)
#define INC(p) __atomic_fetch_add((p), 1, __ATOMIC_RELAXED)

static aod_audiodiag_stats st;
static unsigned events_logged;
/* Port handles are opaque (no range is promised), so geometry is kept per handle, not indexed by it.
 * A slot is claimed with fetch_add, filled, then published by a release store of `ready`. */
static struct { int id; unsigned bytes; int ready; } ports[AOD_AUDIODIAG_PORTS];
static unsigned n_claimed;

static int find_port(int port) {
	unsigned n = LOAD(&n_claimed);
	for (unsigned i = 0; i < n && i < AOD_AUDIODIAG_PORTS; i++)
		if (LOAD_ACQ(&ports[i].ready) && LOAD(&ports[i].id) == port) return (int)i;
	return -1;
}

static unsigned port_bytes(int port) {
	int i = find_port(port);
	return i < 0 ? 0 : LOAD(&ports[i].bytes);
}

static void remember_port(int port, unsigned bytes) {
	int i = find_port(port);
	if (i >= 0) { STORE(&ports[i].bytes, bytes); return; }   /* reopened handle: update in place */
	unsigned slot = __atomic_fetch_add(&n_claimed, 1, __ATOMIC_RELAXED);
	if (slot >= AOD_AUDIODIAG_PORTS) return;                  /* table full: those ports stay "unknown" */
	STORE(&ports[slot].id, port);
	STORE(&ports[slot].bytes, bytes);
	STORE_REL(&ports[slot].ready, 1);
}

static int event_log_ok(void) { return INC(&events_logged) < AOD_AUDIODIAG_EVENT_LOG_MAX; }

void aod_audiodiag_open(int type, int grain, int freq, int mode, int rc) {
	INC(&st.opens);
	STORE(&st.last_open_rc, rc);
	if (rc < 0) INC(&st.open_fails);
	else if (grain > 0)
		remember_port(rc, (unsigned)grain * (mode == 1 ? 2u : 1u) * 2u); /* mode 1 = stereo, S16 */
	if (event_log_ok())
		aod_log("audio: sceAudioOutOpenPort(type %d, grain %d, freq %d, mode %d) -> %#x", type, grain, freq, mode, (unsigned)rc);
}

static void raise_max(unsigned *p, unsigned v) {
	unsigned cur = LOAD(p);
	while (v > cur && !__atomic_compare_exchange_n(p, &cur, v, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

/* Called with the caller's buffer BEFORE it is passed to sceAudioOutOutput: the audio thread owns it
 * and is blocked in the wrapper, and the samples are exactly what is about to be queued. */
void aod_audiodiag_sample(int port, const void *buf) {
	unsigned n = LOAD(&st.outputs) + 1;   /* the output this buffer is about to become */
	if (n % AOD_AUDIODIAG_PEAK_EVERY || !buf) return;
	unsigned bytes = port_bytes(port);
	if (!bytes) { INC(&st.unknown_port_checks); return; }
	unsigned count = bytes / 2;
	if (count > AOD_AUDIODIAG_PEAK_SAMPLES) count = AOD_AUDIODIAG_PEAK_SAMPLES;
	unsigned peak = 0;
	for (unsigned i = 0; i < count; i++) {
		int16_t s;
		memcpy(&s, (const char *)buf + 2 * i, sizeof s);
		unsigned a = s < 0 ? (unsigned)(-(int)s) : (unsigned)s;
		if (a > peak) peak = a;
	}
	INC(&st.peak_checks);
	STORE(&st.last_peak, peak);
	if (peak) INC(&st.nonzero_checks);
	raise_max(&st.max_peak, peak);
}

/* Called after sceAudioOutOutput returns: accounting only, the buffer is not touched. */
void aod_audiodiag_output(int port, int rc) {
	INC(&st.outputs);
	if (rc < 0) {
		INC(&st.output_fails);
		STORE(&st.last_output_rc, rc);
		if (event_log_ok()) aod_log("audio: sceAudioOutOutput(port %d) -> %#x", port, (unsigned)rc);
	}
}

void aod_audiodiag_volume(int port, int flags, const int *vol, int rc) {
	INC(&st.volume_calls);
	if (rc < 0) INC(&st.volume_fails);
	if (event_log_ok())
		aod_log("audio: sceAudioOutSetVolume(port %d, flags %#x, vol %d/%d) -> %#x", port, (unsigned)flags,
		        vol && (flags & 1) ? vol[0] : -1, vol && (flags & 2) ? vol[1] : -1, (unsigned)rc); /* only the selected channels */
}

void aod_audiodiag_fill_time(unsigned us) {
	INC(&st.fills);
	__atomic_fetch_add(&st.fill_total_us, (unsigned long long)us, __ATOMIC_RELAXED);
	raise_max(&st.fill_max_us, us);
}

void aod_audiodiag_output_time(unsigned us) {
	__atomic_fetch_add(&st.output_total_us, (unsigned long long)us, __ATOMIC_RELAXED);
	raise_max(&st.output_max_us, us);
}

aod_audiodiag_stats aod_audiodiag_get(void) {
	aod_audiodiag_stats s;   /* per-field atomic loads (fields may be from slightly different moments) */
	s.opens = LOAD(&st.opens); s.open_fails = LOAD(&st.open_fails);
	s.outputs = LOAD(&st.outputs); s.output_fails = LOAD(&st.output_fails);
	s.peak_checks = LOAD(&st.peak_checks); s.nonzero_checks = LOAD(&st.nonzero_checks);
	s.max_peak = LOAD(&st.max_peak); s.last_peak = LOAD(&st.last_peak);
	s.volume_calls = LOAD(&st.volume_calls); s.volume_fails = LOAD(&st.volume_fails);
	s.stderr_lines = LOAD(&st.stderr_lines); s.stderr_dropped = LOAD(&st.stderr_dropped);
	s.unknown_port_checks = LOAD(&st.unknown_port_checks);
	s.last_open_rc = LOAD(&st.last_open_rc); s.last_output_rc = LOAD(&st.last_output_rc);
	s.fills = LOAD(&st.fills); s.fill_max_us = LOAD(&st.fill_max_us); s.output_max_us = LOAD(&st.output_max_us);
	s.fill_total_us = LOAD(&st.fill_total_us); s.output_total_us = LOAD(&st.output_total_us);
	return s;
}

void aod_audiodiag_report(const char *when) {
	aod_audiodiag_stats s = aod_audiodiag_get();
	aod_log("audio %s: port opens=%u (fail %u, last rc %#x) outputs=%u (fail %u) pcm checks=%u nonzero=%u max-peak=%u last-peak=%u "
	        "unknown-port checks=%u volume calls=%u (fail %u) stderr lines=%u (+%u dropped) | "
	        "fill n=%u avg=%lluus max=%uus | output avg=%lluus max=%uus",
	        when, s.opens, s.open_fails, (unsigned)s.last_open_rc, s.outputs, s.output_fails, s.peak_checks, s.nonzero_checks,
	        s.max_peak, s.last_peak, s.unknown_port_checks, s.volume_calls, s.volume_fails, s.stderr_lines, s.stderr_dropped,
	        s.fills, s.fills ? s.fill_total_us / s.fills : 0ull, s.fill_max_us, s.outputs ? s.output_total_us / s.outputs : 0ull,
	        s.output_max_us);
}

static char line[AOD_AUDIODIAG_STDERR_LINE];
static size_t line_len;

static void flush_line(void) {
	if (!line_len) return;
	line[line_len] = 0;
	if (LOAD(&st.stderr_lines) < AOD_AUDIODIAG_STDERR_LINES_MAX) {
		INC(&st.stderr_lines);
		aod_log("[stderr] %s", line);
	} else
		INC(&st.stderr_dropped);
	line_len = 0;
}

size_t aod_audiodiag_stderr_write(const char *data, size_t n) {
	for (size_t i = 0; i < n; i++) {
		char c = data[i];
		if (c == '\n') { flush_line(); continue; }
		if (c == '\r') continue;
		if (line_len >= sizeof line - 1) flush_line();   /* over-long line: split */
		line[line_len++] = c;
	}
	return n;
}
