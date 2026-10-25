#include "sdr_external_lo.h"
#include "sdr_runtime.h"
#include "console.h"
#include "platform.h"
#include <string.h>

/* RX/TX LO selection applies to both RF channels.
 * External LO is a separate RF input, unrelated to the 10 MHz reference PLL.
 * UG-570 requires EXT_LO_IN = 2 * RF frequency (140 MHz..8 GHz input).
 * Register verification proves selection, never external signal presence.
 */
static uint8_t external_lo_target[2]; /* Cold boot: both internal. */
static uint32_t external_lo_dummy_target[2];
static unsigned external_lo_restore_in_progress;

static uint64_t external_lo_frequency(unsigned tx)
{
    unsigned ext = tx ? ad9361_phy->pdata->use_ext_tx_lo : ad9361_phy->pdata->use_ext_rx_lo;
    if (ext) {
        return ad9361_from_clk(ad9361_phy->clks[tx ? TX_RFPLL_DUMMY : RX_RFPLL_DUMMY]->rate);
    }
    return ad9361_from_clk(tx ? ad9361_phy->current_tx_lo_freq : ad9361_phy->current_rx_lo_freq);
}

static int external_lo_read(unsigned tx, uint8_t *value)
{
    int rc = tx ? ad9361_get_tx_lo_int_ext(ad9361_phy, value)
                : ad9361_get_rx_lo_int_ext(ad9361_phy, value);
    unsigned cached = tx ? ad9361_phy->pdata->use_ext_tx_lo : ad9361_phy->pdata->use_ext_rx_lo;
    if (rc < 0) {
        return rc;
    }
    return *value == cached ? 0 : -EIO;
}

static int external_lo_set(unsigned tx, unsigned value)
{
    return tx ? ad9361_set_tx_lo_int_ext(ad9361_phy, (uint8_t)value)
              : ad9361_set_rx_lo_int_ext(ad9361_phy, (uint8_t)value);
}

/* Validate current hardware and driver state. During rf_reinit, FIR is restored
 * with internal LOs before the separately retained external target is applied. */
int sdr_external_lo_verify(void)
{
    uint8_t actual;
    unsigned tx;
    int rc;
    for (tx = 0; tx < 2; ++tx) {
        rc = external_lo_read(tx, &actual);
        if (rc < 0) {
            return rc;
        }
        if (actual && (external_lo_frequency(tx) < 70000000ULL ||
                       external_lo_frequency(tx) > 4000000000ULL)) {
            return -EINVAL;
        }
    }
    return 0;
}

/* Call before destroying phy, including recovery after an RF error. Frequency
 * setters update the dummy only after a valid nominal external frequency. */
void sdr_external_lo_save_frequencies(void)
{
    unsigned tx;
    if (!ad9361_phy || external_lo_restore_in_progress) {
        return;
    }
    for (tx = 0; tx < 2; ++tx) {
        uint32_t rate = ad9361_phy->clks[tx ? TX_RFPLL_DUMMY : RX_RFPLL_DUMMY]->rate;
        unsigned active = tx ? ad9361_phy->pdata->use_ext_tx_lo : ad9361_phy->pdata->use_ext_rx_lo;
        /* A reinit that failed before LO restore leaves a fresh internal phy;
         * its default dummy frequency must not overwrite the retained target. */
        if (rate && active && external_lo_target[tx]) {
            external_lo_dummy_target[tx] = rate;
        }
    }
}

int sdr_external_lo_restore(void)
{
    unsigned tx;
    int rc;
    /* A source may be restored before its nominal frequency setter fails.
     * Do not capture such a partially restored PHY on the next reinit retry. */
    external_lo_restore_in_progress = 1;
    for (tx = 0; tx < 2; ++tx) {
        rc = external_lo_set(tx, external_lo_target[tx]);
        if (rc < 0) {
            return rc;
        }
        if (external_lo_target[tx] && external_lo_dummy_target[tx]) {
            uint64_t hz = ad9361_from_clk(external_lo_dummy_target[tx]);
            rc = tx ? ad9361_set_tx_lo_freq(ad9361_phy, hz) : ad9361_set_rx_lo_freq(ad9361_phy, hz);
            if (rc < 0) {
                return rc;
            }
        }
    }
    rc = sdr_external_lo_verify();
    if (!rc) {
        external_lo_restore_in_progress = 0;
    }
    return rc;
}

static int external_lo_restore_ensm(int config, int state)
{
    int rc, actual;
    unsigned retry;
    rc = ad9361_spi_write(ad9361_phy->spi, REG_ENSM_CONFIG_1, (unsigned)config);
    if (rc < 0) {
        return rc;
    }
    actual = ad9361_spi_read(ad9361_phy->spi, REG_ENSM_CONFIG_1);
    if (actual != config) {
        return -EIO;
    }
    for (retry = 0; retry < 10; ++retry) {
        actual = ad9361_spi_read(ad9361_phy->spi, REG_STATE);
        if (actual < 0) {
            return actual;
        }
        if ((actual & ENSM_STATE(~0)) == state) {
            return 0;
        }
        mdelay(1);
    }
    return -EIO;
}

