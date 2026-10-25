#include "sdr_fir.h"
#include "sdr_clock.h"
#include "sdr_runtime.h"
#include "sdr_correction.h"
#include "sdr_external_lo.h"
#include "console.h"
#include "pps_monitor.h"
#include <string.h>

extern int32_t ad9361_digital_check(struct ad9361_rf_phy *phy, int tx);

static AD9361_RXFIRConfig active_rx_fir;
static AD9361_TXFIRConfig active_tx_fir;
static unsigned active_fir_initialized, custom_rx_fir, custom_tx_fir;
static unsigned fir_snapshot_verified;
static int fir_verify_coeff(unsigned tx, const int16_t *coef, unsigned taps, int gain,
                            unsigned factor);

static void fir_init_snapshot(void)
{
    if (active_fir_initialized) {
        return;
    }
    active_rx_fir = rx_fir_config;
    active_tx_fir = tx_fir_config;
    active_rx_fir.rx = 3;
    active_tx_fir.tx = 3;
    active_fir_initialized = 1;
}

/* FIR_TRANSACTION_BEGIN: tested verbatim by the host fault-injection harness. */

typedef struct {
    uint32_t token;
    unsigned tx, taps, factor, count;
    int gain;
    int16_t coef[128];
} FirUpload;

static FirUpload fir_upload;
static SdrFirState recover_fir;
static unsigned recover_fir_valid;

static uint32_t fir_crc(const int16_t *v, unsigned n)
{
    uint32_t crc = 0xffffffffU;
    unsigned i, j, k;
    for (i = 0; i < n; ++i) {
        for (j = 0; j < 2; ++j) {
            crc ^= ((uint16_t)v[i] >> (j * 8)) & 255U;
            for (k = 0; k < 8; ++k) {
                crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
            }
        }
    }
    return crc ^ 0xffffffffU;
}

/* Deliberately decimal-only, no float/exponent parsing or lossy casts. */
static int fir_numbers(const char *s, int64_t *v, unsigned max, unsigned *n)
{
    *n = 0;
    for (;;) {
        uint64_t u = 0;
        int neg = 0;
        while (*s == ' ' || *s == '\t') {
            ++s;
        }
        if (*s == '-') {
            neg = 1;
            ++s;
        }
        if (*s < '0' || *s > '9' || *n == max) {
            return -1;
        }
        do {
            unsigned d = (unsigned)(*s++ - '0');
            if (u > (4294967295ULL - d) / 10U) {
                return -1;
            }
            u = u * 10 + d;
        } while (*s >= '0' && *s <= '9');
        v[(*n)++] = neg ? -(int64_t)u : (int64_t)u;
        while (*s == ' ' || *s == '\t') {
            ++s;
        }
        if (*s == ',') {
            ++s;
            continue;
        }
        if (*s == '\r') {
            ++s;
        }
        if (*s == '\n') {
            ++s;
        }
        return *s ? -1 : 0;
    }
}

static int fir_parameters(unsigned tx, unsigned taps, unsigned factor, int gain)
{
    return taps >= 16 && taps <= 128 && taps % 16 == 0 &&
           (factor == 1 || factor == 2 || factor == 4) && (!tx || factor != 1 || taps <= 64) &&
           (tx ? (gain == 0 || gain == -6) : (gain == 6 || gain == 0 || gain == -6 || gain == -12));
}

static int fir_idle(void)
{
    if (!sdr_state.rf_ready || !ad9361_phy) {
        return sdr_command_error("rf_not_initialized use=rf_reinit!", sdr_state.rf_error);
    }
    if (sdr_state.tx_enabled || sdr_state.usb_rx || sdr_state.usb_tx) {
        return sdr_command_error("busy stop_tx_and_usb_before_clock_or_fir_change", -1);
    }
    return 0;
}

