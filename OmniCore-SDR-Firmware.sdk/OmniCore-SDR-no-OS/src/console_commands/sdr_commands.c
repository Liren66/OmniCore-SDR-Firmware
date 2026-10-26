#include "sdr_commands.h"
#include "sdr_iq_format.h"
#include "sdr_runtime.h"
#include "sdr_fir.h"
#include "sdr_clock.h"
#include "sdr_correction.h"
#include "sdr_reference.h"
#include "sdr_external_lo.h"
#include "sdr_pps.h"
#include "sdr_vcxo.h"
#include "sdr_board.h"
#include "console.h"
#include "ch32h417_ip.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define RX_BITS (CH32H417_IP_RX_EN | CH32H417_IP_RX_DUAL | CH32H417_IP_RX_CH1)
#define TX_BITS (CH32H417_IP_TX_EN | CH32H417_IP_TX_DUAL | CH32H417_IP_TX_CH1 | CH32H417_IP_TX_RUN)

/* 扩展命令总表：命令字符串 → 具名回调 → 参数数量/范围 → 帮助。
 * 增加普通命令时先在此登记，再实现同名回调；接口在 sdr_commands.h。
 * FIR/clock 分段上传使用独立事务解析器，见 sdr_fir.c / sdr_clock.c。
 */
