/* Host test for source/aod/touch_ids.c (REVIEW-CODE F4). */
#include <stdio.h>
#include "aod/touch_ids.h"
static int fails, checks;
#define CHECK(c, m) do { checks++; if (!(c)) { fails++; printf("FAIL %s\n", m); } } while (0)
int main(void) {
	aod_touch_reset();
	CHECK(aod_touch_map(57, 0) == 0, "first contact -> 0");
	CHECK(aod_touch_map(58, 0) == 1, "second contact -> 1");
	CHECK(aod_touch_map(57, 1) == 0, "move keeps id");
	CHECK(aod_touch_map(57, 2) == 0, "up reports id");
	CHECK(aod_touch_map(57, 1) == -1, "move after up dropped");
	CHECK(aod_touch_map(90, 0) == 0, "lowest free slot reused");
	CHECK(aod_touch_map(58, 2) == 1, "second up");
	for (int i = 0; i < 9; i++) aod_touch_map(100 + i, 0);
	CHECK(aod_touch_map(127, 0) == -1, "11th contact dropped");
	CHECK(aod_touch_map(99, 2) == -1, "unknown up dropped");
	for (int id = 0; id < 128; id++) { int r = aod_touch_map(id, 0); CHECK(r >= -1 && r < AOD_MAX_POINTERS, "bounded"); }
	printf("%d/%d checks passed\n", checks - fails, checks);
	return fails != 0;
}
