#ifndef AOD_PORT_H
#define AOD_PORT_H

#include <stdint.h>

/* From the APK's AndroidManifest.xml (jadx/resources). */
#define AOD_PACKAGE      "com.backflipstudios.android.aodd"
#define AOD_VERSION_NAME "1.1.1"
#define AOD_VERSION_CODE "7018"

/* Always-on diagnostics: console + DATA_PATH "aod_log.txt". */
void aod_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Exported symbol of the loaded libgame.so (NULL if absent). */
void *aod_game_symbol(const char *name);

void aod_request_exit(void);
int aod_prefs_get_int(const char *key, int def);
const char *aod_write_path(void);
const char *aod_device_model(void);
const char *aod_device_id(void);
const char *aod_language(void);
const char *aod_country(void);
int64_t aod_free_space_bytes(void);
double aod_uptime_seconds(void);
/* Start a system message dialog for PlatformAlert.show with n (0..3) button labels, in order.
 * Returns 0 if started. When it closes the port calls aod_jni_alert_result(index, handle):
 * index = the pressed button (0..n-1), or -1 if closed without a button. */
int aod_alert_begin(const char *title, const char *message, const char *const *labels, int n, int64_t handle);
void aod_jni_alert_result(int index, int64_t handle);

#endif