int sdr_external_lo_command(unsigned tx, unsigned set, unsigned value)
{
    uint8_t old, other, actual;
    uint32_t dummy;
    uint64_t other_hz;
    int rc, rollback, config = 0, state = 0;
    const char *name = tx ? "tx_lo_source" : "rx_lo_source";
    if (!sdr_state.rf_ready || !ad9361_phy) {
        return sdr_command_error("rf_not_initialized use=rf_reinit!", sdr_state.rf_error);
    }
    if (tx > 1 || (set && value > 1)) {
        return sdr_command_error("invalid_integer_or_range", -EINVAL);
    }
    if (set && (sdr_state.tx_enabled || sdr_state.usb_rx || sdr_state.usb_tx)) {
        return sdr_command_error("busy stop_tx_and_usb_before_lo_source_change", -1);
    }
    rc = external_lo_read(tx, &old);
    if (rc < 0) {
        goto read_error;
    }
    actual = old;
    if (set && value != old) {
        if (value && (external_lo_frequency(tx) < 70000000ULL ||
                      external_lo_frequency(tx) > 4000000000ULL)) {
            return sdr_command_error(
                "external_lo_frequency_range min_hz=70000000 max_hz=4000000000 applied=unchanged",
                -EINVAL);
        }
        rc = external_lo_read(!tx, &other);
        if (rc < 0) {
            goto read_error;
        }
        other_hz = external_lo_frequency(!tx);
        config = ad9361_spi_read(ad9361_phy->spi, REG_ENSM_CONFIG_1);
        if (config < 0) {
            rc = config;
            goto read_error;
        }
        state = ad9361_spi_read(ad9361_phy->spi, REG_STATE);
        if (state < 0) {
            rc = state;
            goto read_error;
        }
        state &= ENSM_STATE(~0);
        dummy = ad9361_phy->clks[tx ? TX_RFPLL_DUMMY : RX_RFPLL_DUMMY]->rate;
        rc = external_lo_set(tx, value);
        if (rc >= 0) {
            rc = external_lo_read(tx, &actual);
        }
        if (rc >= 0 && actual != value) {
            rc = -EIO;
        }
        if (rc >= 0) {
            rc = external_lo_read(!tx, &actual);
        }
        if (rc >= 0 && (actual != other || external_lo_frequency(!tx) != other_hz)) {
            rc = -EIO;
        }
        if (rc < 0) {
            rollback = external_lo_set(tx, old);
            if (old) {
                uint64_t hz = ad9361_from_clk(dummy);
                /* This also restores RX gain-table band after a cross-band
                 * internal selection was applied before a later failure. */
                int restore = tx ? ad9361_set_tx_lo_freq(ad9361_phy, hz)
                                 : ad9361_set_rx_lo_freq(ad9361_phy, hz);
                if (restore < 0) {
                    rollback = restore;
                }
            } else {
                ad9361_phy->clks[tx ? TX_RFPLL_DUMMY : RX_RFPLL_DUMMY]->rate = dummy;
            }
            if (external_lo_restore_ensm(config, state) < 0) {
                rollback = -EIO;
            }
            if (external_lo_read(tx, &actual) < 0 || actual != old) {
                rollback = -EIO;
            }
            if (external_lo_read(!tx, &actual) < 0 || actual != other ||
                external_lo_frequency(!tx) != other_hz) {
                rollback = -EIO;
            }
            if (rollback < 0) {
                sdr_state.rf_ready = sdr_state.digital_ready = 0;
                sdr_state.rf_error = rollback;
                sdr_set_tx_gate(0);
            }
            console_print("ERR %s stage=apply code=%d rollback=%s applied=%s%s\n", name, rc,
                          rollback < 0 ? "failed" : "verified",
                          rollback < 0 ? "unknown" : "restored",
                          rollback < 0 ? " use=rf_reinit!" : "");
            return -1;
        }
        actual = (uint8_t)value;
    }
    if (set) {
        external_lo_target[tx] = actual;
        sdr_external_lo_save_frequencies();
    }
    console_print("%s=%u frequency_hz=%llu verified=1 signal_present=unverified scope=all_%s\n",
                  name, (unsigned)actual, (unsigned long long)external_lo_frequency(tx),
                  tx ? "tx" : "rx");
    return 0;
read_error:
    console_print("ERR %s stage=read code=%d applied=%s\n", name, rc,
                  set ? "unchanged" : "unknown");
    return -1;
}
