/*
 * source/aod/configurator.c
 * AOD Input-Settings Controller — pure state machine, no Vita headers.
 * 2026-10-03
 *
 * Compile guard for test harness: AOD_INPUT_SETTINGS_HOST_TEST
 * P10: no dynamic allocation, bounded loops, small helpers (<=60 lines),
 *      checked returns, readonly constants, no eval/exec.
 * NOTE: this file relaxes P10 rule 2 (bounded loops) because the Vita
 *       main loop must run for the full interactive lifetime of the process.
 */

#include "aod/input_settings.h"
#include <stddef.h>
#include <string.h>
#include <stdint.h>

/* ── Screen bounds ───────────────────────────────────────────────────── */
#define SCREEN_W 960
#define SCREEN_H 544

/* ── Touch layout ────────────────────────────────────────────────────── */
#define TOUCH_ROW_X_MIN  48.0f
#define TOUCH_ROW_X_MAX  912.0f

static const float ROW_Y_MIN[3] = { 128.0f, 226.0f, 324.0f };
static const float ROW_Y_MAX[3] = { 208.0f, 306.0f, 404.0f };

#define TOUCH_SAVE_X_MIN   600.0f
#define TOUCH_SAVE_X_MAX   748.0f
#define TOUCH_SAVE_Y_MIN   432.0f
#define TOUCH_SAVE_Y_MAX   490.0f

#define TOUCH_CANCEL_X_MIN 764.0f
#define TOUCH_CANCEL_X_MAX 912.0f
#define TOUCH_CANCEL_Y_MIN 432.0f
#define TOUCH_CANCEL_Y_MAX 490.0f

/* ── Internal helpers ────────────────────────────────────────────────── */

/* Returns 1 if v is finite (non-NaN, non-Inf). Avoids -ffast-math issues. */
static int coord_finite(float v)
{
    uint32_t bits;
    memcpy(&bits, &v, sizeof bits);
    /* exponent all-ones => NaN or Inf */
    return (bits & 0x7F800000u) != 0x7F800000u;
}

static int coord_valid(float x, float y)
{
    if (!coord_finite(x) || !coord_finite(y)) return 0;
    if (x < 0.0f || x >= (float)SCREEN_W)     return 0;
    if (y < 0.0f || y >= (float)SCREEN_H)     return 0;
    return 1;
}

/* Returns row index 0..2 if touch lands in a mode-selection row, else -1. */
static int hit_row(float x, float y)
{
    int i;
    if (x < TOUCH_ROW_X_MIN || x >= TOUCH_ROW_X_MAX) return -1;
    for (i = 0; i < 3; i++) {
        if (y >= ROW_Y_MIN[i] && y < ROW_Y_MAX[i]) return i;
    }
    return -1;
}

static int hit_save(float x, float y)
{
    return x >= TOUCH_SAVE_X_MIN && x < TOUCH_SAVE_X_MAX &&
           y >= TOUCH_SAVE_Y_MIN && y < TOUCH_SAVE_Y_MAX;
}

static int hit_cancel(float x, float y)
{
    return x >= TOUCH_CANCEL_X_MIN && x < TOUCH_CANCEL_X_MAX &&
           y >= TOUCH_CANCEL_Y_MIN && y < TOUCH_CANCEL_Y_MAX;
}

/* Returns 1 if the given mode value is a valid enum member. */
static int mode_valid(aod_input_mode m)
{
    return m == AOD_INPUT_REAR || m == AOD_INPUT_SHOULDERS || m == AOD_INPUT_BOTH;
}

/* ── Public API ──────────────────────────────────────────────────────── */

void aod_settings_init(aod_settings_state *s, const char *path)
{
    aod_input_mode loaded;
    int rc;

    if (!s) return;

    memset(s, 0, sizeof *s);
    s->selected         = AOD_INPUT_CONFIG_DEFAULT;  /* AOD_INPUT_SHOULDERS */
    s->persisted        = AOD_INPUT_CONFIG_DEFAULT;
    s->load_error       = 0;
    s->save_error       = 0;
    s->armed            = 0;
    s->touch_down       = 0;
    s->previous_buttons = 0u;

    rc = aod_input_config_load(path, &loaded);
    if (rc == 0) {
        /* rc==0: valid file, use loaded value. */
        s->selected  = loaded;
        s->persisted = loaded;
    } else if (rc == 1) {
        /* rc==1: missing/absent file, keep safe Shoulders defaults, no error. */
    } else {
        /* rc==-1: corrupt/I/O error, keep safe Shoulders defaults, flag error. */
        s->load_error = rc;
    }
}

