#include "sdr_correction.h"
#include "sdr_runtime.h"
#include "console.h"
#include <string.h>

/* AD9361 RX tracking controls, shared by RX1/RX2.
 * Disabling tracking freezes the learned correction; it does not bypass the
 * correction blocks or erase the calibration words (ADI UG-570).
 * The register transaction is covered by fault-injection tests. */
typedef struct {
    uint8_t reg[5];
    unsigned bbdc, rfdc, quad;
} CorrectionState;

static const uint32_t correction_regs[5] = {REG_CALIBRATION_CONFIG_2, REG_CALIBRATION_CONFIG_3,
                                            REG_DC_OFFSET_CONFIG2, REG_RX_QUAD_GAIN2,
                                            REG_CALIBRATION_CONFIG_1};
/* Last fully verified target survives an RF failure and rf_reinit!. */
static unsigned correction_target_valid;
static CorrectionState correction_target;

static unsigned correction_quad_mask(void)
{
    if (ad9361_phy->pdata->rx2tx2) {
        return ENABLE_TRACKING_MODE_CH1 | ENABLE_TRACKING_MODE_CH2;
    }
    return ad9361_phy->pdata->rx1tx1_mode_use_rx_num == 1 ? ENABLE_TRACKING_MODE_CH1
                                                          : ENABLE_TRACKING_MODE_CH2;
}

static int correction_read(CorrectionState *s)
{
    unsigned i, q, mask = correction_quad_mask();
    int v;
    for (i = 0; i < 5; ++i) {
        v = ad9361_spi_read(ad9361_phy->spi, correction_regs[i]);
        if (v < 0) {
            return v;
        }
        s->reg[i] = (uint8_t)v;
    }
    s->bbdc = !!(s->reg[2] & ENABLE_BB_DC_OFFSET_TRACKING);
    s->rfdc = !!(s->reg[2] & ENABLE_RF_OFFSET_TRACKING);
    q = s->reg[4] & (ENABLE_TRACKING_MODE_CH1 | ENABLE_TRACKING_MODE_CH2);
    if ((q && q != mask) || (s->reg[4] & (ENABLE_PHASE_CORR | ENABLE_GAIN_CORR)) !=
                                (ENABLE_PHASE_CORR | ENABLE_GAIN_CORR)) {
        return -EIO;
    }
    s->quad = q != 0;
    return 0;
}

static void correction_cache(const CorrectionState *s)
{
    ad9361_phy->bbdc_track_en = s->bbdc;
    ad9361_phy->rfdc_track_en = s->rfdc;
    ad9361_phy->quad_track_en = s->quad;
}

static int correction_verify(const CorrectionState *target, CorrectionState *actual)
{
    unsigned dc, quad;
    int rc = correction_read(actual);
    if (rc < 0) {
        return rc;
    }
    dc = USE_WAIT_COUNTER_FOR_RF_DC_INIT_CAL |
         DC_OFFSET_UPDATE(ad9361_phy->pdata->dc_offset_update_events) |
         (target->bbdc ? ENABLE_BB_DC_OFFSET_TRACKING : 0) |
         (target->rfdc ? ENABLE_RF_OFFSET_TRACKING : 0);
    quad = ENABLE_PHASE_CORR | ENABLE_GAIN_CORR | FREE_RUN_MODE | ENABLE_CORR_WORD_DECIMATION |
           (target->quad ? correction_quad_mask() : 0);
    if (actual->reg[0] != (CALIBRATION_CONFIG2_DFLT | K_EXP_PHASE(0x15)) ||
        actual->reg[1] != (PREVENT_POS_LOOP_GAIN | K_EXP_AMPLITUDE(0x15)) || actual->reg[2] != dc ||
        actual->reg[4] != quad ||
        (actual->reg[3] & CORRECTION_WORD_DECIMATION_M(~0)) !=
            CORRECTION_WORD_DECIMATION_M(ad9361_phy->pdata->qec_tracking_slow_mode_en ? 4 : 0)) {
        return -EIO;
    }
    return 0;
}

static int correction_write(const CorrectionState *target, CorrectionState *actual)
{
    int rc = ad9361_tracking_control(ad9361_phy, target->bbdc, target->rfdc, target->quad);
    if (rc < 0) {
        return rc;
    }
    return correction_verify(target, actual);
}

static int correction_rollback(const CorrectionState *old)
{
    unsigned i;
    int rc = 0, v;
    /* Restore the exact prior configuration, including unrelated gain bits. */
    for (i = 0; i < 5; ++i) {
        if (ad9361_spi_write(ad9361_phy->spi, correction_regs[i], old->reg[i]) < 0) {
            rc = -EIO;
        }
    }
    for (i = 0; i < 5; ++i) {
        v = ad9361_spi_read(ad9361_phy->spi, correction_regs[i]);
        if (v < 0 || v != old->reg[i]) {
            rc = -EIO;
        }
    }
    if (!rc) {
        correction_cache(old);
    }
    return rc;
}

int sdr_correction_restore(void)
{
    CorrectionState actual;
    int rc;
    if (!correction_target_valid) {
        /* Native no-OS init enables all three tracking algorithms. */
        memset(&correction_target, 0, sizeof(correction_target));
        correction_target.bbdc = correction_target.rfdc = correction_target.quad = 1;
    }
    rc = correction_write(&correction_target, &actual);
    if (rc < 0) {
        return rc;
    }
    correction_cache(&actual);
    correction_target = actual;
    correction_target_valid = 1;
    return 0;
}

int sdr_correction_verify(void)
{
    CorrectionState actual;
    if (!correction_target_valid) {
        return -EIO;
    }
    return correction_verify(&correction_target, &actual);
}

int sdr_correction_command(unsigned iq, unsigned set, unsigned enabled)
{
    CorrectionState old, next, actual;
    int rc, rollback;
    const char *name = iq ? "rx_iq_correction" : "rx_dc_correction";
    if (!sdr_state.rf_ready || !ad9361_phy) {
        return sdr_command_error("rf_not_initialized use=rf_reinit!", sdr_state.rf_error);
    }
    if (set && (sdr_state.tx_enabled || sdr_state.usb_rx || sdr_state.usb_tx)) {
        return sdr_command_error("busy stop_tx_and_usb_before_rx_correction_change", -1);
    }
    rc = correction_read(&old);
    if (rc < 0) {
        console_print("ERR %s stage=read code=%d applied=%s\n", name, rc,
                      set ? "unchanged" : "unknown");
        return -1;
    }
    actual = old;
    if (set) {
        next = old;
        if (iq) {
            next.quad = enabled;
        } else {
            next.bbdc = next.rfdc = enabled;
        }
        rc = correction_write(&next, &actual);
        if (rc < 0) {
            rollback = correction_rollback(&old);
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
        correction_cache(&actual);
        correction_target = actual;
        correction_target_valid = 1;
    }
    if (!iq && actual.rfdc != actual.bbdc) {
        console_print("ERR rx_dc_correction inconsistent rfdc=%u bbdc=%u applied=mixed\n",
                      actual.rfdc, actual.bbdc);
        return -1;
    }
    if (iq) {
        console_print("rx_iq_correction=%u verified=1 scope=all_rx mode=tracking\n", actual.quad);
    } else {
        console_print("rx_dc_correction=%u rfdc=%u bbdc=%u verified=1 scope=all_rx mode=tracking\n",
                      actual.rfdc, actual.rfdc, actual.bbdc);
    }
    return 0;
}
