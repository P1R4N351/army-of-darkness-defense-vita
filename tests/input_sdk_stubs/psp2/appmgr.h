/*
 * appmgr.h — Stub declarations for SceAppMgr SDK (test use only)
 * This file is part of aod-vita and is distributed under the MIT license.
 * SPDX-License-Identifier: MIT
 */

#ifndef SCE_APPMGR_H
#define SCE_APPMGR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SceAppMgrExecOptParam SceAppMgrExecOptParam;

int sceAppMgrLoadExec(const char *appPath,
                      char * const argv[],
                      const SceAppMgrExecOptParam *opt);

#ifdef __cplusplus
}
#endif

#endif /* SCE_APPMGR_H */