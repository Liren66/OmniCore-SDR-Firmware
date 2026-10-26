#include "sdr_pps.h"
#include "sdr_runtime.h"
#include "sdr_vcxo.h"
#include "console.h"
#include "pps_monitor.h"

static uint64_t expected_q16;

/* Exact DATA_CLK in Q16 Hz from the BBPLL words: 4 * BB_REF * (N + F / M) / D.
 * clk_get_rate() truncates a fractional BBPLL (30.72 Msps from 40 MHz is
 * fractional), which would bias the PA4 loop by up to about 1 ppb. D is the
 * integer BBPLL -> RX_SAMPL divider chain. Returns 0 if inconsistent. */
static uint64_t data_clk_q16(uint32_t hz)
{
    uint8_t buf[4];
    uint32_t ref, bbpll, rx, div;
    uint64_t num, den, q;
    int rc;
    /* Runs from the idle loop right after a clock change, exactly when the
     * host sends its next command: keep the console FIFO drained (polled
     * console only; harmless with the RX interrupt). */
    console_poll_rx();
    ref = clk_get_rate(ad9361_phy, ad9361_phy->ref_clk_scale[BB_REFCLK]);
    console_poll_rx();
    bbpll = clk_get_rate(ad9361_phy, ad9361_phy->ref_clk_scale[BBPLL_CLK]);
    console_poll_rx();
    rx = clk_get_rate(ad9361_phy, ad9361_phy->ref_clk_scale[RX_SAMPL_CLK]);
    console_poll_rx();
    rc = ref && rx ? ad9361_spi_readm(ad9361_phy->spi, REG_INTEGER_BB_FREQ_WORD, buf,
                                      REG_INTEGER_BB_FREQ_WORD - REG_FRACT_BB_FREQ_WORD_1 + 1)
                   : -1;
    console_poll_rx();
    if (rc < 0) {
        return 0;
    }
    div = (bbpll + rx / 2U) / rx;
    if (!div || bbpll / div + 1U < rx || bbpll / div > rx + 1U) {
        return 0;
    }
    num = (uint64_t)ref * ((uint64_t)buf[0] * BBPLL_MODULUS +
                           (((uint32_t)buf[3] << 16) | ((uint32_t)buf[2] << 8) | buf[1]));
    den = (uint64_t)BBPLL_MODULUS * div;
    /* 4 * 65536 = 2^18; the remainder term stays below 2^50. */
    q = ((num / den) << 18) + (((num % den) << 18) / den);
    return (q >> 16) + 1U < hz || (q >> 16) > hz + 1U ? 0 : q;
}

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
    if (pps_monitor_nominal(hz)) {
        expected_q16 = hz ? data_clk_q16(hz) : 0;
        if (hz && !expected_q16) {
            expected_q16 = (uint64_t)hz << 16;
        }
    }
}

uint64_t sdr_pps_expected_q16(void)
{
    return expected_q16;
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
    /* monitor_only/adjustment describe this counter; PA4 steering is reported
     * separately by vcxo_status?. */
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
    if (write && sdr_vcxo_request(sdr_vcxo_requested_ref(), enabled) < 0) {
        return -1;
    }
    pps_monitor_status(&s);
    console_print("pps_input_en=%lu vcxo_mode=%u adjustment=%u\n", (unsigned long)s.enabled,
                  sdr_vcxo_requested_mode(), sdr_vcxo_requested_mode() == SDR_VCXO_PPS);
    return 0;
}