int sdr_fir_capture(SdrFirState *s)
{
    fir_init_snapshot();
    s->rx = active_rx_fir;
    s->tx = active_tx_fir;
    s->rx_enabled = !ad9361_phy->bypass_rx_fir;
    s->tx_enabled = !ad9361_phy->bypass_tx_fir;
    if (ad9361_get_trx_path_clks(ad9361_phy, s->rx_clocks, s->tx_clocks) < 0 ||
        ad9361_get_rx_rf_bandwidth(ad9361_phy, &s->rx_bandwidth) < 0 ||
        ad9361_get_tx_rf_bandwidth(ad9361_phy, &s->tx_bandwidth) < 0) {
        return -1;
    }
    return 0;
}

static int fir_ratio(uint32_t parent, uint32_t child, unsigned max)
{
    return child && parent >= child && parent % child == 0 && parent / child <= max;
}

int sdr_fir_validate(const SdrFirState *s)
{
    unsigned d, i;
    if (!fir_parameters(0, s->rx.rx_coef_size, s->rx.rx_dec, s->rx.rx_gain) ||
        !fir_parameters(1, s->tx.tx_coef_size, s->tx.tx_int, s->tx.tx_gain)) {
        return -1;
    }
    for (i = 0; i < 6; ++i) {
        if (!s->rx_clocks[i] || !s->tx_clocks[i]) {
            return -1;
        }
    }
    if (s->rx_clocks[0] != s->tx_clocks[0] || s->rx_clocks[0] < MIN_BBPLL_FREQ ||
        s->rx_clocks[0] > MAX_BBPLL_FREQ || s->rx_clocks[1] < MIN_ADC_CLK ||
        s->rx_clocks[1] > MAX_ADC_CLK || s->tx_clocks[1] > MAX_DAC_CLK ||
        s->rx_clocks[5] != s->tx_clocks[5] || s->rx_clocks[5] > 61440000U ||
        ad9361_phy->rx_eq_2tx) {
        return -1;
    }
    if (!fir_ratio(s->rx_clocks[0], s->rx_clocks[1], 64)) {
        return -1;
    }
    d = s->rx_clocks[0] / s->rx_clocks[1];
    if (d < 2 || (d & (d - 1))) {
        return -1;
    }
    if (!fir_ratio(s->rx_clocks[1], s->tx_clocks[1], 2) ||
        !fir_ratio(s->rx_clocks[1], s->rx_clocks[2], 3) ||
        !fir_ratio(s->tx_clocks[1], s->tx_clocks[2], 3)) {
        return -1;
    }
    for (i = 2; i < 4; ++i) {
        if (!fir_ratio(s->rx_clocks[i], s->rx_clocks[i + 1], 2) ||
            !fir_ratio(s->tx_clocks[i], s->tx_clocks[i + 1], 2)) {
            return -1;
        }
    }
    if ((uint64_t)s->rx_clocks[5] * (s->rx_enabled ? s->rx.rx_dec : 1) != s->rx_clocks[4] ||
        (uint64_t)s->tx_clocks[5] * (s->tx_enabled ? s->tx.tx_int : 1) != s->tx_clocks[4]) {
        return -1;
    }
    if (s->tx_enabled && s->tx.tx_coef_size > (s->tx_clocks[1] / s->tx_clocks[5]) * 16U) {
        return -1;
    }
    if (s->rx_enabled &&
        s->rx.rx_coef_size >
            ((s->rx_clocks[1] / (s->rx_clocks[1] == s->rx_clocks[2] ? 1U : 2U)) / s->rx_clocks[5]) *
                16U) {
        return -1;
    }
    return 0;
}

/* Calculation changes only driver fields temporarily, never hardware. No
 * validate_enable_fir(): that helper silently falls back to 1/1.5 MSPS. */