/* ── Private step helpers (each ≤60 non-comment lines) ──────────────── */

/* Validates inputs; resets arming/touch state on bad frame. Returns 1=ok. */
static int step_validate(aod_settings_state *s, unsigned buttons,
                          int valid, int touch_count)
{
    if (!valid || touch_count < 0 || touch_count > 6 ||
        (buttons & ~AOD_SETTINGS_BTN_ALL)) {
        s->armed            = 0;
        s->touch_down       = 0;
        s->previous_buttons = 0u;
        return 0;
    }
    return 1;
}

/* Arming gate: requires full release after startup / invalid frame.
 * Returns 1 once armed, 0 while waiting (caller must return NONE). */
static int step_arm(aod_settings_state *s, unsigned buttons, int touch_count)
{
    if (s->armed) return 1;
    if (buttons == 0u && touch_count == 0) { s->armed = 1; return 1; }
    s->previous_buttons = buttons;
    return 0;
}

/* Handles touch edge/held logic. Sets *handled=1 when step is consumed.
 * Returns the action (NONE, SAVE, or CANCEL). */
static int step_touch(aod_settings_state *s, unsigned buttons,
                       int touch_count, float x, float y, int *handled)
{
    *handled = 0;
    if (touch_count > 1) {
        /* Multi-contact: block until full release. */
        s->touch_down = 1; s->previous_buttons = buttons;
        *handled = 1; return AOD_SETTINGS_ACTION_NONE;
    }
    if (touch_count == 0) { s->touch_down = 0; return AOD_SETTINGS_ACTION_NONE; }
    /* Exactly one contact. */
    if (!coord_valid(x, y)) {
        /* Hold invalid coords: block until zero-contact release. */
        s->touch_down = 1; s->previous_buttons = buttons;
        *handled = 1; return AOD_SETTINGS_ACTION_NONE;
    }
    if (!s->touch_down) {
        /* Rising edge: process once per touch. Cancel > Save > row. */
        s->touch_down = 1;
        if (hit_cancel(x, y)) {
            s->previous_buttons = buttons;
            *handled = 1; return AOD_SETTINGS_ACTION_CANCEL;
        }
        if (hit_save(x, y)) {
            s->previous_buttons = buttons;
            *handled = 1; return AOD_SETTINGS_ACTION_SAVE;
        }
        { int row = hit_row(x, y); if (row >= 0) s->selected = (aod_input_mode)row; }
    }
    /* Held valid touch: no repeated action. */
    s->previous_buttons = buttons;
    *handled = 1; return AOD_SETTINGS_ACTION_NONE;
}

/* Handles D-pad / button rising edges. Cancel > Save > UP/DOWN. */
static int step_buttons(aod_settings_state *s, unsigned buttons)
{
    unsigned rising = buttons & ~s->previous_buttons;
    if (rising & AOD_SETTINGS_BTN_CANCEL) {
        s->previous_buttons = buttons; return AOD_SETTINGS_ACTION_CANCEL;
    }
    if (rising & AOD_SETTINGS_BTN_SAVE) {
        s->previous_buttons = buttons; return AOD_SETTINGS_ACTION_SAVE;
    }
    /* Simultaneous UP+DOWN: no change. */
    if ((rising & AOD_SETTINGS_BTN_UP) && (rising & AOD_SETTINGS_BTN_DOWN)) {
        s->previous_buttons = buttons; return AOD_SETTINGS_ACTION_NONE;
    }
    if (rising & AOD_SETTINGS_BTN_UP) {
        int v = (int)s->selected - 1; if (v < 0) v = 0;
        s->selected = (aod_input_mode)v;
    } else if (rising & AOD_SETTINGS_BTN_DOWN) {
        int v = (int)s->selected + 1; if (v > 2) v = 2;
        s->selected = (aod_input_mode)v;
    }
    s->previous_buttons = buttons; return AOD_SETTINGS_ACTION_NONE;
}

int aod_settings_step(aod_settings_state *s,
                      unsigned buttons,
                      int valid,
                      int touch_count,
                      float x,
                      float y)
{
    int action, handled;

    if (!s) return AOD_SETTINGS_ACTION_NONE;
    if (!step_validate(s, buttons, valid, touch_count))
        return AOD_SETTINGS_ACTION_NONE;
    if (!step_arm(s, buttons, touch_count))
        return AOD_SETTINGS_ACTION_NONE;

    /* Physical Cancel has highest priority — check before touch dispatch. */
    if ((buttons & ~s->previous_buttons) & AOD_SETTINGS_BTN_CANCEL) {
        s->previous_buttons = buttons;
        return AOD_SETTINGS_ACTION_CANCEL;
    }

    action = step_touch(s, buttons, touch_count, x, y, &handled);
    if (handled) return action;

    return step_buttons(s, buttons);
}

