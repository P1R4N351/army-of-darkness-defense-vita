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
typedef struct { int kind; int type, id; float x, y; int64_t t; } input_event;
enum { EV_TOUCH, EV_KEY, EV_WINDOW };
#define MAX_EVENTS 256
static input_event events[MAX_EVENTS];
static int n_events;

static int64_t uptime_ms(void) { return (int64_t)(sceKernelGetProcessTimeWide() / 1000); }

static void push_event(input_event e) {
	if (n_events < MAX_EVENTS) events[n_events++] = e;
}

/* Android KeyEvent -> BFSEngine: KEYCODE_BACK(4) -> 0, KEYCODE_MENU(82) -> 1. */
void controls_handler_key(int32_t keycode, ControlsAction action) {
	int code;
	if (keycode == AKEYCODE_BUTTON_B || keycode == AKEYCODE_BUTTON_SELECT) code = 0;        /* Circle/Select = Back */
	else if (keycode == AKEYCODE_BUTTON_START) code = 1;                                    /* Start = Menu */
	else return;
	if (action == CONTROLS_ACTION_MOVE) return;
	push_event((input_event){ EV_KEY, action == CONTROLS_ACTION_DOWN ? 0 : 1, code, 0, 0, uptime_ms() });
}

/* MotionEvent: 0 Down, 1 Move, 2 Up. Coordinates are surface pixels. */
void controls_handler_touch(int32_t id, float x, float y, ControlsAction action) {
	int type = action == CONTROLS_ACTION_DOWN ? 0 : action == CONTROLS_ACTION_UP ? 2 : 1;
	int pointer = aod_touch_map(id, type);
	if (pointer < 0) return;
	push_event((input_event){ EV_TOUCH, type, pointer, x, y, uptime_ms() });
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
	aod_audiodiag_install_stderr();   /* libOpenSLES reports its errors only on stderr */
	/* FMOD dlsym()s slCreateEngine during nativeCreate; the port's proxy needs the SDK entry point and
	 * the SDK's interface-ID values (not the addresses dynlib exports for FMOD to dereference). */
	aod_opensl_set_backend(slCreateEngine, SL_IID_ENGINE, SL_IID_ANDROIDCONFIGURATION);
	aod_opensl_set_output_hooks(SDL_open, &_opensles_user_freq);   /* output rate follows FMOD's player (boot-12) */
	soloader_init_all();
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
	push_event((input_event){ EV_WINDOW, 1, 0, 0, 0, 0 });
	l_info("lifecycle: entering frame loop");

	/* Bounded frame diagnostics (boot-03 showed "first frame presented" then silence): the first
	 * three frames log update/swap boundaries, then sparse heartbeats; nothing per frame after. */
	unsigned long frame = 0;
	while (!aod_exit_requested) {
		int trace = frame < 3;
		aod_gldiag_frame_begin(frame);   /* DIAGNOSTIC build */
		controls_poll();
		if (trace) l_info("frame %lu: nativeUpdate begin", frame);
		nativeUpdate(&jni, bridge);
		if (trace) l_info("frame %lu: nativeUpdate end", frame);
		for (int i = 0; i < n_events; i++) {
			input_event *e = &events[i];
			if (e->kind == EV_TOUCH) nativeOnTouchEvent(&jni, bridge, e->type, e->id, e->x, e->y, e->t);
			else if (e->kind == EV_KEY) nativeOnKeyEvent(&jni, bridge, e->type, e->id, e->t);
			else nativeOnWindowEvent(&jni, bridge, e->type ? JNI_TRUE : JNI_FALSE);
		}
		n_events = 0;
		aod_jni_run_deferred();
		aod_alert_poll();
		if (trace) l_info("frame %lu: swap begin", frame);
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
