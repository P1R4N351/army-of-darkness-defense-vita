/*
 * apputil.h — Stub declarations for SceAppUtil SDK (test use only)
 * This file is part of aod-vita and is distributed under the MIT license.
 * SPDX-License-Identifier: MIT
 */

#ifndef SCE_APPUTIL_H
#define SCE_APPUTIL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SceAppUtilInitParam {
    uint32_t workBufSize;
    uint8_t  reserved[60];
} SceAppUtilInitParam;

typedef struct SceAppUtilBootParam {
    unsigned int attr;
    unsigned int appVersion;
    uint8_t      reserved[32];
} SceAppUtilBootParam;

typedef struct SceAppUtilAppEventParam {
    unsigned int type;
    uint8_t      dat[1024];
} SceAppUtilAppEventParam;

int sceAppUtilInit(SceAppUtilInitParam *init_param,
                   SceAppUtilBootParam *boot_param);

int sceAppUtilReceiveAppEvent(SceAppUtilAppEventParam *event_param);

int sceAppUtilAppEventParseLiveArea(const SceAppUtilAppEventParam *event_param,
                                    char *buf);

#ifdef __cplusplus
}
#endif

#endif /* SCE_APPUTIL_H */