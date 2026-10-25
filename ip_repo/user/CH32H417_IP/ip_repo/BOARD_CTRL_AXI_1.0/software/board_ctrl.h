#ifndef BOARD_CTRL_H
#define BOARD_CTRL_H
#include "xil_io.h"
#include "xil_types.h"

#define BOARD_CTRL_RGB_OFFSET        0x00U
#define BOARD_CTRL_FAN_PERIOD_OFFSET 0x04U
#define BOARD_CTRL_FAN_HIGH_OFFSET   0x08U
#define BOARD_CTRL_CLOCK_HZ_OFFSET   0x0CU

static inline void BoardCtrl_SetRgb(UINTPTR base, u8 red, u8 green, u8 blue)
{
    Xil_Out32(base + BOARD_CTRL_RGB_OFFSET,
              ((u32)red << 16) | ((u32)green << 8) | (u32)blue);
}

/* permille: 0..1000, e.g. 500 = 50% high time. Not an RPM setting.
 * The period/high registers take effect at PWM boundaries. They are separate
 * writes; changing frequency and duty together may give one intermediate cycle.
 */
static inline int BoardCtrl_SetFan(UINTPTR base, u32 frequency_hz, u32 permille)
{
    u32 clock_hz = Xil_In32(base + BOARD_CTRL_CLOCK_HZ_OFFSET);
    u32 period, high;
    if (frequency_hz == 0U || frequency_hz > clock_hz || permille > 1000U)
        return -1;
    period = clock_hz / frequency_hz;
    high = (u32)(((u64)period * permille + 500U) / 1000U);
    Xil_Out32(base + BOARD_CTRL_FAN_PERIOD_OFFSET, period);
    Xil_Out32(base + BOARD_CTRL_FAN_HIGH_OFFSET, high);
    return 0;
}

/* Change duty only, retaining the requested PWM frequency. */
static inline int BoardCtrl_SetFanDuty(UINTPTR base, u32 permille)
{
    u32 period = Xil_In32(base + BOARD_CTRL_FAN_PERIOD_OFFSET);
    if (permille > 1000U) return -1;
    if (period == 0U) period = 1U;
    Xil_Out32(base + BOARD_CTRL_FAN_HIGH_OFFSET,
              (u32)(((u64)period * permille + 500U) / 1000U));
    return 0;
}
#endif
