#ifndef SDR_SYNC_H
#define SDR_SYNC_H

/* Multi-board synchronisation (OC_SYNC_AXI 1.0, see docs/multi-board-sync.md).
 * REF_OUT (10 MHz from the board's 40 MHz VCTCXO), TRIG generator, SYNC /
 * PPS_EXT edge counters, PPS selection for the time base, AD9361 MCS steps.
 * Main loop only. */
void sdr_sync_init(void);
/* 1 when the bitstream contains OC_SYNC_AXI. */
unsigned sdr_sync_capable(void);
/* Time base PPS input: 0 = PPS_EXT pin, 1 = AD9361_SYNC (or internal TRIG
 * with sync_loopback=1). Returns 0 or a negative error (no ERR printed). */
int sdr_sync_select_pps(unsigned sync);
unsigned sdr_sync_pps_is_sync(void);
/* Text commands. 0 = not a sync command, 1 = handled, -1 = rejected. */
int sdr_sync_execute(const char *line);
/* Reports SYNC edges as EVT type=sync_in (at most one line per 250 ms). */
void sdr_sync_service(void);
void sdr_sync_help(void);
void sdr_sync_print_capabilities(void);

#endif
