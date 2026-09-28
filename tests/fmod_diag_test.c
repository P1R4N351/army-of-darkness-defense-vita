/* Host test for source/aod/fmod_diag.c (FMOD boundary evidence): counters, timing, bounded logs. */
#include "aod/fmod_diag.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static char logs[400][300]; static int nlog;
void aod_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(logs[nlog % 400], 300, fmt, ap); va_end(ap); if (getenv("VERBOSE")) puts(logs[nlog % 400]); nlog++; }
static const char *last(void) { return logs[(nlog - 1) % 400]; }
static int count(const char *s) { int n = 0; for (int i = 0; i < nlog && i < 400; i++) n += strstr(logs[i], s) != NULL; return n; }

int main(void) {
	aod_fmoddiag_create(AOD_FMOD_STREAM, "assets/assets.archondb/loop-rox.ogg", 0x80, 0, 1234);
	CHECK(strstr(last(), "createStream(\"loop-rox.ogg\", mode 0x80) -> 0 in 1.234 ms"), "stream logged with basename (%s)", last());
	aod_fmoddiag_create(AOD_FMOD_SOUND, "whinny-3.ogg", 0x200, 0, 80000);
	CHECK(strstr(last(), "createSound(\"whinny-3.ogg\", mode 0x200) -> 0 in 80.000 ms (SLOW)"), "slow create flagged (%s)", last());
	aod_fmoddiag_create(AOD_FMOD_SOUND, NULL, 0, 23, 10);
	CHECK(strstr(last(), "createSound(\"(null)\", mode 0) -> 23"), "failure and NULL name");
	for (int i = 0; i < 100; i++) aod_fmoddiag_create(AOD_FMOD_SOUND, "gallop.ogg", 0, 0, 1000);
	CHECK(count("createSound(\"gallop.ogg\"") <= AOD_FMODDIAG_CREATE_LOG_MAX, "fast creates bounded");
	int before = nlog;
	aod_fmoddiag_create(AOD_FMOD_SOUND, "late-slow.ogg", 0, 0, 60000);
	aod_fmoddiag_create(AOD_FMOD_STREAM, "in_game_1.ogg", 0x80, 0, 2000);
	CHECK(nlog == before + 2, "slow creates and streams still logged after the create cap");
	for (int i = 0; i < 40; i++) aod_fmoddiag_create(AOD_FMOD_STREAM, "s.ogg", 0, 0, 1);
	CHECK(count("createStream(") <= AOD_FMODDIAG_STREAM_LOG_MAX + 1, "streams bounded (%d)", count("createStream("));
	aod_fmoddiag_stats s = aod_fmoddiag_get();
	CHECK(s.sounds == 103 && s.streams == 42 && s.create_fails == 1 && s.slow_creates == 2 && s.max_create_us == 80000 &&
	      s.sound_total_us == 1234ull * 0 + 80000 + 10 + 100 * 1000 + 60000, "counters (sounds %u streams %u total %llu)", s.sounds, s.streams, s.sound_total_us);
	aod_fmoddiag_play(0, 1); aod_fmoddiag_play(36, 0);
	aod_fmoddiag_group_volume((void *)0x10, 0.0f); aod_fmoddiag_group_volume((void *)0x10, 0.75f);
	aod_fmoddiag_channel_volume((void *)0x20, 0.0f); aod_fmoddiag_group_paused((void *)0x10, 1);
	CHECK(strstr(logs[(nlog - 3) % 400], "ChannelGroup 0x10 setVolume(0.750)"), "group volume logged");
	s = aod_fmoddiag_get();
	CHECK(s.plays == 2 && s.play_fails == 1 && s.zero_volume_sets == 2 && s.group_pauses == 1, "play/volume/pause counters");
	aod_fmoddiag_report("test");
	CHECK(strstr(last(), "createStream n=42") && strstr(last(), "playSound n=2 (fail 1)") && strstr(last(), "zero-volume sets=2"), "report (%s)", last());
	for (int i = 0; i < 200; i++) aod_fmoddiag_channel_volume((void *)0x20, 0.5f);
	CHECK(count("setVolume(") <= AOD_FMODDIAG_EVENT_LOG_MAX, "event lines bounded");
	printf("%d/%d checks passed (fmod diag)\n", checks - fails, checks);
	return fails != 0;
}
