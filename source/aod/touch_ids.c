/*
 * aod-vita: Vita touch report id -> Android pointer id.
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#include "aod/touch_ids.h"

static int slot_owner[AOD_MAX_POINTERS];   /* vita id + 1, 0 = free */

void aod_touch_reset(void) {
	for (int i = 0; i < AOD_MAX_POINTERS; i++) slot_owner[i] = 0;
}

static int find(int vita_id) {
	for (int i = 0; i < AOD_MAX_POINTERS; i++)
		if (slot_owner[i] == vita_id + 1) return i;
	return -1;
}

int aod_touch_map(int vita_id, int action) {
	int slot = find(vita_id);
	if (action == 0 && slot < 0) {
		for (int i = 0; i < AOD_MAX_POINTERS; i++)
			if (!slot_owner[i]) { slot_owner[i] = vita_id + 1; return i; }
		return -1;   /* more contacts than Android would report */
	}
	if (action == 2 && slot >= 0) slot_owner[slot] = 0;
	return slot;     /* move/up of an unknown contact: -1, dropped */
}
