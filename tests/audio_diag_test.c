/* Host test for source/aod/audio_diag.c: port geometry from sceAudioOutOpenPort, sparse PCM peak
 * sampling over a realistic buffer flow (128-frame stereo S16 buffers, as libOpenSLES's audioThread
 * outputs them), failure accounting, bounded logging and the stderr line assembler. */
#include "aod/audio_diag.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static char logs[512][300];
static int nlog;
void aod_log(const char *fmt, ...) {
	va_list ap; va_start(ap, fmt); vsnprintf(logs[nlog % 512], sizeof logs[0], fmt, ap); va_end(ap);
	if (getenv("VERBOSE")) puts(logs[nlog % 512]);
	nlog++;
}
/* the Vita wrapper's order: sample the prepared buffer, then account the real call's result */
static void out(int port, const void *buf, int rc) { aod_audiodiag_sample(port, buf); aod_audiodiag_output(port, rc); }
static const char *last(void) { return logs[(nlog - 1) % 512]; }
static int count(const char *s) { int n = 0; for (int i = 0; i < nlog && i < 512; i++) n += strstr(logs[i], s) != NULL; return n; }

int main(void) {
	/* libOpenSLES opens: BGM (1), grain 128, 44100 Hz, stereo (1). Handles are opaque: use a large one. */
	const int P = 0x10000;
	aod_audiodiag_open(1, 128, 44100, 1, P);
	CHECK(strstr(last(), "sceAudioOutOpenPort(type 1, grain 128, freq 44100, mode 1) -> 0x10000"), "open logged (%s)", last());
	static int16_t silent[256], tone[256];
	for (int i = 0; i < 256; i++) tone[i] = (int16_t)((i % 2 ? -1 : 1) * (i * 97 % 12000));   /* peak < 12000 */
	int tone_peak = 0;
	for (int i = 0; i < 256; i++) { int a = abs(tone[i]); if (a > tone_peak) tone_peak = a; }
	/* 20 x 64 silent buffers: 20 checks, all zero */
	for (int i = 0; i < 20 * AOD_AUDIODIAG_PEAK_EVERY; i++) out(P, silent, 0);
	aod_audiodiag_stats s = aod_audiodiag_get();
	CHECK(s.outputs == 1280 && s.peak_checks == 20 && s.nonzero_checks == 0 && s.max_peak == 0, "silent flow: checks=%u nonzero=%u", s.peak_checks, s.nonzero_checks);
	/* then real PCM: every check sees the tone peak */
	for (int i = 0; i < 20 * AOD_AUDIODIAG_PEAK_EVERY; i++) out(P, tone, 0);
	s = aod_audiodiag_get();
	CHECK(s.peak_checks == 40 && s.nonzero_checks == 20 && (int)s.max_peak == tone_peak && (int)s.last_peak == tone_peak,
	      "tone flow: nonzero=%u max=%u want %d", s.nonzero_checks, s.max_peak, tone_peak);
	/* INT16_MIN does not overflow the magnitude */
	static int16_t edge[256];
	edge[255] = INT16_MIN;
	for (int i = 0; i < AOD_AUDIODIAG_PEAK_EVERY; i++) out(P, edge, 0);
	CHECK(aod_audiodiag_get().max_peak == 32768, "INT16_MIN magnitude 32768 (%u)", aod_audiodiag_get().max_peak);
	/* stereo scan covers all 256 samples of a 512-byte buffer; a mono port scans only its 128 */
	aod_audiodiag_open(1, 128, 48000, 0, P + 1);   /* mono port: 256 bytes */
	static int16_t big[512];
	big[200] = 1000;                            /* inside a stereo buffer, outside a mono one */
	unsigned nz0 = aod_audiodiag_get().nonzero_checks;
	for (int i = 0; i < AOD_AUDIODIAG_PEAK_EVERY; i++) out(P + 1, big, 0);
	CHECK(aod_audiodiag_get().nonzero_checks == nz0, "mono port: sample beyond 128 not read");
	for (int i = 0; i < AOD_AUDIODIAG_PEAK_EVERY; i++) out(P, big, 0);
	CHECK(aod_audiodiag_get().nonzero_checks == nz0 + 1 && aod_audiodiag_get().last_peak == 1000, "stereo port: sample 200 read");
	/* unaligned buffer (UBSan build checks the loads) */
	static unsigned char raw[600];
	int16_t v = -1234;
	memcpy(raw + 1 + 2 * 7, &v, 2);
	for (int i = 0; i < AOD_AUDIODIAG_PEAK_EVERY; i++) out(P, raw + 1, 0);
	CHECK(aod_audiodiag_get().last_peak == 1234, "unaligned buffer peak (%u)", aod_audiodiag_get().last_peak);
	/* unknown port / NULL buffer: never dereferenced; unknown geometry is counted separately, not as silence */
	unsigned nz1 = aod_audiodiag_get().nonzero_checks, pc1 = aod_audiodiag_get().peak_checks;
	for (int i = 0; i < AOD_AUDIODIAG_PEAK_EVERY; i++) out(7, (void *)0x10, 0);
	for (int i = 0; i < AOD_AUDIODIAG_PEAK_EVERY; i++) out(P, NULL, 0);
	s = aod_audiodiag_get();
	CHECK(s.unknown_port_checks == 1 && s.peak_checks == pc1 && s.nonzero_checks == nz1, "unknown port counted apart; NULL skipped (%u)", s.unknown_port_checks);
	/* failures */
	out(P, tone, (int)0x80260105u);
	s = aod_audiodiag_get();
	CHECK(s.output_fails == 1 && s.last_output_rc == (int)0x80260105u && strstr(last(), "sceAudioOutOutput(port 65536) -> 0x80260105"), "output failure logged");
	aod_audiodiag_open(1, 128, 44100, 1, (int)0x80260102u);
	CHECK(aod_audiodiag_get().open_fails == 1, "open failure counted");
	int vol[2] = { 32767, 32767 };
	aod_audiodiag_volume(0, 3, vol, 0);
	CHECK(strstr(last(), "sceAudioOutSetVolume(port 0, flags 0x3, vol 32767/32767) -> 0"), "volume logged (%s)", last());
	int one = 1000;   /* L only: a single-element array is valid, vol[1] must not be read */
	aod_audiodiag_volume(0, 1, &one, 0);
	CHECK(strstr(last(), "vol 1000/-1"), "L-only volume reads vol[0] only (%s)", last());
	for (int i = 0; i < 100; i++) out(0, tone, -1);
	CHECK(count("audio: ") <= AOD_AUDIODIAG_EVENT_LOG_MAX, "event log bounded (%d)", count("audio: "));
	/* where the audio thread's time goes: 3 fills of 100/200/3000 us, output times 50/150 */
	aod_audiodiag_fill_time(100); aod_audiodiag_fill_time(200); aod_audiodiag_fill_time(3000);
	aod_audiodiag_output_time(50); aod_audiodiag_output_time(150);
	s = aod_audiodiag_get();
	CHECK(s.fills == 3 && s.fill_total_us == 3300 && s.fill_max_us == 3000 && s.output_total_us == 200 && s.output_max_us == 150,
	      "timing accounting (fills %u total %llu max %u)", s.fills, s.fill_total_us, s.fill_max_us);
	aod_audiodiag_report("test");
	CHECK(strstr(last(), "fill n=3 avg=1100us max=3000us"), "fill timing reported (%s)", last());
	CHECK(strstr(last(), "outputs=") && strstr(last(), "max-peak=32768"), "report (%s)", last());
	/* stderr assembler: split writes, CRLF, empty lines, overlong split, bound */
	int before = nlog;
	aod_audiodiag_stderr_write("SL_LOGE: sles.c:checkDataSource:123 ", 36);
	CHECK(nlog == before, "no partial line logged");
	aod_audiodiag_stderr_write("bad locator\r\n\n", 14);
	CHECK(nlog == before + 1 && strcmp(last(), "[stderr] SL_LOGE: sles.c:checkDataSource:123 bad locator") == 0, "line joined across writes (%s)", last());
	char longl[700];
	memset(longl, 'x', sizeof longl - 1); longl[sizeof longl - 1] = '\n';
	aod_audiodiag_stderr_write(longl, sizeof longl);
	CHECK(nlog == before + 1 + 3, "700-char line split into 3 log lines (%d)", nlog - before - 1);
	for (int i = 0; i < 400; i++) aod_audiodiag_stderr_write("Opened Vita audio port 0\n", 25);
	s = aod_audiodiag_get();
	CHECK(s.stderr_lines == AOD_AUDIODIAG_STDERR_LINES_MAX && s.stderr_dropped > 0, "stderr lines bounded (%u, dropped %u)", s.stderr_lines, s.stderr_dropped);
	CHECK(aod_audiodiag_stderr_write("abc", 3) == 3, "writer consumes all bytes");
	printf("%d/%d checks passed (audio diag)\n", checks - fails, checks);
	return fails != 0;
}