static int fir_calculate(SdrFirState *s, uint32_t rate)
{
    unsigned br = ad9361_phy->bypass_rx_fir, bt = ad9361_phy->bypass_tx_fir;
    unsigned rd = ad9361_phy->rx_fir_dec, ti = ad9361_phy->tx_fir_int;
    int rc;
    if (!rate || rate > 61440000U) {
        return -1;
    }
    ad9361_phy->bypass_rx_fir = !s->rx_enabled;
    ad9361_phy->bypass_tx_fir = !s->tx_enabled;
    ad9361_phy->rx_fir_dec = s->rx.rx_dec;
    ad9361_phy->tx_fir_int = s->tx.tx_int;
    rc = ad9361_calculate_rf_clock_chain(ad9361_phy, rate, ad9361_phy->rate_governor, s->rx_clocks,
                                         s->tx_clocks);
    ad9361_phy->bypass_rx_fir = br;
    ad9361_phy->bypass_tx_fir = bt;
    ad9361_phy->rx_fir_dec = rd;
    ad9361_phy->tx_fir_int = ti;
    if (rc < 0 || s->rx_clocks[5] != rate || s->tx_clocks[5] != rate) {
        return -1;
    }
    return sdr_fir_validate(s);
}

/* The local ADI loader can ignore SPI write errors. Verify both physical
 * channels explicitly, even in non-DEBUG builds. Always restore access regs.
 * TX taps read back only in the interpolation mode the loader wrote them in
 * (0x002[1:0] = factor); in bypass an x2/x4 filter reads back wrong. */
static int fir_verify_coeff(unsigned tx, const int16_t *coef, unsigned taps, int gain,
                            unsigned factor)
{
    unsigned off = tx ? 0 : REG_RX_FILTER_COEF_ADDR - REG_TX_FILTER_COEF_ADDR;
    int conf = ad9361_spi_read(ad9361_phy->spi, REG_TX_FILTER_CONF + off), oldgain = 0;
    int filter_ctrl = -1;
    unsigned ch, i;
    int lo, hi, rc = 0;
    if (conf < 0 || ((((unsigned)conf & FIR_NUM_TAPS(7)) >> 5) + 1U) * 16U != taps) {
        return -1;
    }
    if (tx) {
        if ((-6 * !!(conf & TX_FIR_GAIN_6DB)) != gain) {
            return -1;
        }
        ad9361_ensm_force_state(ad9361_phy, ENSM_STATE_ALERT);
        filter_ctrl = ad9361_spi_read(ad9361_phy->spi, REG_TX_ENABLE_FILTER_CTRL);
        if (filter_ctrl < 0 ||
            ad9361_spi_write(ad9361_phy->spi, REG_TX_ENABLE_FILTER_CTRL,
                             ((unsigned)filter_ctrl & ~TX_FIR_ENABLE_INTERPOLATION(~0)) |
                                 TX_FIR_ENABLE_INTERPOLATION(factor == 4 ? 3 : factor)) < 0) {
            rc = -1;
        }
    } else {
        oldgain = ad9361_spi_read(ad9361_phy->spi, REG_RX_FILTER_GAIN);
        if (oldgain < 0 || 6 - 6 * (oldgain & 3) != gain) {
            return -1;
        }
        if (ad9361_spi_write(ad9361_phy->spi, REG_RX_FILTER_GAIN, 0) < 0) {
            rc = -1;
        }
    }
    for (ch = 1; ch <= 2 && !rc; ++ch) {
        if (ad9361_spi_write(ad9361_phy->spi, REG_TX_FILTER_CONF + off,
                             ((unsigned)conf & ~FIR_SELECT(3)) | FIR_SELECT(ch) | FIR_START_CLK) <
            0) {
            rc = -1;
            break;
        }
        for (i = 0; i < taps; ++i) {
            if (ad9361_spi_write(ad9361_phy->spi, REG_TX_FILTER_COEF_ADDR + off, i) < 0) {
                rc = -1;
                break;
            }
            lo = ad9361_spi_read(ad9361_phy->spi, REG_TX_FILTER_COEF_READ_DATA_1 + off);
            hi = ad9361_spi_read(ad9361_phy->spi, REG_TX_FILTER_COEF_READ_DATA_2 + off);
            if (lo < 0 || hi < 0 || (uint16_t)(lo | ((unsigned)hi << 8)) != (uint16_t)coef[i]) {
                rc = -1;
                break;
            }
        }
    }
    if (ad9361_spi_write(ad9361_phy->spi, REG_TX_FILTER_CONF + off, (unsigned)conf) < 0) {
        rc = -1;
    }
    if (!tx && ad9361_spi_write(ad9361_phy->spi, REG_RX_FILTER_GAIN, (unsigned)oldgain) < 0) {
        rc = -1;
    }
    if (tx) {
        if (filter_ctrl >= 0 &&
            ad9361_spi_write(ad9361_phy->spi, REG_TX_ENABLE_FILTER_CTRL, (unsigned)filter_ctrl) < 0) {
            rc = -1;
        }
        ad9361_ensm_restore_prev_state(ad9361_phy);
    }
    return rc;
}

