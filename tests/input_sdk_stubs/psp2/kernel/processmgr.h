/**
 * Stub header for host-side input adapter tests (PSVita process/time API).
 * Mirrors the sceKernelGetProcessTimeWide prototype from
 * psp2/kernel/processmgr.h; no implementation provided.
 * Does NOT include vitasdk build_utils or real SDK headers.
 */

#ifndef _STUB_PSP2_KERNEL_PROCESSMGR_H_
#define _STUB_PSP2_KERNEL_PROCESSMGR_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Scalar type aliases used by the SDK -------------------------------- */
typedef uint64_t SceUInt64;

/* ---- Required function prototype ----------------------------------------
 * Returns the current process time as a 64-bit microsecond counter.        */
SceUInt64 sceKernelGetProcessTimeWide(void);

#ifdef __cplusplus
}
#endif

#endif /* _STUB_PSP2_KERNEL_PROCESSMGR_H_ */