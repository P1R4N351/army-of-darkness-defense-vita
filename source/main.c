/*
 * aod-vita: Army of Darkness Defense 1.1.1 (Android) on PS Vita.
 *
 * Reproduces the Android lifecycle of BFSEngineActivity/BFSEngine (see REVIEW-STATIC.md §2):
 *   JNI_OnLoad -> nativeStart -> Application.onApplicationLaunch -> nativeCreate(w,h)
 *   -> every frame: nativeUpdate, then queued touch/key/window events, then swap.
 *
 * Based on soloader-boilerplate (MIT). This file is distributed under the MIT license.
 */

#include "utils/init.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/dialog.h"

#include "aod/jni_bridge.h"
#include "aod/trophies.h"
#include "aod/port.h"
#include "aod/touch_ids.h"
#include "aod/uniform_remap.h"
#include "aod/gl_diag.h"
#include "aod/audio_diag.h"
#include "aod/fmod_diag.h"
#include "aod/opensl_compat.h"
#include "reimpl/controls.h"

#include "aod/input_queue.h"
#include "aod/input_launch.h"
#include "aod/input_config.h"
#include "aod/input_overlay.h"
#include "aod/input.h"

#include <psp2/appmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>
#include <vitaGL.h>

#include <pthread.h>
#include <string.h>


int _newlib_heap_size_user = 256 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif

so_module aod_game_mod;
so_module aod_fmod_mod;

extern volatile int aod_exit_requested;
int aod_alert_active(void);
void aod_alert_poll(void);

#define SCREEN_W 960
#define SCREEN_H 544

/* Java BFSEngine event queue: filled by input polling, drained after nativeUpdate(). */
static aod_input_queue queue;

static int64_t uptime_ms(void) { return (int64_t)(sceKernelGetProcessTimeWide() / 1000); }

static bool emit_cb(void *userdata, const aod_event *event) {
    aod_input_queue *q = (aod_input_queue *)userdata;
    return aod_input_queue_emit(q, event, uptime_ms());
}

/* Android KeyEvent -> BFSEngine: KEYCODE_BACK(4) -> 0, KEYCODE_MENU(82) -> 1. */
void controls_handler_key(int32_t keycode, ControlsAction action) {
    if (action == CONTROLS_ACTION_MOVE) return;
    int id;
    if (keycode == AKEYCODE_BUTTON_B || keycode == AKEYCODE_BUTTON_SELECT) id = 0;  /* Circle/Select = Back */
    else if (keycode == AKEYCODE_BUTTON_START) id = 1;                              /* Start = Menu */
    else return;
    int type = (action == CONTROLS_ACTION_DOWN) ? 0 : 1;
    aod_input_event ev = { AOD_KIND_KEY, type, id, 0.0f, 0.0f, uptime_ms() };
    bool ok = aod_input_queue_push(&queue, &ev);
    (void)ok; /* void compat only; actual adapter uses bool callback; rejection noted */
}

/* MotionEvent: 0 Down, 1 Move, 2 Up. Coordinates are surface pixels. */
void controls_handler_touch(int32_t id, float x, float y, ControlsAction action) {
    int atype;
    if (action == CONTROLS_ACTION_DOWN) atype = AOD_TOUCH_DOWN;
    else if (action == CONTROLS_ACTION_UP) atype = AOD_TOUCH_UP;
    else if (action == CONTROLS_ACTION_MOVE) atype = AOD_TOUCH_MOVE;
    else return;
    bool ok = aod_input_queue_touch(&queue, (int)id, atype, x, y, uptime_ms());
    (void)ok;
}

void controls_handler_analog(ControlsStickId which, float x, float y, ControlsAction action) {}

typedef void (*native_v)(JNIEnv *, jclass);
typedef void (*native_ii)(JNIEnv *, jclass, jint, jint);
typedef void (*native_touch)(JNIEnv *, jclass, jint, jint, jfloat, jfloat, jlong);
typedef void (*native_key)(JNIEnv *, jclass, jint, jint, jlong);
typedef void (*native_z)(JNIEnv *, jclass, jboolean);

static void *need(const char *name) {
	void *p = (void *)so_symbol(&aod_game_mod, name);
	if (!p) {
		l_fatal("libgame.so has no export %s", name);
		fatal_error("libgame.so is missing %s. Wrong game version? This port needs 1.1.1.", name);
	}
	return p;
}

#define NB "Java_com_backflipstudios_bf_1core_jni_NativeBridge_"
#define APP "Java_com_backflipstudios_bf_1core_jni_Application_"

