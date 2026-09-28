/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021-2022 Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/init.h"

#include "utils/dialog.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/settings.h"

#include <reimpl/controls.h>

#include <string.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/kernel/clib.h>
#include <psp2/power.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>
#include <fios/fios.h>

// Base addresses. libgame spans 0x98000000..0x988E4400 (PT_LOAD memsz), libfmodex is
// 0x13C47C bytes; 0x9A000000 is reserved for the kuser helper page in patch.c.
#define GAME_LOAD_ADDRESS 0x98000000
#define FMOD_LOAD_ADDRESS 0x99000000

extern so_module aod_game_mod;
extern so_module aod_fmod_mod;

static void load_module(so_module *mod, const char *path, uintptr_t addr) {
    if (!file_exists(path)) {
        fatal_error("Looks like you haven't installed the data files for this "
                    "port, or they are in an incorrect location. Please make "
                    "sure that you have %s file exactly at that path.", path);
    }
    int r = so_file_load(mod, path, addr);
    if (r < 0) {
        l_fatal("%s could not be loaded (%d).", path, r);
        fatal_error("Error: could not load %s.", path);
    }
    l_success("%s loaded at 0x%08x (exidx 0x%08x, %u bytes).", path, addr,
              (unsigned)mod->exidx_base, (unsigned)mod->exidx_size);
}

void soloader_init_all() {
	// Launch `app0:configurator.bin` on `-config` init param
    sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
    SceAppUtilAppEventParam eventParam;
    sceClibMemset(&eventParam, 0, sizeof(SceAppUtilAppEventParam));
    sceAppUtilReceiveAppEvent(&eventParam);
    if (eventParam.type == 0x05) {
        char buffer[2048];
        sceAppUtilAppEventParseLiveArea(&eventParam, buffer);
        if (strstr(buffer, "-config"))
            sceAppMgrLoadExec("app0:/configurator.bin", NULL, NULL);
    }

    // Set default overclock values
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

#ifdef USE_SCELIBC_IO
    if (fios_init(DATA_PATH) == 0)
        l_success("FIOS initialized.");
#endif

    if (!module_loaded("kubridge")) {
        l_fatal("kubridge is not loaded.");
        fatal_error("Error: kubridge.skprx is not installed.");
    }
    l_success("kubridge check passed.");

    // Same order as GameApplication's static block: fmodex, then game (which NEEDs it).
    load_module(&aod_fmod_mod, FMOD_SO_PATH, FMOD_LOAD_ADDRESS);
    load_module(&aod_game_mod, SO_PATH, GAME_LOAD_ADDRESS);

    settings_load();
    l_success("Settings loaded.");

    file_mkpath(DATA_PATH "files/", 0777);

    so_relocate(&aod_fmod_mod);
    so_relocate(&aod_game_mod);
    l_success("SOs relocated.");

    resolve_imports(&aod_fmod_mod);
    resolve_imports(&aod_game_mod);
    l_success("SO imports resolved.");

    so_patch();
    l_success("SOs patched.");

    so_flush_caches(&aod_fmod_mod);
    so_flush_caches(&aod_game_mod);
    l_success("SO caches flushed.");

    so_initialize(&aod_fmod_mod);
    so_initialize(&aod_game_mod);
    l_success("SOs initialized (init_array: fmodex %u, game %u).",
              (unsigned)aod_fmod_mod.num_init_array, (unsigned)aod_game_mod.num_init_array);

    gl_preload();
    l_success("OpenGL preloaded.");

    jni_init();
    l_success("FalsoJNI initialized.");

#ifndef NDK_PORT
    controls_init();
    l_success("Controls initialized.");
#endif
}
