#include "board_ctrl.h"

/* Supply the base address assigned by Vivado Address Editor / xparameters.h.
 * Call after the bitstream is loaded and AXI reset is released.
 */
void BoardCtrl_Example(UINTPTR base)
{
    BoardCtrl_SetRgb(base, 16, 0, 0);       /* Dim red, automatic refresh. */
    (void)BoardCtrl_SetFan(base, 25000, 500); /* 25 kHz, 50% high time. */

    /* Other examples:
     * BoardCtrl_SetRgb(base, 0, 16, 0);   // Green
     * BoardCtrl_SetRgb(base, 0, 0, 16);   // Blue
     * BoardCtrl_SetRgb(base, 0, 0, 0);    // LED off
     * BoardCtrl_SetFanDuty(base, 0);      // Constant low
     * BoardCtrl_SetFanDuty(base, 1000);   // Constant high
     */
}
