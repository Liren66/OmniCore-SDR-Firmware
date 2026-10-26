#ifndef SDR_COMMANDS_H
#define SDR_COMMANDS_H

/* 扩展命令入口：command.c 完成行接收/协议封装后调用这里。
 * 回调输入已完成整数、数量和范围校验；count=0 查询，count>0 设置。
 * 回调返回 0 成功，-1 失败；失败时必须输出 ERR 供 OCH 判断。
 */
typedef int (*SdrCommandCallback)(const double *values, unsigned count);

typedef struct {
    const char *name;
    SdrCommandCallback callback;
    unsigned parameter_count;
    unsigned maximum;
    const char *description;
    const char *example;
} SdrCommand;

/* 0=不识别；1=已处理；-1=拒绝。协议字符串和旧版本兼容。 */
int sdr_commands_execute(const char *line);
void sdr_commands_help(void);

/* RX channel mask: 0=off, 1=RX1, 2=RX2, 3=both */
int sdr_cmd_rx_channel(const double *values, unsigned count);
/* TX channel mask: 0=off, 1=TX1, 2=TX2, 3=both */
int sdr_cmd_tx_channel(const double *values, unsigned count);
/* TX sample source: 0=DDS, 1=USB */
int sdr_cmd_tx_source(const double *values, unsigned count);
/* TX output gate; USB requires prefill */
int sdr_cmd_tx_enable(const double *values, unsigned count);
/* USB RX upload / TX download enables */
int sdr_cmd_usb_data(const double *values, unsigned count);
/* USB wire format: 12 by default, optional signed8 for 2TX2RX USB. */
int sdr_cmd_iq_format(const double *values, unsigned count);
/* Read RX and TX clock chains in Hz */
int sdr_cmd_clock_chain(const double *values, unsigned count);
/* Read RX FIR / restore default profile 0 */
int sdr_cmd_rx_fir_profile(const double *values, unsigned count);
/* Read TX FIR / restore default profile 0 */
int sdr_cmd_tx_fir_profile(const double *values, unsigned count);
/* RX RF+baseband DC tracking: 0=freeze, 1=track */
int sdr_cmd_rx_dc_correction(const double *values, unsigned count);
/* RX IQ tracking: 0=freeze, 1=track */
int sdr_cmd_rx_iq_correction(const double *values, unsigned count);
/* Board identity, protocol and capabilities */
int sdr_cmd_system_info(const double *values, unsigned count);
/* RF, USB, reference and PPS status */
int sdr_cmd_system_status(const double *values, unsigned count);
int sdr_cmd_rssi_enable(const double *values, unsigned count);
int sdr_cmd_rx_rssi(const double *values, unsigned count);
/* Reference: 0=internal 40M, 1=external 10M discipline */
int sdr_cmd_clock_reference(const double *values, unsigned count);
/* ADF4002 GPIO / reference status; no lock readback */
int sdr_cmd_adf4002_status(const double *values, unsigned count);
/* Blink D5 three times for visual communication test */
int sdr_cmd_adf4002_self_test(const double *values, unsigned count);
/* RX LO: 0=internal, 1=external 2x RF frequency */
int sdr_cmd_rx_lo_source(const double *values, unsigned count);
/* TX LO: 0=internal, 1=external 2x RF frequency */
int sdr_cmd_tx_lo_source(const double *values, unsigned count);
/* PPS input; without 10M the CH32 PA4 DAC disciplines 40M; default=0 */
int sdr_cmd_pps_input(const double *values, unsigned count);
/* PPS signal presence / valid DATA_CLK measurement */
int sdr_cmd_pps_status(const double *values, unsigned count);
/* 40M tune mode, ADF4002/PA4 ownership and CH32 PPS loop state */
int sdr_cmd_vcxo_status(const double *values, unsigned count);
/* Stop streams, reinitialize RF, restore verified settings */
int sdr_cmd_rf_reinit(const double *values, unsigned count);
/* Stop streams and check RX PRBS / TX digital loopback */
int sdr_cmd_digital_self_test(const double *values, unsigned count);
/* USB bridge control bits, subject to stream guards */
int sdr_cmd_usb_config(const double *values, unsigned count);
/* USB RX control: enable,dual,channel1_select */
int sdr_cmd_usb_rx(const double *values, unsigned count);
/* USB TX control: enable,dual,channel1_select,run */
int sdr_cmd_usb_tx(const double *values, unsigned count);
/* USB FIFO status and error history */
int sdr_cmd_usb_status(const double *values, unsigned count);
/* USB bridge interrupt and recovery diagnostics */
int sdr_cmd_usb_irq(const double *values, unsigned count);
/* Queue USB fault-latch clear, preserve history */
int sdr_cmd_usb_clear(const double *values, unsigned count);
/* Stop USB transport and USB-sourced TX */
int sdr_cmd_usb_stop(const double *values, unsigned count);

/* RGB、风扇温控及 Zynq 内部温度。 */
int sdr_cmd_rgb_enable(const double *values, unsigned count);
int sdr_cmd_rgb_status(const double *values, unsigned count);
int sdr_cmd_fan_enable(const double *values, unsigned count);
int sdr_cmd_fan_status(const double *values, unsigned count);
int sdr_cmd_core_temp(const double *values, unsigned count);

#endif
