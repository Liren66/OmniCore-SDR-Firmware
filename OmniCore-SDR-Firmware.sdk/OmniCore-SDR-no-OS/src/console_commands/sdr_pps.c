#include "sdr_pps.h"
#include "sdr_runtime.h"
#include "console.h"
#include "pps_monitor.h"

/* This board clocks the PPS counter from the un-divided LVDS DATA_CLK.
 * As in ad9361_validate_trx_clock_chain(), physical 2R2T LVDS is 4 * Fs.
 * USB channel masks do not change the physical AD9361 port mode. */
void sdr_pps_refresh(void)
{
    uint32_t hz = 0;
    if (sdr_state.rf_ready && ad9361_phy && ad9361_phy->pdata->rx2tx2 &&
        (ad9361_phy->pdata->port_ctrl.pp_conf[2] & LVDS_MODE)) {
        hz = 4U * clk_get_rate(ad9361_phy, ad9361_phy->ref_clk_scale[RX_SAMPL_CLK]);
    }
    pps_monitor_nominal(hz);
}

void sdr_pps_status(void)
{
    PpsStatus s;
    static const char *states[] = {"disabled",  "waiting",          "valid",
                                   "no_signal", "invalid_interval", "clock_unavailable"};
    sdr_pps_refresh();
    pps_monitor_status(&s);
    console_print(
        "pps_status available=%lu enabled=%lu state=%s signal_present=%lu frequency_valid=%lu nominal_hz=%lu measured_hz=%lu error_ppm=",
        (unsigned long)s.available, (unsigned long)s.enabled, states[s.state],
        (unsigned long)s.signal_present, (unsigned long)s.frequency_valid,
        (unsigned long)s.nominal_hz, (unsigned long)s.measured_hz);
    if (s.frequency_valid && s.nominal_hz) {
        console_print("%.6f", ((double)s.measured_hz / s.nominal_hz - 1.0) * 1000000.0);
    } else {
        console_print("unknown");
    }
    console_print(
        " pulse_count=%lu invalid_count=%lu age_ms=%lu raw_counter=%lu hardware_timeout=%lu monitor_only=1 adjustment=0 clock=DATA_CLK expected_pps_hz=1\n",
        (unsigned long)s.pulse_count, (unsigned long)s.invalid_count, (unsigned long)s.age_ms,
        (unsigned long)s.raw_counter, (unsigned long)s.hardware_timeout);
}

int sdr_pps_command(unsigned write, unsigned enabled)
{
    PpsStatus s;
    if (!pps_monitor_available()) {
        return sdr_command_error("unsupported pps_monitor_requires_updated_bitstream", -1);
    }
    sdr_pps_refresh();
    if (write && pps_monitor_enable(enabled) < 0) {
        return sdr_command_error("pps_monitor_enable_failed", -1);
    }
    pps_monitor_status(&s);
    console_print("pps_input_en=%lu monitor_only=1 adjustment=0\n", (unsigned long)s.enabled);
    return 0;
}