static const SdrCommand commands[] = {
    /* 通道选择、数据来源与 TX/USB 开关。多参数命令使用空格分隔。 */
    {"rx_channel?", sdr_cmd_rx_channel, 0, 0, "RX channel mask: 0=off, 1=RX1, 2=RX2, 3=both",
     "rx_channel=3"},
    {"rx_channel=", sdr_cmd_rx_channel, 1, 3, "RX channel mask: 0=off, 1=RX1, 2=RX2, 3=both",
     "rx_channel=3"},
    {"tx_channel?", sdr_cmd_tx_channel, 0, 0, "TX channel mask: 0=off, 1=TX1, 2=TX2, 3=both",
     "tx_channel=3"},
    {"tx_channel=", sdr_cmd_tx_channel, 1, 3, "TX channel mask: 0=off, 1=TX1, 2=TX2, 3=both",
     "tx_channel=3"},
    {"tx_source?", sdr_cmd_tx_source, 0, 0, "TX sample source: 0=DDS, 1=USB", "tx_source=1"},
    {"tx_source=", sdr_cmd_tx_source, 1, 1, "TX sample source: 0=DDS, 1=USB", "tx_source=1"},
    {"tx_enable?", sdr_cmd_tx_enable, 0, 0, "TX output gate; USB requires prefill", "tx_enable=0"},
    {"tx_enable=", sdr_cmd_tx_enable, 1, 1, "TX output gate; USB requires prefill", "tx_enable=0"},
    {"usb_data_en?", sdr_cmd_usb_data, 0, 0, "USB RX upload / TX download enables",
     "usb_data_en=1 0"},
    {"usb_data_en=", sdr_cmd_usb_data, 2, 1, "USB RX upload / TX download enables",
     "usb_data_en=1 0"},
    {"iq_format?", sdr_cmd_iq_format, 0, 0,
     "USB signed IQ bits: default 12; 8 requires 2TX2RX USB", "iq_format?"},
    {"iq_format=", sdr_cmd_iq_format, 1, 12,
     "USB IQ valid bits 8 or 12; stop TX and USB before change", "iq_format=8"},
    /* 时钟查询和默认 FIR；自定义 FIR/时钟上传由下方事务入口处理。 */
    {"clock_chain?", sdr_cmd_clock_chain, 0, 0, "Read RX and TX clock chains in Hz",
     "clock_chain?"},
    {"rx_fir_config?", sdr_cmd_rx_fir_profile, 0, 0, "Read RX FIR / restore default profile 0",
     "rx_fir_config=0"},
    {"rx_fir_config=", sdr_cmd_rx_fir_profile, 1, 0, "Read RX FIR / restore default profile 0",
     "rx_fir_config=0"},
    {"tx_fir_config?", sdr_cmd_tx_fir_profile, 0, 0, "Read TX FIR / restore default profile 0",
     "tx_fir_config=0"},
    {"tx_fir_config=", sdr_cmd_tx_fir_profile, 1, 0, "Read TX FIR / restore default profile 0",
     "tx_fir_config=0"},
    /* AD9361 接收直流 / IQ 跟踪校正。 */
    {"rx_dc_correction?", sdr_cmd_rx_dc_correction, 0, 0,
     "RX RF+baseband DC tracking: 0=freeze, 1=track", "rx_dc_correction=1"},
    {"rx_dc_correction=", sdr_cmd_rx_dc_correction, 1, 1,
     "RX RF+baseband DC tracking: 0=freeze, 1=track", "rx_dc_correction=1"},
    {"rx_iq_correction?", sdr_cmd_rx_iq_correction, 0, 0, "RX IQ tracking: 0=freeze, 1=track",
     "rx_iq_correction=1"},
    {"rx_iq_correction=", sdr_cmd_rx_iq_correction, 1, 1, "RX IQ tracking: 0=freeze, 1=track",
     "rx_iq_correction=1"},
    /* 外部 LO：RX、TX 独立选择，默认内部。 */
    {"rx_lo_source?", sdr_cmd_rx_lo_source, 0, 0, "RX LO: 0=internal, 1=external 2x RF frequency",
     "rx_lo_source=0"},
    {"rx_lo_source=", sdr_cmd_rx_lo_source, 1, 1, "RX LO: 0=internal, 1=external 2x RF frequency",
     "rx_lo_source=0"},
    {"tx_lo_source?", sdr_cmd_tx_lo_source, 0, 0, "TX LO: 0=internal, 1=external 2x RF frequency",
     "tx_lo_source=0"},
    {"tx_lo_source=", sdr_cmd_tx_lo_source, 1, 1, "TX LO: 0=internal, 1=external 2x RF frequency",
     "tx_lo_source=0"},
    /* ADF4002、系统状态及参考源。 */
    {"adf4002_status?", sdr_cmd_adf4002_status, 0, 0,
     "ADF4002 GPIO / reference status; no lock readback", "adf4002_status?"},
    {"adf4002_self_test!", sdr_cmd_adf4002_self_test, 0, 0,
     "Blink D5 three times for visual communication test", "adf4002_self_test!"},
    {"system_info?", sdr_cmd_system_info, 0, 0, "Board identity, protocol and capabilities",
     "system_info?"},
    {"system_status?", sdr_cmd_system_status, 0, 0, "RF, USB, reference and PPS status",
     "system_status?"},
    {"rssi_enable?", sdr_cmd_rssi_enable, 0, 0, "RX RSSI reporting; default=0 after cold boot",
     "rssi_enable?"},
    {"rssi_enable=", sdr_cmd_rssi_enable, 1, 1, "RX RSSI reporting: 0=off, 1=on", "rssi_enable=1"},
    {"rx_rssi?", sdr_cmd_rx_rssi, 0, 0,
     "RX1/RX2 symbol RSSI in relative dB; not calibrated input power; needs rssi_enable=1",
     "rx_rssi?"},
    {"clock_ref_source?", sdr_cmd_clock_reference, 0, 0,
     "Reference: 0=ADF4002 high-Z, 1=external 10M PLL after CH32 PA4 high-Z", "clock_ref_source=0"},
    {"clock_ref_source=", sdr_cmd_clock_reference, 1, 1,
     "Reference: 0=ADF4002 high-Z, 1=external 10M PLL after CH32 PA4 high-Z", "clock_ref_source=0"},
    /* PPS：默认关闭。无 10M 时由 CH32 PA4 驯服 40M；有 10M 时仅作参考。 */
    {"pps_status?", sdr_cmd_pps_status, 0, 0, "PPS signal presence / valid DATA_CLK measurement",
     "pps_status?"},
    {"pps_input_en?", sdr_cmd_pps_input, 0, 0,
     "PPS input; without 10M the CH32 PA4 DAC disciplines 40M; default=0", "pps_input_en=1"},
    {"pps_input_en=", sdr_cmd_pps_input, 1, 1,
     "PPS input; without 10M the CH32 PA4 DAC disciplines 40M; default=0", "pps_input_en=1"},
    {"vcxo_status?", sdr_cmd_vcxo_status, 0, 0,
     "40M tune mode, ADF4002/PA4 ownership and CH32 PPS loop state", "vcxo_status?"},
    /* 板级状态灯、风扇自动温控及 Zynq 内部温度。 */
    {"rgb_enable?", sdr_cmd_rgb_enable, 0, 0, "RGB status indicator; default on, brightness 15%",
     "rgb_enable?"},
    {"rgb_enable=", sdr_cmd_rgb_enable, 1, 1, "RGB status indicator: 0=off, 1=on", "rgb_enable=1"},
    {"rgb_status?", sdr_cmd_rgb_status, 0, 0, "RGB state, brightness and current output color",
     "rgb_status?"},
    {"fan_enable?", sdr_cmd_fan_enable, 0, 0, "Zynq fan temperature control; default off",
     "fan_enable?"},
    {"fan_enable=", sdr_cmd_fan_enable, 1, 1, "Fan: 0=PWM zero, 1=automatic temperature control",
     "fan_enable=1"},
    {"fan_status?", sdr_cmd_fan_status, 0, 0,
     "Fan PWM, target duty, boost, failsafe and Zynq temperature", "fan_status?"},
    {"core_temp?", sdr_cmd_core_temp, 0, 0,
     "Zynq XADC temperature in milli-Celsius; valid flag required", "core_temp?"},
    /* RF 恢复与数字接口测试。 */
    {"rf_reinit!", sdr_cmd_rf_reinit, 0, 0,
     "Stop streams, reinitialize RF, restore verified settings", "rf_reinit!"},
    {"digital_self_test!", sdr_cmd_digital_self_test, 0, 0,
     "Stop streams and check RX PRBS / TX digital loopback", "digital_self_test!"},
    /* USB 桥高级调试入口，仍然遵守流状态和 TX 门控保护。 */
    {"usb_config?", sdr_cmd_usb_config, 0, 0, "USB bridge control bits, subject to stream guards",
     "usb_config?"},
    {"usb_config=", sdr_cmd_usb_config, 1, 255, "USB bridge control bits, subject to stream guards",
     "usb_config?"},
    {"usb_rx=", sdr_cmd_usb_rx, 3, 1, "USB RX control: enable,dual,channel1_select",
     "usb_rx=1 1 0"},
    {"usb_tx=", sdr_cmd_usb_tx, 4, 1, "USB TX control: enable,dual,channel1_select,run",
     "usb_tx=1 1 0 0"},
    {"usb_status?", sdr_cmd_usb_status, 0, 0, "USB FIFO status and error history", "usb_status?"},
    {"usb_irq?", sdr_cmd_usb_irq, 0, 0, "USB bridge interrupt and recovery diagnostics",
     "usb_irq?"},
    {"usb_clear!", sdr_cmd_usb_clear, 0, 0, "Queue USB fault-latch clear, preserve history",
     "usb_clear!"},
    {"usb_stop!", sdr_cmd_usb_stop, 0, 0, "Stop USB transport and USB-sourced TX", "usb_stop!"},
};