int aod_settings_save(aod_settings_state *s, const char *path)
{
    int rc;

    if (!s) return AOD_SETTINGS_ACTION_NONE;
    if (!mode_valid(s->selected)) {
        s->save_error = -1;
        /* selected remains user's pending selection; persisted unchanged. */
        return AOD_SETTINGS_ACTION_NONE;
    }

    rc = aod_input_config_save(path, s->selected);
    if (rc != 0) {
        /*
         * Save failure: selected remains user's pending selection, persisted
         * remains the old committed value, save_error is set for visible
         * reporting; caller stays in UI.
         */
        s->save_error = rc;
        return AOD_SETTINGS_ACTION_NONE;
    }

    /* Successful save: commit selection, clear error, signal EXIT. */
    s->persisted  = s->selected;
    s->save_error = 0;
    return AOD_SETTINGS_ACTION_EXIT;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Vita standalone main + adapter/render
 * Compiled only when AOD_INPUT_SETTINGS_HOST_TEST is NOT defined.
 * ═══════════════════════════════════════════════════════════════════════ */
#ifndef AOD_INPUT_SETTINGS_HOST_TEST

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>
#include <png.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* ── Paths ───────────────────────────────────────────────────────────── */
#define PANEL_PNG   "app0:sce_sys/livearea/contents/input-settings-screen.png"
#define CONFIG_PATH "ux0:data/aodd/input-controls.cfg"
#define SAVE_DIR    "ux0:data/aodd"

/* ── Module-level state (no heap) ────────────────────────────────────── */
static SceTouchPanelInfo s_panel;
static int               s_touch_ok = 0;
static GLuint            s_tex      = 0;
static int               s_png_ok   = 0;
/* Static PNG decode buffer: 960×544×4 = 2 088 960 bytes in BSS, allocated once. */
static unsigned char s_png_buf[960u * 544u * 4u];

/* ── Row geometry (mirrors controller constants) ─────────────────────── */
static const float s_row_y0[3] = { 128.0f, 226.0f, 324.0f };
static const float s_row_y1[3] = { 208.0f, 306.0f, 404.0f };

/* Background RGB(16,24,32) for error-text cover rects. */
#define BG_R (16.0f / 255.0f)
#define BG_G (24.0f / 255.0f)
#define BG_B (32.0f / 255.0f)

/* ── GL draw helpers (each ≤60 lines) ────────────────────────────────── */

static void gl_setup_ortho(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 960.0, 544.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void draw_rect_rgb(float x0, float y0, float x1, float y1,
                           float r, float g, float b)
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x1, y0);
    glVertex2f(x1, y1); glVertex2f(x0, y1);
    glEnd();
}

/* Filled-quad outline, 3 px each edge. */
static void draw_outline(float x0, float y0, float x1, float y1,
                          float r, float g, float b)
{
    draw_rect_rgb(x0,        y0,        x1,        y0 + 3.0f, r, g, b);
    draw_rect_rgb(x0,        y1 - 3.0f, x1,        y1,        r, g, b);
    draw_rect_rgb(x0,        y0,        x0 + 3.0f, y1,        r, g, b);
    draw_rect_rgb(x1 - 3.0f, y0,        x1,        y1,        r, g, b);
}

