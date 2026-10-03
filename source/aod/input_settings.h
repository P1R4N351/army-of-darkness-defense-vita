#ifndef AOD_INPUT_SETTINGS_H
#define AOD_INPUT_SETTINGS_H

#include "aod/input_config.h"

/* ── Settings button constants ───────────────────────────────────────── */
#define AOD_SETTINGS_BTN_UP     1u
#define AOD_SETTINGS_BTN_DOWN   2u
#define AOD_SETTINGS_BTN_SAVE   4u
#define AOD_SETTINGS_BTN_CANCEL 8u
#define AOD_SETTINGS_BTN_ALL    (AOD_SETTINGS_BTN_UP | AOD_SETTINGS_BTN_DOWN | \
                                  AOD_SETTINGS_BTN_SAVE | AOD_SETTINGS_BTN_CANCEL)

/* ── Settings action constants ───────────────────────────────────────── */
#define AOD_SETTINGS_ACTION_NONE   0
#define AOD_SETTINGS_ACTION_SAVE   1
#define AOD_SETTINGS_ACTION_CANCEL 2
#define AOD_SETTINGS_ACTION_EXIT   3

/* ── Public state ────────────────────────────────────────────────────── */
typedef struct {
    aod_input_mode  selected;
    aod_input_mode  persisted;
    int             load_error;
    int             save_error;
    int             armed;
    int             touch_down;
    unsigned        previous_buttons;
} aod_settings_state;

/* ── Public API ──────────────────────────────────────────────────────── */
void aod_settings_init(aod_settings_state *s, const char *path);

int aod_settings_step(aod_settings_state *s,
                      unsigned buttons,
                      int valid,
                      int touch_count,
                      float x,
                      float y);

int aod_settings_save(aod_settings_state *s, const char *path);

#endif /* AOD_INPUT_SETTINGS_H */