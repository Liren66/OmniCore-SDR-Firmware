#ifndef SDR_CLOCK_H
#define SDR_CLOCK_H
#include <stdint.h>
/* Console-thread only. Implemented by sdr_fir.c using the same stopped
 * transport guard and RF snapshot/rollback as FIR commits. */
int sdr_clock_apply(const uint32_t rx[6], const uint32_t tx[6]);
int sdr_clock_set_rate(uint32_t hz);
int sdr_clock_execute(const char *line);
#endif