/* DIAGNOSTIC E9: two frames, each a green colour clear and a swap, sampled at the display hand-off */
static void gldiag_preflight(unsigned long tag) {
	for (unsigned long i = 0; i < 2; i++) {
		aod_gldiag_frame_begin(tag + i);
		aod_gldiag_preflight_clear();
		aod_gldiag_before_swap(tag + i);
		vglSwapBuffers(GL_FALSE);
		aod_gldiag_frame_end(tag + i);
	}
	l_info("DIAG preflight f%lu..f%lu: green clear + swap done; vitaGL free/total KiB: vram %u/%u ram %u/%u phycont %u/%u cdlg %u/%u", tag, tag + 1,
	       (unsigned)(vglMemFree(VGL_MEM_VRAM) >> 10), (unsigned)(vglMemTotal(VGL_MEM_VRAM) >> 10),
	       (unsigned)(vglMemFree(VGL_MEM_RAM) >> 10), (unsigned)(vglMemTotal(VGL_MEM_RAM) >> 10),
	       (unsigned)(vglMemFree(VGL_MEM_SLOW) >> 10), (unsigned)(vglMemTotal(VGL_MEM_SLOW) >> 10),
	       (unsigned)(vglMemFree(VGL_MEM_BUDGET) >> 10), (unsigned)(vglMemTotal(VGL_MEM_BUDGET) >> 10));
}

/* SDK libOpenSLES (Vita.o): audioThread opens its BGM port at _opensles_user_freq when > 0 (a weak
 * reference the port defines), and SDL_open(IEngine *) restarts that thread and port. */
int _opensles_user_freq = 0;
void SDL_open(void *sdk_engine);