static void draw_panel_tex(void)
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s_tex);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(  0.0f,   0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(960.0f,   0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(960.0f, 544.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(  0.0f, 544.0f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

static void draw_frame(const aod_settings_state *s)
{
    int sel  = (int)s->selected;
    int pers = (int)s->persisted;
    glClear(GL_COLOR_BUFFER_BIT);
    gl_setup_ortho();
    if (s_png_ok && s_tex != 0u) {
        draw_panel_tex();
    } else {
        draw_rect_rgb(0.0f, 0.0f, 960.0f, 544.0f, BG_R, BG_G, BG_B);
    }
    /* Amber (255,176,0) outline: selected row, exact geometry x48..912. */
    if (sel >= 0 && sel <= 2) {
        draw_outline(48.0f, s_row_y0[sel], 912.0f, s_row_y1[sel],
                     255.0f / 255.0f, 176.0f / 255.0f, 0.0f / 255.0f);
    }
    /* Cyan (0,220,220) persisted square x883..899, y(row_y0+28)..(row_y0+44). */
    if (pers >= 0 && pers <= 2) {
        draw_rect_rgb(883.0f, s_row_y0[pers] + 28.0f,
                      899.0f, s_row_y0[pers] + 44.0f,
                      0.0f / 255.0f, 220.0f / 255.0f, 220.0f / 255.0f);
    }
    /* Opaque bg covers static PNG error text when no active error. */
    /* Load error text: y=496..518 */
    if (!s->load_error) {
        draw_rect_rgb(48.0f, 496.0f, 912.0f, 518.0f, BG_R, BG_G, BG_B);
    }
    /* Save error text: y=518..544 (clears font descenders; max bottom 542 inclusive) */
    if (!s->save_error) {
        draw_rect_rgb(48.0f, 518.0f, 912.0f, 544.0f, BG_R, BG_G, BG_B);
    }
}

/* ── PNG loader ──────────────────────────────────────────────────────── */

static int load_png_tex(const char *path)
{
    png_image img;
    GLuint tex = 0;
    GLenum gl_err;

    memset(&img, 0, sizeof img);
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&img, path)) return -1;
    img.format = PNG_FORMAT_RGBA;
    if (img.width != 960u || img.height != 544u) {
        png_image_free(&img);
        return -2;
    }
    /* row_stride=0: libpng uses minimum tightly-packed stride. */
    if (!png_image_finish_read(&img, NULL, s_png_buf, 0, NULL)) {
        png_image_free(&img);
        return -3;
    }
    png_image_free(&img); /* always free after finish_read, including success */
    glGenTextures(1, &tex);
    if (tex == 0) { return -4; } /* driver rejected allocation */
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 960, 544, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, s_png_buf);
    gl_err = glGetError();
    if (gl_err != GL_NO_ERROR) { glDeleteTextures(1, &tex); return -5; }
    s_tex = tex;
    return 0;
}

/* ── Controller read ─────────────────────────────────────────────────── */

static unsigned read_ctrl(int *ctrl_valid)
{
    SceCtrlData pad;
    int rc;
    unsigned raw, out;
    memset(&pad, 0, sizeof pad);
    *ctrl_valid = 0;
    rc = sceCtrlPeekBufferPositive(0, &pad, 1);
    if (rc != 1) return 0u;
    *ctrl_valid = 1;
    raw = pad.buttons;
    out = 0u;
    if (raw & SCE_CTRL_UP)     out |= AOD_SETTINGS_BTN_UP;
    if (raw & SCE_CTRL_DOWN)   out |= AOD_SETTINGS_BTN_DOWN;
    if (raw & SCE_CTRL_CROSS)  out |= AOD_SETTINGS_BTN_SAVE;
    if (raw & SCE_CTRL_CIRCLE) out |= AOD_SETTINGS_BTN_CANCEL;
    return out;
}

/* ── Front-touch read + normalization ────────────────────────────────── */

typedef struct { int count; float x; float y; } touch_result_t;

/* Returns count=0 (no touch), 1 (single), 2-6 (multi), or 7 (invalid frame).
 * x,y normalized to 0..959, 0..543 for single in-range touch, or -1/-1 for
 * single out-of-range touch (controller marks held_invalid). */
static touch_result_t read_touch(void)
{
    touch_result_t r = {0, 0.0f, 0.0f};
    SceTouchData td;
    int rc, rn, i, j, mn_x, mn_y, mx_x, mx_y, rx, ry;
    if (!s_touch_ok) return r;
    memset(&td, 0, sizeof td);
    rc = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1);
    if (rc != 1) { r.count = 7; return r; } /* failed read → invalid frame, force re-arm */
    rn = (int)td.reportNum;
    if (rn < 0 || rn > 6) { r.count = 7; return r; }
    for (i = 0; i < rn; i++) {
        for (j = 0; j < i; j++) {
            if (td.report[j].id == td.report[i].id) { r.count = 7; return r; }
        }
    }
    r.count = rn;
    if (rn == 1) {
        mn_x = (int)(s_panel.minAaX); mx_x = (int)(s_panel.maxAaX);
        mn_y = (int)(s_panel.minAaY); mx_y = (int)(s_panel.maxAaY);
        rx   = (int)(td.report[0].x);
        ry   = (int)(td.report[0].y);
        if (rx >= mn_x && rx <= mx_x && ry >= mn_y && ry <= mx_y) {
            r.x = (float)(rx - mn_x) * 959.0f / (float)(mx_x - mn_x);
            r.y = (float)(ry - mn_y) * 543.0f / (float)(mx_y - mn_y);
        } else {
            /* Out-of-range: tell controller this contact is held_invalid. */
            r.x = -1.0f;
            r.y = -1.0f;
        }
    }
    return r;
}

