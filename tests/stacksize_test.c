/* aod-vita: bionic stack-size semantics over pthread-embedded (boot-13 BGM). Host test.
 *
 * Replays FMOD Ex 4.44.31's FMOD_OS_Thread_Create (libfmodex 0xadfdc):
 *   attr_init; setdetachstate; if (req) setstacksize(max(req, 8192)); create; destroy;
 *   any non-zero -> FMOD_ERR_INTERNAL (33)
 * against a model of pthread-embedded's setstacksize (EINVAL below 32768, linked code), once
 * through the old pass-through bridge and once through aod_bionic_stacksize. */
#include <stdio.h>
#include "aod/stacksize.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static size_t pte_attr_size;
static int pte_setstacksize(size_t s) { if (s < 32768) return EINVAL; pte_attr_size = s; return 0; }

static int bridge_old(size_t s) { return pte_setstacksize(s); }
static int bridge_new(size_t s) {
	size_t p;
	int r = aod_bionic_stacksize(s, &p);
	return r ? r : pte_setstacksize(p);
}

/* FMOD's thread create, reduced to the stack-size step (the other steps succeed on both bridges). */
static int fmod_thread_create(int (*setstacksize)(size_t), unsigned req) {
	if (req) {
		if (setstacksize(req < 8192 ? 8192 : req)) return 33;
	}
	return 0;
}

int main(void) {
	int checks = 0;
	/* calibration: the old bridge reproduces the emulator failure for the file thread (8192) */
	CHECK(fmod_thread_create(bridge_old, 8192) == 33); checks++;
	/* ... and accepts the stream/nonblocking (65536) and mixer (49152) defaults, as observed (SFX OK) */
	CHECK(fmod_thread_create(bridge_old, 65536) == 0); checks++;
	CHECK(fmod_thread_create(bridge_old, 49152) == 0); checks++;

	/* the fix: every FMOD thread request succeeds */
	unsigned reqs[] = { 8192, 16384, 49152, 65536, 0 };
	for (unsigned i = 0; i < sizeof reqs / sizeof *reqs; i++) {
		pte_attr_size = 0;
		CHECK(fmod_thread_create(bridge_new, reqs[i]) == 0); checks++;
		if (reqs[i]) { CHECK(pte_attr_size == (reqs[i] < 32768 ? 32768 : reqs[i])); checks++; }
	}

	/* bionic semantics kept: EINVAL below PTHREAD_STACK_MIN (8192), never a success stub */
	size_t p = 12345;
	CHECK(aod_bionic_stacksize(0, &p) == EINVAL && p == 12345); checks++;
	CHECK(aod_bionic_stacksize(8191, &p) == EINVAL && p == 12345); checks++;
	CHECK(aod_bionic_stacksize(8192, &p) == 0 && p == 32768); checks++;
	CHECK(aod_bionic_stacksize(32767, &p) == 0 && p == 32768); checks++;
	CHECK(aod_bionic_stacksize(32768, &p) == 0 && p == 32768); checks++;
	CHECK(aod_bionic_stacksize(32769, &p) == 0 && p == 32769); checks++;
	CHECK(aod_bionic_stacksize(1u << 20, &p) == 0 && p == 1u << 20); checks++;
	CHECK(bridge_new(4096) == EINVAL); checks++;

	printf("stacksize: %d/%d passed\n", checks - fails, checks);
	return fails != 0;
}
