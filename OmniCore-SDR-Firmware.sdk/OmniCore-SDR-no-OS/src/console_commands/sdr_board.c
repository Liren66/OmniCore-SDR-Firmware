#include "sdr_board.h"
#include "sdr_runtime.h"
#include "board_monitor.h"
#include "console.h"

static unsigned board_initializing = 1;
static unsigned board_recovering;

void sdr_board_service(unsigned recovering)
{
    unsigned failed;
    board_recovering = recovering;
    failed = !board_initializing &&
             (!sdr_state.rf_ready || !sdr_state.digital_ready || !sdr_state.bridge_ready);
    board_monitor_activity(board_initializing, failed, recovering, sdr_state.usb_rx,
                           sdr_state.tx_enabled);
    board_monitor_service();
}

void sdr_board_initializing(unsigned active)
{
    board_initializing = active;
    sdr_board_service(board_recovering);
}

static void current_status(BoardMonitorStatus *status)
{
    sdr_board_service(board_recovering);
    board_monitor_status(status);
}

void sdr_board_rgb_status(void)
{
    BoardMonitorStatus status;
    current_status(&status);
    console_print(
        "rgb_status available=%u enabled=%u brightness_pct=15 state=%s red=%u green=%u blue=%u blink_hz=%u\n",
        status.available, status.rgb_enabled, board_monitor_rgb_name(status.rgb_state), status.red,
        status.green, status.blue, status.blink_hz);
}

void sdr_board_fan_status(void)
{
    BoardMonitorStatus status;
    current_status(&status);
    console_print(
        "fan_status available=%u enabled=%u frequency_hz=25000 duty_permille=%u target_permille=%u boost=%u failsafe=%u temp_valid=%u temp_mC=",
        status.available, status.fan_enabled, status.fan_duty_permille, status.fan_target_permille,
        status.fan_boost, status.fan_failsafe, status.temperature_valid);
    if (status.temperature_valid) {
        console_print("%ld", (long)status.temperature_mc);
    } else {
        console_print("unknown");
    }
    console_print(" temp_age_ms=%lu overtemp=%u source=zynq\n",
                  (unsigned long)status.temperature_age_ms, status.overtemperature);
}

void sdr_board_temperature_status(void)
{
    BoardMonitorStatus status;
    current_status(&status);
    console_print("core_temp source=zynq valid=%u milli_celsius=", status.temperature_valid);
    if (status.temperature_valid) {
        console_print("%ld", (long)status.temperature_mc);
    } else {
        console_print("unknown");
    }
    console_print(" age_ms=%lu overtemp=%u\n", (unsigned long)status.temperature_age_ms,
                  status.overtemperature);
}

void sdr_board_status(void)
{
    sdr_board_temperature_status();
    sdr_board_rgb_status();
    sdr_board_fan_status();
}

int sdr_board_rgb_command(unsigned write, unsigned enabled)
{
    BoardMonitorStatus status;
    if (!board_monitor_available()) {
        return sdr_command_error("board_rgb_unavailable", -1);
    }
    if (write && board_monitor_rgb_enable(enabled) < 0) {
        return sdr_command_error("rgb_enable_failed", -1);
    }
    current_status(&status);
    console_print("rgb_enable=%u brightness_pct=15\n", status.rgb_enabled);
    return 0;
}

int sdr_board_fan_command(unsigned write, unsigned enabled)
{
    BoardMonitorStatus status;
    if (!board_monitor_available()) {
        return sdr_command_error("board_fan_unavailable", -1);
    }
    if (write && board_monitor_fan_enable(enabled) < 0) {
        return sdr_command_error("fan_enable_failed", -1);
    }
    current_status(&status);
    console_print("fan_enable=%u mode=%s\n", status.fan_enabled,
                  status.fan_enabled ? "auto" : "off");
    return 0;
}