int main(void) {
    /* Cold startup: launch settings check before any game assets or SDK init. */
    {
        int li_rc = aod_input_launch_init();
        if (li_rc != 0) l_info("input_launch_init: rc=%d", li_rc);
        int lp_rc = aod_input_launch_poll();
        if (lp_rc == 1) { sceKernelExitProcess(0); return 0; }
        if (lp_rc == -1) { sceKernelExitProcess(1); return 1; }
    }
	aod_audiodiag_install_stderr();   /* libOpenSLES reports its errors only on stderr */
	/* FMOD dlsym()s slCreateEngine during nativeCreate; the port's proxy needs the SDK entry point and
	 * the SDK's interface-ID values (not the addresses dynlib exports for FMOD to dereference). */
	aod_opensl_set_backend(slCreateEngine, SL_IID_ENGINE, SL_IID_ANDROIDCONFIGURATION);
	aod_opensl_set_output_hooks(SDL_open, &_opensles_user_freq);   /* output rate follows FMOD's player (boot-12) */
	soloader_init_all();
    /* Initialize input queue and load config; controls_init already called by soloader_init_all. */
    aod_input_queue_init(&queue);
    {
        aod_input_mode cfg_mode = AOD_INPUT_SHOULDERS;
        int cfg_rc = aod_input_config_load("ux0:data/aodd/input-controls.cfg", &cfg_mode);
        if (cfg_rc == 1) {
            cfg_mode = AOD_INPUT_SHOULDERS; /* missing: default Shoulders */
        } else if (cfg_rc == -1) {
            /* corrupt/error: retain default, l_info diagnostic, no file writes */
            l_info("input_config_load: corrupt or error, retaining default mode %d", (int)cfg_mode);
            cfg_mode = AOD_INPUT_SHOULDERS;
        }
        bool cc = controls_configure(cfg_mode, (float)SCREEN_W, (float)SCREEN_H, emit_cb, &queue);
        if (!cc) l_info("controls_configure: failed; controls unavailable this session");
    }
	aod_jni_install();

	native_v nativeStart = need(NB "nativeStart");
	native_ii nativeCreate = need(NB "nativeCreate");
	native_v nativeUpdate = need(NB "nativeUpdate");
	native_touch nativeOnTouchEvent = need(NB "nativeOnTouchEvent");
	native_key nativeOnKeyEvent = need(NB "nativeOnKeyEvent");
	native_z nativeOnWindowEvent = need(NB "nativeOnWindowEvent");
	native_v onApplicationLaunch = need(APP "onApplicationLaunch");
	int (*JNI_OnLoad)(JavaVM *, void *) = need("JNI_OnLoad");

	jclass bridge = (*(&jni))->FindClass(&jni, "com/backflipstudios/bf_core/jni/NativeBridge");
	jclass app = (*(&jni))->FindClass(&jni, "com/backflipstudios/bf_core/jni/Application");

	gl_init();
	aod_trophies_init(); /* optional setup before any game lifecycle */
	aod_jni_set_surface(SCREEN_W, SCREEN_H);
	if (aod_gldiag_enabled())
		gldiag_preflight(AOD_DIAG_PREFLIGHT_P1);   /* opt-in DIAGNOSTIC E9: GL clear before any game code */

	l_info("lifecycle: JNI_OnLoad");
	int ver = JNI_OnLoad(&jvm, NULL);
	l_info("lifecycle: JNI_OnLoad -> 0x%x; nativeStart", ver);
	nativeStart(&jni, bridge);
	l_info("lifecycle: Application.onApplicationLaunch");
	onApplicationLaunch(&jni, app);
	aod_jni_run_deferred();
	l_info("lifecycle: nativeCreate(%d, %d)", SCREEN_W, SCREEN_H);
	nativeCreate(&jni, bridge, SCREEN_W, SCREEN_H);
	aod_audiodiag_report("after nativeCreate");
	aod_fmoddiag_report("after nativeCreate");
	if (aod_gldiag_enabled())
		gldiag_preflight(AOD_DIAG_PREFLIGHT_P2);   /* opt-in DIAGNOSTIC E9: GL clear after the game's GL init */
	/* Activity.onWindowFocusChanged(true) is queued before the first frame. */
    {
        aod_input_event wev = { AOD_KIND_WINDOW, 1, 0, 0.0f, 0.0f, 0 };
        bool wq = aod_input_queue_push(&queue, &wev);
        (void)wq;
    }
	l_info("lifecycle: entering frame loop");

	/* Bounded frame diagnostics (boot-03 showed "first frame presented" then silence): the first
	 * three frames log update/swap boundaries, then sparse heartbeats; nothing per frame after. */
	unsigned long frame = 0;
	while (!aod_exit_requested) {
		int trace = frame < 3;
		aod_gldiag_frame_begin(frame);   /* DIAGNOSTIC build */
        /* Per-frame: bounded one settings launch event (no substring). */
        {
            int lp = aod_input_launch_poll();
            if (lp == 1 || lp == -1) {
                int exit_status = (lp == 1) ? 0 : 1;
                bool cc = controls_cancel(); (void)cc;
                /* LoadExec on lp==1 normally does not return; terminate if returned. */
                sceKernelExitProcess(exit_status);
                return exit_status;
            }
        }
        /* Focus / resume: poll SDK once per frame; ON_RESUME flags inactive this frame. */
        {
            SceAppMgrSystemEvent sys_ev;
            sceClibMemset(&sys_ev, 0, sizeof(sys_ev));
            bool this_frame_inactive = false;
            int sys_rc = sceAppMgrReceiveSystemEvent(&sys_ev);
            if (sys_rc == 0 && sys_ev.systemEvent == SCE_APPMGR_SYSTEMEVENT_ON_RESUME) {
                this_frame_inactive = true;
                bool cc = controls_cancel(); (void)cc;
            }
            bool want_active = !this_frame_inactive && !aod_alert_active();
            controls_set_active(want_active);
        }
        {
            bool poll_ok = controls_poll_checked();
            (void)poll_ok; /* rejection retained pending retry */
        }
		if (trace) l_info("frame %lu: nativeUpdate begin", frame);
		nativeUpdate(&jni, bridge);
		if (trace) l_info("frame %lu: nativeUpdate end", frame);
        {
            unsigned ev_count = aod_input_queue_count(&queue);
            for (unsigned i = 0; i < ev_count; i++) {
                const aod_input_event *e = aod_input_queue_at(&queue, i);
                if (!e) break;
                if (e->kind == AOD_KIND_TOUCH) nativeOnTouchEvent(&jni, bridge, e->type, e->id, e->x, e->y, e->t);
                else if (e->kind == AOD_KIND_KEY) nativeOnKeyEvent(&jni, bridge, e->type, e->id, e->t);
                else if (e->kind == AOD_KIND_WINDOW) nativeOnWindowEvent(&jni, bridge, e->type ? JNI_TRUE : JNI_FALSE);
            }
            aod_input_queue_clear(&queue);
        }
		aod_jni_run_deferred();
		aod_alert_poll();
		if (trace) l_info("frame %lu: swap begin", frame);
        if (!aod_alert_active()) {
            float ptr_x = 0.0f, ptr_y = 0.0f;
            if (controls_pointer(&ptr_x, &ptr_y)) {
                bool od = aod_input_overlay_draw((float)SCREEN_W, (float)SCREEN_H, ptr_x, ptr_y);
                (void)od;
            }
        }
		aod_gldiag_before_swap(frame);   /* DIAGNOSTIC: tags the display-queue entry this swap creates */
		vglSwapBuffers(aod_alert_active() ? GL_TRUE : GL_FALSE);
		if (trace) l_info("frame %lu: swap end", frame);
		aod_gldiag_frame_end(frame);     /* DIAGNOSTIC: logs counters and display-queue evidence */
		if (frame == 0) l_info("lifecycle: first frame swap submitted (not proof of display)");
		if (frame == 60 || frame == 300 || frame == 1800) {
			l_info("heartbeat: frame %lu swap submitted at %.1f s", frame, sceKernelGetProcessTimeWide() / 1000000.0);
			aod_audiodiag_report("at heartbeat");
			aod_fmoddiag_report("at heartbeat");
		}
		if (frame % 1800 == 0) { aod_jni_log_stats(); aod_uremap_log_stats(); }
		frame++;
	}

	l_info("lifecycle: exit requested");
	aod_jni_log_stats();
	aod_trophies_stop();
	sceKernelExitProcess(0);
	return 0;
}
