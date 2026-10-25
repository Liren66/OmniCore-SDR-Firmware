#include "sdr_reference.h"
#include "sdr_runtime.h"
#include "console.h"
#include "adf4002.h"

/* Board reference control. Main-loop reference selection. */
int sdr_reference_status(void)
{
    struct adf4002_status s;
    int rc = adf4002_get_status(&s);
    if (rc < 0) {
        return sdr_command_error("adf4002_status", rc);
    }
    console_print(
        "adf4002_status source=%u source_known=%u initialized=%u gpio_verified=%u chip_verified=%u lock_known=%u locked=%d led_known=%u led_on=%d led_mode=%u reference_hz=%lu oscillator_hz=%lu r_counter=%u n_counter=%u last_error=%ld visual_confirmation_required=1\n",
        s.external_reference, s.source_known, s.initialized, s.gpio_verified, s.chip_verified,
        s.lock_known, (int)s.locked, s.led_known, (int)s.led_on, s.led_mode,
        (unsigned long)s.reference_hz, (unsigned long)s.oscillator_hz, s.r_counter, s.n_counter,
        (long)s.last_error);
    return 0;
}

int sdr_reference_command(unsigned set, unsigned enabled)
{
    struct adf4002_status s;
    uint8_t actual = 0;
    int rc;
    if (set && (sdr_state.tx_enabled || sdr_state.usb_rx || sdr_state.usb_tx)) {
        return sdr_command_error("busy stop_tx_and_usb_before_reference_change", -1);
    }
    if (set) {
        adf4002_get_status(&s);
        if (!s.initialized) {
            rc = adf4002_init();
            if (rc < 0) {
                return sdr_command_error("adf4002_init", rc);
            }
        }
        rc = adf4002_set_external_reference((uint8_t)enabled);
        if (rc < 0) {
            sdr_state.rf_ready = sdr_state.digital_ready = 0;
            sdr_state.rf_error = rc;
            sdr_set_tx_gate(0);
            return sdr_command_error(
                "adf4002_apply reference=unknown recover=clock_ref_source=0_then_rf_reinit!", rc);
        }
    }
    rc = adf4002_get_external_reference(&actual);
    if (rc < 0) {
        return sdr_command_error("adf4002_source_unavailable", rc);
    }
    console_print(
        "clock_ref_source=%u reference_hz=%lu oscillator_hz=%lu lock_known=0 locked=-1 chip_verified=0\n",
        actual, (unsigned long)ADF4002_REFERENCE_HZ, (unsigned long)ADF4002_OSCILLATOR_HZ);
    return 0;
}

int sdr_reference_self_test(void)
{
    int rc;
    if (sdr_state.tx_enabled || sdr_state.usb_rx || sdr_state.usb_tx) {
        return sdr_command_error("busy stop_tx_and_usb_before_reference_test", -1);
    }
    rc = adf4002_self_test();
    if (rc < 0) {
        sdr_state.rf_ready = sdr_state.digital_ready = 0;
        sdr_state.rf_error = rc;
        sdr_set_tx_gate(0);
        return sdr_command_error(
            "adf4002_self_test gpio=failed chip=unverified recover=clock_ref_source=0_then_rf_reinit!",
            rc);
    }
    console_print(
        "adf4002_self_test gpio_verified=1 chip_verified=0 visual_confirmation_required=1 led_pulses=3 charge_pump=high_z_during_test source=restored\n");
    return sdr_reference_status();
}