static int fir_verify(const SdrFirState *s)
{
    uint32_t rx_clocks[6], tx_clocks[6], rx_bandwidth, tx_bandwidth;
    int v;
    unsigned i;
    if (ad9361_get_trx_path_clks(ad9361_phy, rx_clocks, tx_clocks) < 0) {
        return -1;
    }
    for (i = 0; i < 6; ++i) {
        if (rx_clocks[i] != s->rx_clocks[i] || tx_clocks[i] != s->tx_clocks[i]) {
            return -1;
        }
    }
    if (ad9361_get_rx_rf_bandwidth(ad9361_phy, &rx_bandwidth) < 0 ||
        ad9361_get_tx_rf_bandwidth(ad9361_phy, &tx_bandwidth) < 0 ||
        rx_bandwidth != s->rx_bandwidth || tx_bandwidth != s->tx_bandwidth) {
        return -1;
    }
    v = ad9361_spi_read(ad9361_phy->spi, REG_RX_ENABLE_FILTER_CTRL);
    if (v < 0 || ((unsigned)v & RX_FIR_ENABLE_DECIMATION(~0)) !=
                     (s->rx_enabled ? (s->rx.rx_dec == 4 ? 3 : s->rx.rx_dec) : 0)) {
        return -1;
    }
    v = ad9361_spi_read(ad9361_phy->spi, REG_TX_ENABLE_FILTER_CTRL);
    if (v < 0 || ((unsigned)v & TX_FIR_ENABLE_INTERPOLATION(~0)) !=
                     (s->tx_enabled ? (s->tx.tx_int == 4 ? 3 : s->tx.tx_int) : 0)) {
        return -1;
    }
    return fir_verify_coeff(0, s->rx.rx_coef, s->rx.rx_coef_size, s->rx.rx_gain,
                            s->rx.rx_dec) ||
                   fir_verify_coeff(1, s->tx.tx_coef, s->tx.tx_coef_size, s->tx.tx_gain,
                                    s->tx.tx_int)
               ? -1
               : 0;
}

