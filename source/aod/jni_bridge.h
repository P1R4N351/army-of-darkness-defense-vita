#ifndef AOD_JNI_BRIDGE_H
#define AOD_JNI_BRIDGE_H

/* Replace FalsoJNI's name-only method/field dispatch with the class+name+sig table. Call after jni_init(). */
void aod_jni_install(void);
/* Deliver queued Java->native replies (IAP/ads/dialog callbacks). Call from the render loop after nativeUpdate(). */
void aod_jni_run_deferred(void);
/* Surface size reported by Information.getDisplayWidth/Height and getUsable*. */
void aod_jni_set_surface(int w, int h);

/* Log cumulative bridge allocations (objects by kind, jstrings). Called periodically so a
 * per-frame allocation shows up as steady growth in aod_log.txt. */
void aod_jni_log_stats(void);

#endif
