/* ThreadSanitizer check of audio_diag's documented ownership: port open/output/volume on writer
 * threads (libOpenSLES's audioThread), get/report polled concurrently from the main thread. */
#include "aod/audio_diag.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;   /* the Vita logger is serialised too */
static unsigned nlog;
void aod_log(const char *fmt, ...) {
	char b[300]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
	pthread_mutex_lock(&log_lock); nlog++; pthread_mutex_unlock(&log_lock);
}
static int16_t tone[256];

static void *writer(void *arg) {
	int base = (int)(intptr_t)arg;
	for (int r = 0; r < 4; r++) {
		aod_audiodiag_open(1, 128, 44100, 1, base);        /* reopen the same handle: in-place update */
		aod_audiodiag_open(1, 128, 48000, 0, base + 1 + r); /* new handles: slot claims */
		for (int i = 0; i < 2000; i++) {
			aod_audiodiag_fill_time((unsigned)i);
			aod_audiodiag_sample(base, tone);
			aod_audiodiag_output_time(3);
			aod_audiodiag_output(base, 0);
		}
		int vol[2] = { 32767, 32767 };
		aod_audiodiag_volume(base, 3, vol, 0);
	}
	return NULL;
}

int main(void) {
	for (int i = 0; i < 256; i++) tone[i] = (int16_t)(i * 37);
	pthread_t a, b;
	pthread_create(&a, NULL, writer, (void *)(intptr_t)0x10000);
	pthread_create(&b, NULL, writer, (void *)(intptr_t)0x20000);
	unsigned max_seen = 0;
	for (int i = 0; i < 2000; i++) {
		aod_audiodiag_stats s = aod_audiodiag_get();
		if (s.max_peak > max_seen) max_seen = s.max_peak;
		if (i % 500 == 0) aod_audiodiag_report("race");
	}
	pthread_join(a, NULL);
	pthread_join(b, NULL);
	aod_audiodiag_stats s = aod_audiodiag_get();
	int ok = s.outputs == 2 * 4 * 2000 && /* two concurrent writers interleave the every-Nth decision: approximate */
	         s.peak_checks >= s.outputs / AOD_AUDIODIAG_PEAK_EVERY / 2 && s.peak_checks <= 2 * s.outputs / AOD_AUDIODIAG_PEAK_EVERY && s.max_peak == 255 * 37 &&
	         s.unknown_port_checks == 0 && s.fills == s.outputs && s.fill_max_us == 1999 && s.output_total_us == 3ull * s.outputs;
	printf("%s outputs=%u checks=%u max=%u unknown=%u (race test)\n", ok ? "1/1 checks passed" : "0/1 FAIL", s.outputs, s.peak_checks,
	       s.max_peak, s.unknown_port_checks);
	return !ok;
}