int sdr_fir_program(const SdrFirState *s)
{
    uint32_t rx_clocks[6], tx_clocks[6];
    int rc;
    pps_monitor_nominal(0);
    ad9361_phy->bypass_rx_fir = !s->rx_enabled;
    ad9361_phy->bypass_tx_fir = !s->tx_enabled;
    ad9361_phy->filt_valid = 0;
    rc = ad9361_set_rx_fir_config(ad9361_phy, s->rx);
    if (rc < 0) {
        return rc;
    }
    rc = ad9361_set_tx_fir_config(ad9361_phy, s->tx);
    if (rc < 0) {
        return rc;
    }
    memcpy(rx_clocks, s->rx_clocks, sizeof(rx_clocks));
    memcpy(tx_clocks, s->tx_clocks, sizeof(tx_clocks));
    rc = ad9361_set_trx_clock_chain(ad9361_phy, rx_clocks, tx_clocks);
    if (rc < 0) {
        return rc;
    }
    rc = ad9361_update_rf_bandwidth(ad9361_phy, s->rx_bandwidth, s->tx_bandwidth);
    if (rc < 0) {
        return rc;
    }
    if (!ad9361_phy->pdata->dig_interface_tune_fir_disable) {
        rc = ad9361_dig_tune(
            ad9361_phy, 0, (s->rx_enabled || s->tx_enabled) ? SKIP_STORE_RESULT : RESTORE_DEFAULT);
        if (rc < 0) {
            return rc;
        }
    }
    /* Local dig_tune(max_freq=0) masks PN failure. Check the resulting delays
     * at this exact rate without touching the sample clock or test RF output. */
    rc = ad9361_digital_check(ad9361_phy, 0);
    if (rc < 0) {
        return rc;
    }
    rc = ad9361_digital_check(ad9361_phy, 1);
    if (rc < 0) {
        return rc;
    }
    if (sdr_set_tx_gate(0) < 0) {
        return -1;
    }
    if (sdr_correction_verify() < 0) {
        return -1;
    }
    if (sdr_external_lo_verify() < 0) {
        return -1;
    }
    return fir_verify(s);
}

static int fir_apply(const SdrFirState *next, const SdrFirState *old)
{
    int rc, rollback;
    if (sdr_fir_validate(next) < 0) {
        return sdr_command_error("fir_or_clock_invalid applied=unchanged", -1);
    }
    if (sdr_set_tx_gate(0) < 0) {
        return sdr_command_error("fir_or_clock_gate applied=unchanged", -1);
    }
    recover_fir = *old;
    recover_fir_valid = 1;
    rc = sdr_fir_program(next);
    if (rc < 0) {
        rollback = sdr_fir_program(old);
        if (rollback < 0) {
            sdr_state.rf_ready = sdr_state.digital_ready = 0;
            sdr_state.rf_error = rollback;
            fir_snapshot_verified = 0;
        }
        sdr_set_tx_gate(0);
        console_print(
            "ERR fir_or_clock_apply code=%d rollback=%s applied=%s tx_enable=0 usb_data_en=0,0\n",
            rc, rollback < 0 ? "failed" : "verified", rollback < 0 ? "unknown" : "restored");
        return -1;
    }
    active_rx_fir = next->rx;
    active_tx_fir = next->tx;
    fir_snapshot_verified = 1;
    recover_fir = *next;
    sdr_state.rf_error = 0;
    return 0;
}

int sdr_clock_apply(const uint32_t rx[6], const uint32_t tx[6])
{
    SdrFirState old, next;
    if (!rx || !tx || fir_idle() < 0) {
        return -1;
    }
    if (sdr_fir_capture(&old) < 0) {
        return sdr_command_error("clock_snapshot", -1);
    }
    next = old;
    memcpy(next.rx_clocks, rx, sizeof(next.rx_clocks));
    memcpy(next.tx_clocks, tx, sizeof(next.tx_clocks));
    return fir_apply(&next, &old);
}

int sdr_clock_set_rate(uint32_t rate)
{
    SdrFirState old, next;
    if (fir_idle() < 0) {
        return -1;
    }
    if (sdr_fir_capture(&old) < 0) {
        return sdr_command_error("clock_snapshot", -1);
    }
    next = old;
    if (fir_calculate(&next, rate) < 0) {
        return sdr_command_error("clock_rate_invalid applied=unchanged", -1);
    }
    return fir_apply(&next, &old);
}

