#ifndef AOD_TOUCH_IDS_H
#define AOD_TOUCH_IDS_H

/* Android MotionEvent pointer ids are the lowest free small integer; Vita touch report ids
 * grow per contact (0..127). Map one to the other (REVIEW-CODE F4). */
#define AOD_MAX_POINTERS 10

/* action: 0 down, 1 move, 2 up. Returns the Android pointer id, or -1 to drop the event. */
int aod_touch_map(int vita_id, int action);
void aod_touch_reset(void);

#endif