void sdr_commands_help(void)
{
    unsigned i;
    for (i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        const SdrCommand *command = &commands[i];
        console_print("%s parameters=%u range=0..%u -- %s; example: %s\n", command->name,
                      command->parameter_count, command->maximum, command->description,
                      command->example);
    }
    console_print(
        "RGB default=on brightness=15%%; fan default=off, 25kHz automatic Zynq temperature control when enabled.\n");
    console_print(
        "PPS default=off; pps_status? reports DATA_CLK Hz, not IQ timestamps. Mode=clock_ref_source+2*pps_input_en: 0 all high-Z, 1 10M PLL, 2 CH32 PA4 PPS loop, 3 10M PLL with PPS reference only.\n");
    console_print(
        "TX default=off; source 0=DDS 1=USB; masks 1=ch1 2=ch2 3=both; USB requires prefill before tx_enable=1.\n");
    console_print(
        "Custom FIR: rx_fir_begin=/tx_fir_begin=token,taps,factor,gain; fir_data=token,offset,c0..c7; fir_commit=token,enable,crc32; fir_abort=token\n");
    console_print(
        "FIR: rx_fir_read=/tx_fir_read=offset,count<=8; fir_enable=rx,tx. Clock: clock_sample_rate=Hz; clock_begin=/clock_rx=/clock_tx=/clock_commit=/clock_abort=\n");
    console_print(
        "clock_ref_source 0=internal40M 1=external10M_disciplined40M (pending until CH32 confirms PA4 high-Z); D5=hardware_lock_detect, no CPU readback. adf4002_self_test! flashes D5 with charge pump high-Z.\n");
    console_print(
        "rx_lo_source/tx_lo_source 0=internal 1=external; external frequency is nominal, actual input must be 2x RF LO; signal presence unverified.\n");
}