static int fir_enable(unsigned rx_enabled, unsigned tx_enabled)
{
    SdrFirState old, next;
    if (fir_idle() < 0) {
        return -1;
    }
    if (sdr_fir_capture(&old) < 0) {
        return sdr_command_error("fir_snapshot", -1);
    }
    next = old;
    next.rx_enabled = rx_enabled;
    next.tx_enabled = tx_enabled;
    if (fir_calculate(&next, old.tx_clocks[5]) < 0) {
        return sdr_command_error("fir_rate_invalid applied=unchanged", -1);
    }
    return fir_apply(&next, &old);
}

static void fir_report(unsigned tx)
{
    console_print(
        "%s_fir_config=%u taps=%u factor=%lu gain_dB=%ld enabled=%u crc32=%lu verified=%u source=verified_snapshot\n",
        tx ? "tx" : "rx", tx ? custom_tx_fir : custom_rx_fir,
        tx ? active_tx_fir.tx_coef_size : active_rx_fir.rx_coef_size,
        (unsigned long)(tx ? active_tx_fir.tx_int : active_rx_fir.rx_dec),
        (long)(tx ? active_tx_fir.tx_gain : active_rx_fir.rx_gain),
        tx ? !ad9361_phy->bypass_tx_fir : !ad9361_phy->bypass_rx_fir,
        (unsigned long)(tx ? fir_crc(active_tx_fir.tx_coef, active_tx_fir.tx_coef_size)
                           : fir_crc(active_rx_fir.rx_coef, active_rx_fir.rx_coef_size)),
        fir_snapshot_verified);
}

int sdr_fir_profile(unsigned tx, unsigned set)
{
    SdrFirState old, next;
    fir_init_snapshot();
    if (set) {
        if (fir_idle() < 0) {
            return -1;
        }
        if (sdr_fir_capture(&old) < 0) {
            return sdr_command_error("fir_snapshot", -1);
        }
        next = old;
        if (tx) {
            next.tx = tx_fir_config;
            next.tx.tx = 3;
            next.tx_enabled = 0;
        } else {
            next.rx = rx_fir_config;
            next.rx.rx = 3;
            next.rx_enabled = 0;
        }
        if (fir_calculate(&next, old.tx_clocks[5]) < 0) {
            return sdr_command_error("fir_rate_invalid applied=unchanged", -1);
        }
        if (fir_apply(&next, &old) < 0) {
            return -1;
        }
        if (tx) {
            custom_tx_fir = 0;
        } else {
            custom_rx_fir = 0;
        }
    }
    fir_report(tx);
    return 0;
}

/* 分段上传命令独立于普通命令表，避免丢失 token/CRC/回滚校验。 */
typedef enum {
    FIR_NONE = 0,
    FIR_RX_BEGIN,
    FIR_TX_BEGIN,
    FIR_DATA,
    FIR_COMMIT,
    FIR_ABORT,
    FIR_RX_READ,
    FIR_TX_READ,
    FIR_ENABLE_BOTH,
    FIR_ENABLE_RX,
    FIR_ENABLE_TX
} FirOperation;

