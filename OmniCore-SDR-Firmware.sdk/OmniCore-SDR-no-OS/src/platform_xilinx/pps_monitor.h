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
int pps_monitor_init(XScuGic *gic, uint32_t base);
int pps_monitor_enable(unsigned enabled);
/* Main loop only. Set zero before clock/PL changes; restore after they finish.
 * User enable is preserved; every new measurement needs two fresh PPS edges. */
void pps_monitor_nominal(uint32_t hz);
void pps_monitor_status(PpsStatus *status);
int pps_monitor_available(void);
#endif
