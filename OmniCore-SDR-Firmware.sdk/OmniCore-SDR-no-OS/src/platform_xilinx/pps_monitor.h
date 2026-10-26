#ifndef PPS_MONITOR_H
#define PPS_MONITOR_H
#include <stdint.h>
#include "xscugic.h"

/* Board wiring: concat In0 -> IRQ_F2P[0] -> GIC 61. The generated BSP's
 * GPS_PPS_IRQ_INTR=91 is incorrect for this design (91 belongs to CH32). */
#define PPS_BOARD_IRQ_ID 61U
enum { PPS_DISABLED, PPS_WAITING, PPS_VALID, PPS_NO_SIGNAL,
       PPS_INVALID_INTERVAL, PPS_CLOCK_UNAVAILABLE };
typedef struct {
    uint32_t available, enabled, state, signal_present, frequency_valid;
    uint32_t nominal_hz, measured_hz, pulse_count, invalid_count, age_ms;
    uint32_t raw_counter, hardware_timeout;
} PpsStatus;
/* One PPS edge. seq counts edges; epoch changes on every rearm, so equal
 * epochs and consecutive seq values mean an unbroken DATA_CLK count chain.
 * count is DATA_CLK cycles since the previous edge, valid only if valid=1. */
typedef struct {
    uint32_t seq, epoch, count, valid;
} PpsSample;
int pps_monitor_init(XScuGic *gic, uint32_t base);
int pps_monitor_enable(unsigned enabled);
/* Main loop only. Set zero before clock/PL changes; restore after they finish.
 * User enable is preserved; every new measurement needs two fresh PPS edges.
 * Returns 1 when the nominal frequency changed (measurement rearmed). */
int pps_monitor_nominal(uint32_t hz);
void pps_monitor_status(PpsStatus *status);
int pps_monitor_available(void);
/* Main loop only. Returns 1 with the newest edge not yet taken, else 0.
 * Edges arriving while the loop is blocked are skipped (seq jumps). */
int pps_monitor_take_sample(PpsSample *sample);
#endif
