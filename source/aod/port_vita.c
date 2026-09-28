/*
 * aod-vita: PS Vita implementations of the platform queries in aod/port.h.
 *
 * This file is part of aod-vita and is distributed under the MIT license.
 */

#include "aod/port.h"
#include "aod/jni_bridge.h"
#include "utils/logger.h"

#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/io/devctl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/message_dialog.h>
#include <psp2/system_param.h>

#include <so_util/so_util.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern so_module aod_game_mod;

void aod_log(const char *fmt, ...) {
	char buf[1536];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	_log_print(LT_INFO, "%s", buf);
}

void *aod_game_symbol(const char *name) { return (void *)so_symbol(&aod_game_mod, name); }

volatile int aod_exit_requested;
void aod_request_exit(void) { aod_exit_requested = 1; }

/* The native side only ever reads prefs (getInt for the ratings prompt); nothing writes them.
 * A "key=value" file in DATA_PATH lets a user pre-set them; otherwise the default is returned,
 * which matches a fresh Android install. */
int aod_prefs_get_int(const char *key, int def) {
	FILE *f = fopen(DATA_PATH "prefs.txt", "r");
	if (!f) return def;
	char line[256];
	size_t kl = strlen(key);
	int v = def;
	while (fgets(line, sizeof(line), f))
		if (strncmp(line, key, kl) == 0 && line[kl] == '=') { v = atoi(line + kl + 1); break; }
	fclose(f);
	return v;
}

const char *aod_write_path(void) { return DATA_PATH "files"; }
const char *aod_device_model(void) { return "PlayStation Vita"; }
const char *aod_device_id(void) { return "5ce0a0dd00000001"; }

static int sys_lang(void) {
	int lang = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
	sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang);
	return lang;
}

const char *aod_language(void) {
	switch (sys_lang()) {
	case SCE_SYSTEM_PARAM_LANG_JAPANESE: return "ja";
	case SCE_SYSTEM_PARAM_LANG_FRENCH: return "fr";
	case SCE_SYSTEM_PARAM_LANG_SPANISH: return "es";
	case SCE_SYSTEM_PARAM_LANG_GERMAN: return "de";
	case SCE_SYSTEM_PARAM_LANG_ITALIAN: return "it";
	case SCE_SYSTEM_PARAM_LANG_DUTCH: return "nl";
	case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_PT:
	case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_BR: return "pt";
	case SCE_SYSTEM_PARAM_LANG_RUSSIAN: return "ru";
	case SCE_SYSTEM_PARAM_LANG_KOREAN: return "ko";
	case SCE_SYSTEM_PARAM_LANG_CHINESE_T:
	case SCE_SYSTEM_PARAM_LANG_CHINESE_S: return "zh";
	default: return "en";
	}
}

const char *aod_country(void) {
	switch (sys_lang()) {
	case SCE_SYSTEM_PARAM_LANG_ENGLISH_GB: return "GB";
	case SCE_SYSTEM_PARAM_LANG_JAPANESE: return "JP";
	case SCE_SYSTEM_PARAM_LANG_FRENCH: return "FR";
	case SCE_SYSTEM_PARAM_LANG_GERMAN: return "DE";
	default: return "US";
	}
}

int64_t aod_free_space_bytes(void) {
	struct { uint64_t max_size, free_size; uint32_t cluster_size; void *unk; } info;
	memset(&info, 0, sizeof(info));
	if (sceIoDevctl("ux0:", 0x3001, NULL, 0, &info, sizeof(info)) < 0 || info.free_size == 0)
		return 1024LL * 1024 * 1024;
	return (int64_t)info.free_size;
}

double aod_uptime_seconds(void) { return sceKernelGetProcessTimeWide() / 1000000.0; }

/* --- PlatformAlert via the system message dialog.
 * 0/1 button: OK. 2 buttons: YES/NO with the game's labels spelled out in the text (the YESNO
 * layout has fixed captions). 3 buttons: 3BUTTONS with the game's labels. */
static int alert_active;
static int alert_n;
static int64_t alert_handle;

int aod_alert_begin(const char *title, const char *message, const char *const *labels, int n, int64_t handle) {
	if (alert_active) return -1;
	static char text[1024], l[3][128];
	int k = snprintf(text, sizeof(text), "%s%s%s", title ? title : "", (title && *title) ? "\n\n" : "", message ? message : "");
	if (n == 2 && k > 0 && k < (int)sizeof(text))
		snprintf(text + k, sizeof(text) - k, "\n\n[Yes] %s   [No] %s", labels[0], labels[1]);
	static SceMsgDialogButtonsParam bp;
	memset(&bp, 0, sizeof(bp));
	SceMsgDialogUserMessageParam msg;
	memset(&msg, 0, sizeof(msg));
	msg.msg = (const SceChar8 *)text;
	if (n >= 3) {
		for (int i = 0; i < 3; i++) snprintf(l[i], sizeof(l[i]), "%s", labels[i]);
		bp.msg1 = l[0]; bp.msg2 = l[1]; bp.msg3 = l[2];
		bp.fontSize1 = bp.fontSize2 = bp.fontSize3 = SCE_MSG_DIALOG_FONT_SIZE_DEFAULT;
		msg.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_3BUTTONS;
		msg.buttonParam = &bp;
	} else {
		msg.buttonType = n == 2 ? SCE_MSG_DIALOG_BUTTON_TYPE_YESNO : SCE_MSG_DIALOG_BUTTON_TYPE_OK;
	}
	SceMsgDialogParam param;
	sceMsgDialogParamInit(&param);
	param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
	param.userMsgParam = &msg;
	int r = sceMsgDialogInit(&param);
	if (r < 0) {
		aod_log("PlatformAlert: sceMsgDialogInit failed 0x%x", r);
		return -1;
	}
	alert_active = 1;
	alert_n = n;
	alert_handle = handle;
	return 0;
}

int aod_alert_active(void) { return alert_active; }

void aod_alert_poll(void) {
	if (!alert_active || sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) return;
	SceMsgDialogResult res;
	memset(&res, 0, sizeof(res));
	sceMsgDialogGetResult(&res);
	sceMsgDialogTerm();
	alert_active = 0;
	int index = -1;
	if (alert_n >= 3) {
		if (res.buttonId >= SCE_MSG_DIALOG_BUTTON_ID_BUTTON1 && res.buttonId <= SCE_MSG_DIALOG_BUTTON_ID_BUTTON3)
			index = res.buttonId - SCE_MSG_DIALOG_BUTTON_ID_BUTTON1;
	} else if (alert_n == 2) {
		if (res.buttonId == SCE_MSG_DIALOG_BUTTON_ID_YES) index = 0;
		else if (res.buttonId == SCE_MSG_DIALOG_BUTTON_ID_NO) index = 1;
	} else if (alert_n == 1) {
		if (res.buttonId == SCE_MSG_DIALOG_BUTTON_ID_OK) index = 0;
	}
	aod_jni_alert_result(index, alert_handle);
}
