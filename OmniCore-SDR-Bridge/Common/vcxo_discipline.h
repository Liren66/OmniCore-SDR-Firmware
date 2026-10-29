#ifndef VCXO_DISCIPLINE_H
#define VCXO_DISCIPLINE_H
#include "ch32h417.h"
/* PA4 = DAC1_OUT, wired to the ADF4002 CP / 40 MHz VCTCXO tune node.
 * The Zynq selects the mode; this MCU only drives PA4 in VCXO_MODE_PPS. */
enum
{
    VCXO_MODE_OFF = 0,     /* ADF4002 high-Z, PA4 high-Z: Vc = 1.65 V divider */
    VCXO_MODE_REF = 1,     /* ADF4002 PLL to 10 MHz, PA4 high-Z */
    VCXO_MODE_PPS = 2,     /* ADF4002 high-Z, PA4 DAC loop from Zynq PPS counts */
    VCXO_MODE_REF_PPS = 3  /* ADF4002 PLL, PA4 high-Z, PPS counts are reference only */
};
enum
{
    VCXO_STATE_OFF = 0,
    VCXO_STATE_CAL = 1,    /* measure DAC slope K (ppb/LSB) */
    VCXO_STATE_ACQ = 2,    /* frequency-locked loop */
    VCXO_STATE_TRACK = 3,  /* phase PI loop, PPS time error -> 0 */
    VCXO_STATE_HOLD = 4    /* no fresh PPS sample: DAC frozen */
};
typedef struct
{
    uint32_t lines;
    uint32_t line_errors;
    uint32_t samples;
    uint32_t outliers;
    uint32_t replies;
    uint32_t rsp_ends;       /* "RSP id=N end" lines seen from the Zynq */
    uint32_t last_rsp_id;    /* N of the most recent one */
    uint32_t phase_slips;    /* TRACK phase error beyond 2 us, phase rezeroed */
} VCXO_Stats_t;

/* Inspect using the debugger. */
extern volatile VCXO_Stats_t VCXO_Stats;
/* Call first in Hardware(): PA4 analog, DAC1 disabled (high-Z). */
void VCXO_Init(void);
/* Every byte received from the Zynq console, in order, including bytes the
 * USB bridge later drops. Not interrupt-safe; call from the main loop only. */
void VCXO_RxByte(uint8_t ch);
void VCXO_RxReset(void);
void VCXO_Task(void);
/* Copy the pending "vcxo_ack ...\n" line for injection into the Zynq console.
 * Returns 0 when nothing is pending or the Zynq may be busy. Caller
 * guarantees a host line boundary. The Zynq console is polled: bytes that
 * arrive while it executes a command overrun its 16-byte FIFO and make it
 * discard the following host line. It prints "VCXO" only from its idle loop,
 * so a reply is allowed only shortly after one, with no host bytes since. */
uint16_t VCXO_TakeReply(uint8_t *out, uint16_t capacity);
/* Call whenever host bytes start towards the Zynq: it may be busy now. */
void VCXO_HostActivity(void);
#endif
