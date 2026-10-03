/**
 * Stub header for host-side input adapter tests (PSVita controller).
 * Mirrors exact structs/enums/prototypes from psp2/ctrl.h + psp2common/ctrl.h.
 * Does NOT include vitasdk build_utils or real SDK headers.
 */

#ifndef _STUB_PSP2_CTRL_H_
#define _STUB_PSP2_CTRL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Scalar type aliases used by the SDK -------------------------------- */
typedef uint8_t  SceUInt8;
typedef uint32_t SceUInt32;
typedef uint64_t SceUInt64;

/* ---- SceCtrlButtons ----------------------------------------------------- */
typedef enum SceCtrlButtons {
    SCE_CTRL_SELECT      = 0x00000001,
    SCE_CTRL_L3          = 0x00000002,
    SCE_CTRL_R3          = 0x00000004,
    SCE_CTRL_START       = 0x00000008,
    SCE_CTRL_UP          = 0x00000010,
    SCE_CTRL_RIGHT       = 0x00000020,
    SCE_CTRL_DOWN        = 0x00000040,
    SCE_CTRL_LEFT        = 0x00000080,
    SCE_CTRL_LTRIGGER    = 0x00000100,
    SCE_CTRL_L2          = SCE_CTRL_LTRIGGER,
    SCE_CTRL_RTRIGGER    = 0x00000200,
    SCE_CTRL_R2          = SCE_CTRL_RTRIGGER,
    SCE_CTRL_L1          = 0x00000400,
    SCE_CTRL_R1          = 0x00000800,
    SCE_CTRL_TRIANGLE    = 0x00001000,
    SCE_CTRL_CIRCLE      = 0x00002000,
    SCE_CTRL_CROSS       = 0x00004000,
    SCE_CTRL_SQUARE      = 0x00008000,
    SCE_CTRL_INTERCEPTED = 0x00010000,
    SCE_CTRL_PSBUTTON    = SCE_CTRL_INTERCEPTED,
    SCE_CTRL_HEADPHONE   = 0x00080000,
    SCE_CTRL_VOLUP       = 0x00100000,
    SCE_CTRL_VOLDOWN     = 0x00200000,
    SCE_CTRL_POWER       = 0x40000000
} SceCtrlButtons;

/* ---- SceCtrlPadInputMode ------------------------------------------------ */
typedef enum SceCtrlPadInputMode {
    SCE_CTRL_MODE_DIGITAL     = 0,
    SCE_CTRL_MODE_ANALOG      = 1,
    SCE_CTRL_MODE_ANALOG_WIDE = 2
} SceCtrlPadInputMode;

/* ---- SceCtrlData -------------------------------------------------------- */
typedef struct SceCtrlData {
    uint64_t      timeStamp;
    unsigned int  buttons;
    unsigned char lx;
    unsigned char ly;
    unsigned char rx;
    unsigned char ry;
    uint8_t       up;
    uint8_t       right;
    uint8_t       down;
    uint8_t       left;
    uint8_t       lt;
    uint8_t       rt;
    uint8_t       l1;
    uint8_t       r1;
    uint8_t       triangle;
    uint8_t       circle;
    uint8_t       cross;
    uint8_t       square;
    uint8_t       reserved[4];
} SceCtrlData;

/* ---- Required function prototypes --------------------------------------- */
int sceCtrlSetSamplingModeExt(SceCtrlPadInputMode mode);
int sceCtrlPeekBufferPositiveExt2(int port, SceCtrlData *pad_data, int count);

#ifdef __cplusplus
}
#endif

#endif /* _STUB_PSP2_CTRL_H_ */