int sdr_commands_execute(const char *input)
{
    char alias[128];
    const char *line = input;
    unsigned i, p;
    double values[5];
    char count;
    int result;

    /* 分段上传命令必须走事务校验，不能按普通参数命令执行。 */
    result = sdr_fir_execute(line);
    if (result) {
        return result;
    }
    result = sdr_clock_execute(line);
    if (result) {
        return result;
    }
    /* CH32 发来的 vcxo_ack 行：静默处理，不产生任何输出。 */
    result = sdr_vcxo_execute(line);
    if (result) {
        return result;
    }
    if (!strncmp(line, "ch32_", 5)) {
        snprintf(alias, sizeof(alias), "usb_%s", line + 5);
        line = alias;
    }
    for (i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        const SdrCommand *command = &commands[i];
        count = 0;
        result = console_check_commands(line, command->name, values, &count, 5);
        if (result == UNKNOWN_CMD) {
            continue;
        }
        if (result == INVALID_CMD || (unsigned char)count != command->parameter_count) {
            return sdr_command_error("invalid_parameters", -1);
        }
        for (p = 0; p < (unsigned)count; ++p) {
            if (!isfinite(values[p]) || values[p] < 0 || values[p] > command->maximum ||
                (double)(unsigned)values[p] != values[p]) {
                return sdr_command_error("invalid_integer_or_range", -1);
            }
        }
        return command->callback(values, (unsigned)count) < 0 ? -1 : 1;
    }
    return 0;
}

int sdr_cmd_rx_channel(const double *values, unsigned count)
{
    if (count) {
        if (sdr_iq_format_guard((unsigned)values[0], sdr_state.tx_mask, sdr_state.tx_source))
            return -1;
        if (!values[0]) {
            if (sdr_set_usb_data(0, sdr_state.usb_tx)) {
                return -1;
            }
        } else if (sdr_state.usb_rx && sdr_state.rx_mask != (unsigned)values[0]) {
            return sdr_command_error("busy rx_upload_active", -1);
        }
        sdr_state.rx_mask = (unsigned)values[0];
    }
    console_print("rx_channel=%u\n", sdr_state.rx_mask);
    return 0;
}

