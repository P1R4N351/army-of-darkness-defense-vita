/*
 * aod-vita audio evidence (boot-10): what reaches the Vita audio port, and what the SDK's
 * OpenSL ES library says on stderr. Passive and bounded; always on (a few lines per run).
 *
 * The SDK's libOpenSLES (AOSP wilhelm, SDL backend) opens a BGM port in its own audioThread,
 * mixes the active players with IOutputMixExt_FillBuffer and calls sceAudioOutOutput every
 * 128 frames. All of its errors go to newlib stderr (fprintf), invisible on the Vita, so this
 * module both counts the port traffic (via -Wl,--wrap) and forwards stderr into the log.
 *
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#ifndef AOD_AUDIO_DIAG_H
#define AOD_AUDIO_DIAG_H

#include <stddef.h>

#define AOD_AUDIODIAG_PEAK_EVERY 64      /* sample the PCM of every Nth output buffer */
#define AOD_AUDIODIAG_PEAK_SAMPLES 256   /* at most this many int16 samples per sampled buffer */
#define AOD_AUDIODIAG_EVENT_LOG_MAX 16   /* open/volume/failure lines logged individually */
#define AOD_AUDIODIAG_STDERR_LINES_MAX 200
#define AOD_AUDIODIAG_STDERR_LINE 240
#define AOD_AUDIODIAG_PORTS 8            /* opened ports remembered (id -> bytes per buffer) */

typedef struct {
	unsigned opens, open_fails, outputs, output_fails, peak_checks, nonzero_checks, max_peak, last_peak;
	unsigned volume_calls, volume_fails, stderr_lines, stderr_dropped;
	unsigned unknown_port_checks;   /* sampled buffers whose port geometry was unknown: NOT counted as silence */
	unsigned fills, fill_max_us, output_max_us;
	unsigned long long fill_total_us, output_total_us;
	int last_open_rc, last_output_rc;
} aod_audiodiag_stats;

void aod_audiodiag_open(int type, int grain, int freq, int mode, int rc);
void aod_audiodiag_sample(int port, const void *buf);   /* before the real sceAudioOutOutput */
void aod_audiodiag_output(int port, int rc);                /* after it: accounting only */
void aod_audiodiag_volume(int port, int flags, const int *vol, int rc);
/* Where libOpenSLES's audioThread spends its time: IOutputMixExt_FillBuffer (mixing, including the
 * player's buffer-queue callback into FMOD) vs the real sceAudioOutOutput. */
void aod_audiodiag_fill_time(unsigned us);
void aod_audiodiag_output_time(unsigned us);
aod_audiodiag_stats aod_audiodiag_get(void);
void aod_audiodiag_report(const char *when);
/* stderr capture: complete lines are logged as "[stderr] ..."; returns n (all bytes consumed).
 * Not thread-safe by itself: callers must serialise (the Vita writer holds an LwMutex). */
size_t aod_audiodiag_stderr_write(const char *data, size_t n);
/* Vita only: route newlib stderr through aod_audiodiag_stderr_write (call early, main thread). */
void aod_audiodiag_install_stderr(void);

#endif
