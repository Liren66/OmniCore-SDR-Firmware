#ifndef SDR_TIME_H
#define SDR_TIME_H

#include "CH32H417_IP.h"

/* Hardware sample time base and framed USB transport (CH32H417_IP 1.2).
 * One tick = one AD9361 sample; tick_rate = current sample rate.
 * Main loop only. */
void sdr_time_init(CH32H417_IP *device);
unsigned sdr_time_capable(void);
/* CTRL0 bits selected by rx_timed (RX_TIMED). */
unsigned sdr_time_mode_bits(void);
/* Text commands with 64-bit arguments. 0 = not a time command,
 * 1 = handled, -1 = rejected (an ERR line was printed). */
int sdr_time_execute(const char *line);
/* Drains FPGA timing events into EVT lines; checks for rate changes. */
void sdr_time_service(void);
void sdr_time_help(void);

#endif