int sdr_cmd_tx_channel(const double *values, unsigned count)
{
    if (count) {
        if (sdr_iq_format_guard(sdr_state.rx_mask, (unsigned)values[0], sdr_state.tx_source))
            return -1;
        if (!values[0]) {
            if (sdr_stop_tx()) {
                return -1;
            }
        } else if ((sdr_state.tx_enabled || sdr_state.usb_tx) &&
                   sdr_state.tx_mask != (unsigned)values[0]) {
            return sdr_command_error("busy tx_active_or_prefilling", -1);
        }
        sdr_state.tx_mask = (unsigned)values[0];
    }
    console_print("tx_channel=%u\n", sdr_state.tx_mask);
    return 0;
}

int sdr_cmd_tx_source(const double *values, unsigned count)
{
    if (count) {
        if (sdr_iq_format_guard(sdr_state.rx_mask, sdr_state.tx_mask, (unsigned)values[0]))
            return -1;
        if (sdr_state.tx_enabled || sdr_state.usb_tx) {
            return sdr_command_error("busy stop_tx_before_source_change", -1);
        }
        sdr_state.tx_source = (unsigned)values[0];
    }
    console_print("tx_source=%u\n", sdr_state.tx_source);
    return 0;
}

int sdr_cmd_tx_enable(const double *values, unsigned count)
{
    if (count && sdr_set_tx_enabled((unsigned)values[0])) {
        return -1;
    }
    console_print("tx_enable=%u actual_mask=%u\n", sdr_state.tx_enabled, sdr_actual_tx_mask());
    return 0;
}

int sdr_cmd_usb_data(const double *values, unsigned count)
{
    if (count && sdr_set_usb_data((unsigned)values[0], (unsigned)values[1])) {
        return -1;
    }
    console_print("usb_data_en=%u %u rx_stream=%lu tx_stream=%lu\n", sdr_state.usb_rx,
                  sdr_state.usb_tx, (unsigned long)sdr_state.rx_stream,
                  (unsigned long)sdr_state.tx_stream);
    return 0;
}

int sdr_cmd_iq_format(const double *values, unsigned count)
{
    return sdr_iq_format_command(count != 0, count ? (unsigned)values[0] : 0);
}

int sdr_cmd_clock_chain(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    if (!sdr_state.rf_ready) {
        return sdr_command_error("rf_not_initialized", sdr_state.rf_error);
    }
    sdr_print_clock_chain();
    return 0;
}

int sdr_cmd_rx_fir_profile(const double *values, unsigned count)
{
    (void)values;
    if (!sdr_state.rf_ready) {
        return sdr_command_error("rf_not_initialized", sdr_state.rf_error);
    }
    if (sdr_fir_profile(0, count != 0) < 0) {
        return -1;
    }
    return 0;
}

int sdr_cmd_tx_fir_profile(const double *values, unsigned count)
{
    (void)values;
    if (!sdr_state.rf_ready) {
        return sdr_command_error("rf_not_initialized", sdr_state.rf_error);
    }
    if (sdr_fir_profile(1, count != 0) < 0) {
        return -1;
    }
    return 0;
}

int sdr_cmd_rx_dc_correction(const double *values, unsigned count)
{
    if (sdr_correction_command(0, count != 0, count ? (unsigned)values[0] : 0) < 0) {
        return -1;
    }

    return 0;
}

int sdr_cmd_rx_iq_correction(const double *values, unsigned count)
{
    if (sdr_correction_command(1, count != 0, count ? (unsigned)values[0] : 0) < 0) {
        return -1;
    }

    return 0;
}

int sdr_cmd_system_info(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    sdr_print_system_info();
    return 0;
}

int sdr_cmd_system_status(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    sdr_print_system_status();
    return 0;
}

int sdr_cmd_rssi_enable(const double *values, unsigned count)
{
    return sdr_rssi_enable(count != 0, count ? (unsigned)values[0] : 0);
}

int sdr_cmd_rx_rssi(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    return sdr_print_rx_rssi();
}

int sdr_cmd_clock_reference(const double *values, unsigned count)
{
    if (sdr_reference_command(count != 0, count ? (unsigned)values[0] : 0) < 0) {
        return -1;
    }
    return 0;
}

