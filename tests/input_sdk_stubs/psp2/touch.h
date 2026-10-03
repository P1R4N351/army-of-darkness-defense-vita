/**
 * Stub header for host-side input adapter tests (PSVita touch panels).
 * Mirrors exact structs/enums/prototypes from psp2/touch.h.
 * Does NOT include vitasdk build_utils or real SDK headers.
 */

#ifndef _STUB_PSP2_TOUCH_H_
#define _STUB_PSP2_TOUCH_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Scalar type aliases used by the SDK -------------------------------- */
typedef int16_t  SceInt16;
typedef uint8_t  SceUInt8;
typedef uint16_t SceUInt16;
typedef uint32_t SceUInt32;
typedef uint64_t SceUInt64;

/* ---- SCE_TOUCH_MAX_REPORT -----------------------------------------------
 * SDK definition: 8 slots (front max 6, back max 4; enforced by adapter).  */
#define SCE_TOUCH_MAX_REPORT 8

/* ---- Port numbers ------------------------------------------------------- */
typedef enum SceTouchPortType {
    SCE_TOUCH_PORT_FRONT   = 0,
    SCE_TOUCH_PORT_BACK    = 1,
    SCE_TOUCH_PORT_MAX_NUM = 2
} SceTouchPortType;

/* ---- Sampling state ----------------------------------------------------- */
typedef enum SceTouchSamplingState {
    SCE_TOUCH_SAMPLING_STATE_STOP  = 0,
    SCE_TOUCH_SAMPLING_STATE_START = 1
} SceTouchSamplingState;

/* ---- Report info flags -------------------------------------------------- */
typedef enum SceTouchReportInfo {
    SCE_TOUCH_REPORT_INFO_HIDE_UPPER_LAYER = 0x0001
} SceTouchReportInfo;

/* ---- SceTouchPanelInfo -------------------------------------------------- */
typedef struct SceTouchPanelInfo {
    SceInt16 minAaX;
    SceInt16 minAaY;
    SceInt16 maxAaX;
    SceInt16 maxAaY;
    SceInt16 minDispX;
    SceInt16 minDispY;
    SceInt16 maxDispX;
    SceInt16 maxDispY;
    SceUInt8 minForce;
    SceUInt8 maxForce;
    SceUInt8 reserved[30];
} SceTouchPanelInfo;

/* ---- SceTouchReport ----------------------------------------------------- */
typedef struct SceTouchReport {
    SceUInt8  id;
    SceUInt8  force;
    SceInt16  x;
    SceInt16  y;
    SceUInt8  reserved[8];
    SceUInt16 info;
} SceTouchReport;

/* ---- SceTouchData ------------------------------------------------------- */
typedef struct SceTouchData {
    SceUInt64      timeStamp;
    SceUInt32      status;
    SceUInt32      reportNum;
    SceTouchReport report[SCE_TOUCH_MAX_REPORT];
} SceTouchData;

/* ---- Required function prototypes --------------------------------------- */
int sceTouchGetPanelInfo(SceUInt32 port, SceTouchPanelInfo *pPanelInfo);
int sceTouchPeek(SceUInt32 port, SceTouchData *pData, SceUInt32 nBufs);
int sceTouchSetSamplingState(SceUInt32 port, SceTouchSamplingState state);

#ifdef __cplusplus
}
#endif

#endif /* _STUB_PSP2_TOUCH_H_ */