int sdr_fir_execute(const char *line)
{
    int64_t v[10];
    unsigned n, i, tx = 0;
    FirOperation op = FIR_NONE;
    const char *body = NULL;

    static const struct {
        const char *name;
        FirOperation operation;
    } commands[] = {{"rx_fir_begin=", FIR_RX_BEGIN}, {"tx_fir_begin=", FIR_TX_BEGIN},
                    {"fir_data=", FIR_DATA},         {"fir_commit=", FIR_COMMIT},
                    {"fir_abort=", FIR_ABORT},       {"rx_fir_read=", FIR_RX_READ},
                    {"tx_fir_read=", FIR_TX_READ},   {"fir_enable=", FIR_ENABLE_BOTH},
                    {"rx_fir_en=", FIR_ENABLE_RX},   {"tx_fir_en=", FIR_ENABLE_TX}};

    for (i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (!strncmp(line, commands[i].name, strlen(commands[i].name))) {
            op = commands[i].operation;
            body = line + strlen(commands[i].name);
            break;
        }
    }
    if (!body) {
        return 0;
    }
    if (strlen(line) > 127 || fir_numbers(body, v, 10, &n) < 0) {
        return sdr_command_error("fir_invalid_decimal_parameters", -1);
    }
    fir_init_snapshot();
    if (op == FIR_RX_BEGIN || op == FIR_TX_BEGIN) {
        tx = op == FIR_TX_BEGIN;
        if (n != 4 || v[0] < 1 || v[0] > UINT32_MAX || v[1] < 0 || v[1] > 128 || v[2] < 0 ||
            v[2] > 4 || v[3] < -12 || v[3] > 6 ||
            !fir_parameters(tx, (unsigned)v[1], (unsigned)v[2], (int)v[3])) {
            return sdr_command_error("fir_invalid_config", -1);
        }
        if (fir_idle() < 0) {
            return -1;
        }
        /* A fresh begin replaces only the RAM staging area. This lets a new
         * host recover after disconnect; the abandoned token becomes invalid. */
        memset(&fir_upload, 0, sizeof(fir_upload));
        fir_upload.token = (uint32_t)v[0];
        fir_upload.tx = tx;
        fir_upload.taps = (unsigned)v[1];
        fir_upload.factor = (unsigned)v[2];
        fir_upload.gain = (int)v[3];
        console_print("fir_begin=%lu direction=%s taps=%u\n", (unsigned long)fir_upload.token,
                      tx ? "tx" : "rx", fir_upload.taps);
        return 1;
    }
    if (op == FIR_RX_READ || op == FIR_TX_READ) {
        const int16_t *coef;
        unsigned taps;
        if (!sdr_state.rf_ready || !ad9361_phy) {
            return sdr_command_error("rf_not_initialized", sdr_state.rf_error);
        }
        tx = op == FIR_TX_READ;
        coef = tx ? active_tx_fir.tx_coef : active_rx_fir.rx_coef;
        taps = tx ? active_tx_fir.tx_coef_size : active_rx_fir.rx_coef_size;
        if (n != 2 || v[0] < 0 || v[1] < 1 || v[1] > 8 || v[0] + v[1] > taps) {
            return sdr_command_error("fir_read_range", -1);
        }
        console_print("%s_fir_read=%u", tx ? "tx" : "rx", (unsigned)v[0]);
        for (i = 0; i < (unsigned)v[1]; ++i) {
            console_print(" %d", coef[(unsigned)v[0] + i]);
        }
        console_print("\n");
        return 1;
    }
    if (op == FIR_ENABLE_BOTH || op == FIR_ENABLE_RX || op == FIR_ENABLE_TX) {
        unsigned rx_enabled, tx_enabled;
        if (n != (op == FIR_ENABLE_BOTH ? 2U : 1U) || v[0] < 0 || v[0] > 1 ||
            (n == 2 && (v[1] < 0 || v[1] > 1))) {
            return sdr_command_error("fir_enable_range", -1);
        }
        if (fir_idle() < 0) {
            return -1;
        }
        rx_enabled = op == FIR_ENABLE_TX ? !ad9361_phy->bypass_rx_fir : (unsigned)v[0];
        tx_enabled = op == FIR_ENABLE_RX ? !ad9361_phy->bypass_tx_fir : (unsigned)v[n - 1];
        if (fir_enable(rx_enabled, tx_enabled) < 0) {
            return -1;
        }
        fir_report(0);
        fir_report(1);
        return 1;
    }
    if (!n || v[0] < 1 || v[0] > UINT32_MAX || !fir_upload.token ||
        (uint32_t)v[0] != fir_upload.token) {
        return sdr_command_error("fir_token_mismatch", -1);
    }
    if (op == FIR_ABORT) {
        if (n != 1) {
            return sdr_command_error("fir_abort_parameters", -1);
        }
        fir_upload.token = 0;
        console_print("fir_abort=ok\n");
        return 1;
    }
    if (op == FIR_DATA) {
        if (n < 3 || n > 10 || v[1] != (int64_t)fir_upload.count ||
            fir_upload.count + n - 2 > fir_upload.taps) {
            return sdr_command_error("fir_data_offset_or_count", -1);
        }
        for (i = 2; i < n; ++i) {
            if (v[i] < -32768 || v[i] > 32767) {
                return sdr_command_error("fir_coefficient_range", -1);
            }
        }
        for (i = 2; i < n; ++i) {
            fir_upload.coef[fir_upload.count++] = (int16_t)v[i];
        }
        console_print("fir_data=%lu next=%u\n", (unsigned long)fir_upload.token, fir_upload.count);
        return 1;
    }
    if (op == FIR_COMMIT) {
        SdrFirState old, next;
        if (n != 3 || v[1] < 0 || v[1] > 1 || v[2] < 0 || v[2] > UINT32_MAX ||
            fir_upload.count != fir_upload.taps ||
            (uint32_t)v[2] != fir_crc(fir_upload.coef, fir_upload.taps)) {
            return sdr_command_error("fir_commit_incomplete_or_crc", -1);
        }
        if (fir_idle() < 0) {
            return -1;
        }
        if (sdr_fir_capture(&old) < 0) {
            return sdr_command_error("fir_snapshot", -1);
        }
        next = old;
        tx = fir_upload.tx;
        if (tx) {
            next.tx.tx = 3;
            next.tx.tx_coef_size = (uint8_t)fir_upload.taps;
            next.tx.tx_int = fir_upload.factor;
            next.tx.tx_gain = fir_upload.gain;
            memcpy(next.tx.tx_coef, fir_upload.coef, sizeof(next.tx.tx_coef));
            next.tx_enabled = (unsigned)v[1];
        } else {
            next.rx.rx = 3;
            next.rx.rx_coef_size = (uint8_t)fir_upload.taps;
            next.rx.rx_dec = fir_upload.factor;
            next.rx.rx_gain = fir_upload.gain;
            memcpy(next.rx.rx_coef, fir_upload.coef, sizeof(next.rx.rx_coef));
            next.rx_enabled = (unsigned)v[1];
        }
        if (fir_calculate(&next, old.tx_clocks[5]) < 0) {
            return sdr_command_error("fir_rate_invalid applied=unchanged", -1);
        }
        if (fir_apply(&next, &old) < 0) {
            return -1;
        }
        if (tx) {
            custom_tx_fir = 1;
        } else {
            custom_rx_fir = 1;
        }
        fir_upload.token = 0;
        fir_report(tx);
        return 1;
    }
    return sdr_command_error("fir_invalid_command", -1);
}

/* FIR_TRANSACTION_END */

int sdr_fir_boot(void)
{
    int rc;
    fir_init_snapshot();
    rc = ad9361_set_rx_fir_config(ad9361_phy, active_rx_fir);
    if (!rc) {
        rc = ad9361_set_tx_fir_config(ad9361_phy, active_tx_fir);
    }
    if (!rc) {
        rc = fir_verify_coeff(0, active_rx_fir.rx_coef, active_rx_fir.rx_coef_size,
                              active_rx_fir.rx_gain, active_rx_fir.rx_dec);
    }
    if (!rc) {
        rc = fir_verify_coeff(1, active_tx_fir.tx_coef, active_tx_fir.tx_coef_size,
                              active_tx_fir.tx_gain, active_tx_fir.tx_int);
    }
    return rc;
}

unsigned sdr_fir_recovery(SdrFirState *state)
{
    if (recover_fir_valid) {
        *state = recover_fir;
    }
    return recover_fir_valid;
}

void sdr_fir_set_verified(unsigned verified)
{
    fir_snapshot_verified = verified;
}

void sdr_fir_save_recovery(const SdrFirState *state)
{
    recover_fir = *state;
    recover_fir_valid = 1;
    fir_snapshot_verified = 1;
}