int sdr_cmd_adf4002_status(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    if (sdr_reference_status() < 0) {
        return -1;
    }
    return 0;
}

int sdr_cmd_adf4002_self_test(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    if (sdr_reference_self_test() < 0) {
        return -1;
    }
    return 0;
}

int sdr_cmd_rx_lo_source(const double *values, unsigned count)
{
    if (sdr_external_lo_command(0, count != 0, count ? (unsigned)values[0] : 0) < 0) {
        return -1;
    }

    return 0;
}

int sdr_cmd_tx_lo_source(const double *values, unsigned count)
{
    if (sdr_external_lo_command(1, count != 0, count ? (unsigned)values[0] : 0) < 0) {
        return -1;
    }

    return 0;
}

int sdr_cmd_pps_input(const double *values, unsigned count)
{
    if (sdr_pps_command(count != 0, count ? (unsigned)values[0] : 0) < 0) {
        return -1;
    }
    return 0;
}

int sdr_cmd_pps_status(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    sdr_pps_status();
    return 0;
}

int sdr_cmd_vcxo_status(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    sdr_vcxo_status();
    return 0;
}

int sdr_cmd_rf_reinit(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    sdr_board_initializing(1);
    sdr_rf_reinitialize();
    sdr_board_initializing(0);
    return 0;
}

int sdr_cmd_digital_self_test(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    sdr_digital_self_test();
    return 0;
}

int sdr_cmd_usb_config(const double *values, unsigned count)
{
    if (count && sdr_set_usb_config((unsigned)values[0])) {
        return -1;
    }
    sdr_print_usb_config();
    return 0;
}

int sdr_cmd_usb_rx(const double *values, unsigned count)
{
    (void)count;
    {
        uint32_t bits;
        sdr_get_usb_config(&bits);
        bits = (bits & ~RX_BITS) | (unsigned)values[0] | ((unsigned)values[1] << 2) |
               ((unsigned)values[2] << 3);
        if (sdr_set_usb_config(bits)) {
            return -1;
        }
        sdr_print_usb_config();
    }
    return 0;
}

int sdr_cmd_usb_tx(const double *values, unsigned count)
{
    (void)count;
    {
        uint32_t bits;
        sdr_get_usb_config(&bits);
        bits = (bits & ~TX_BITS) | ((unsigned)values[0] << 1) | ((unsigned)values[1] << 4) |
               ((unsigned)values[2] << 5) | ((unsigned)values[3] << 6);
        if (sdr_set_usb_config(bits)) {
            return -1;
        }
        sdr_print_usb_config();
    }
    return 0;
}

int sdr_cmd_usb_status(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    sdr_print_usb_status();
    return 0;
}

int sdr_cmd_usb_irq(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    sdr_print_irq();
    return 0;
}

int sdr_cmd_usb_clear(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    sdr_request_usb_clear();
    console_print("usb_clear=queued history=retained fifo=unchanged\n");
    return 0;
}

int sdr_cmd_usb_stop(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    if (sdr_state.tx_source) {
        if (sdr_stop_all()) {
            return -1;
        }
    } else if (sdr_set_usb_data(0, 0)) {
        return -1;
    }
    sdr_print_usb_config();
    return 0;
}

int sdr_cmd_rgb_enable(const double *values, unsigned count)
{
    return sdr_board_rgb_command(count != 0, count ? (unsigned)values[0] : 0);
}

int sdr_cmd_fan_enable(const double *values, unsigned count)
{
    return sdr_board_fan_command(count != 0, count ? (unsigned)values[0] : 0);
}

int sdr_cmd_rgb_status(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    sdr_board_rgb_status();
    return 0;
}

int sdr_cmd_fan_status(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    sdr_board_fan_status();
    return 0;
}

int sdr_cmd_core_temp(const double *values, unsigned count)
{
    (void)values;
    (void)count;
    sdr_board_temperature_status();
    return 0;
}