/* ── Checked process exit helper ─────────────────────────────────────── */

/* Calls sceKernelExitProcess(code). If the call itself returns a negative
 * value (unexpected), logs it and returns 1; otherwise returns 0. */
static int checked_exit(int code)
{
    int rc = sceKernelExitProcess(code);
    if (rc < 0) {
        fprintf(stderr, "sceKernelExitProcess: 0x%08X\n", (unsigned)rc);
        return 1;
    }
    return 0;
}

/* ── Save helper (mkdir then persist) ────────────────────────────────── */

static int do_save(aod_settings_state *s)
{
    struct stat sb;
    int rc = mkdir(SAVE_DIR, 0755);
    if (rc != 0 && errno != EEXIST) {
        s->save_error = -1;
        return AOD_SETTINGS_ACTION_NONE;
    }
    if (rc != 0) { /* errno == EEXIST: verify it is actually a directory */
        memset(&sb, 0, sizeof sb);
        if (stat(SAVE_DIR, &sb) != 0 || !S_ISDIR(sb.st_mode)) {
            s->save_error = -1;
            return AOD_SETTINGS_ACTION_NONE;
        }
    }
    /* Directory verified; delegate to controller save (returns EXIT on success). */
    return aod_settings_save(s, CONFIG_PATH);
}

/* ── Control + touch initialisation helper ───────────────────────────── */

/* Initialises digital controller sampling and front-touch sampling.
 * Sets s_touch_ok if the touch panel reports valid calibration data.
 * Returns 0 on success, -1 if the controller sampling call fails. */
static int init_controls(void)
{
    int rc = sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    if (rc < 0) return -1;

    rc = sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,
                                  SCE_TOUCH_SAMPLING_STATE_START);
    if (rc >= 0) {
        memset(&s_panel, 0, sizeof s_panel);
        if (sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &s_panel) >= 0 &&
            (int)s_panel.maxAaX > (int)s_panel.minAaX &&
            (int)s_panel.maxAaY > (int)s_panel.minAaY) {
            s_touch_ok = 1;
        }
        /* If panel info invalid: s_touch_ok stays 0; physical controls still work. */
    }
    return 0;
}

/* ── Entry point ─────────────────────────────────────────────────────── */

int main(void)
{
    aod_settings_state state;
    int running, action, ctrl_valid;
    unsigned buttons;
    touch_result_t t;

    /* The pinned VitaGL initializer returns a resolution-fallback flag:
     * GL_FALSE for the requested resolution, GL_TRUE when clamped.
     * It is not an initialization success/error status. This standalone
     * process calls it once with fixed parameters and ignores that flag. */
    (void)vglInitExtended(4 * 1024 * 1024, 960, 544, 4 * 1024 * 1024,
                          SCE_GXM_MULTISAMPLE_NONE);

    if (init_controls() != 0)
        return checked_exit(1);

    if (load_png_tex(PANEL_PNG) != 0) {
        /* PNG is required for the UI; exit before loop to avoid blank invisible actions. */
        return checked_exit(1);
    }
    s_png_ok = 1;

    aod_settings_init(&state, CONFIG_PATH);

    running = 1;
    while (running) { /* P10 loop exception: interactive process lifetime — see file header NOTE */
        buttons = read_ctrl(&ctrl_valid);
        t       = read_touch();
        action  = aod_settings_step(&state, buttons, ctrl_valid,
                                    t.count, t.x, t.y);
        if (action == AOD_SETTINGS_ACTION_SAVE) {
            action = do_save(&state);
            if (action == AOD_SETTINGS_ACTION_EXIT) running = 0;
        } else if (action == AOD_SETTINGS_ACTION_CANCEL) {
            running = 0; /* exit without any file writes or mkdir */
        }
        draw_frame(&state);
        vglSwapBuffers(GL_TRUE); /* vsync: caps frame rate at 60 Hz */
    }

    if (s_tex != 0u) glDeleteTextures(1, &s_tex);
    /* vglEnd does not exist in vitaGL; sceKernelExitProcess reclaims all
     * graphics and process allocations. */
    return checked_exit(0);
}

#endif /* !AOD_INPUT_SETTINGS_HOST_TEST */