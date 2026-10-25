#ifndef SDR_RUNTIME_H
#define SDR_RUNTIME_H

#include "board_config.h"
#include "sdr_control.h"

/* 模块内部接口。仅在主循环访问；中断处理程序不得修改这些字段。
 * rf_ready/digital_ready 仅在校验成功后置位，失败时关闭 TX 门控。
 */
typedef struct {
    unsigned rx_mask, tx_mask; /* 0=关闭，1=通道1，2=通道2，3=双通道 */
    unsigned tx_source;        /* 0=DDS，1=USB */
    unsigned tx_enabled, usb_rx, usb_tx;
    unsigned rf_ready, digital_ready, bridge_ready;
    int rf_error;
    uint32_t attenuation[2];
    uint32_t rx_stream, tx_stream;
} SdrRuntimeState;

extern SdrRuntimeState sdr_state;

int sdr_command_error(const char *what, int code);
int sdr_set_tx_gate(unsigned enabled);
unsigned sdr_actual_tx_mask(void);
int sdr_stop_tx(void);
int sdr_stop_all(void);
int sdr_set_usb_data(unsigned rx_enabled, unsigned tx_enabled);
int sdr_set_tx_enabled(unsigned enabled);
int sdr_set_usb_config(unsigned bits);
void sdr_get_usb_config(uint32_t *bits);
void sdr_request_usb_clear(void);
void sdr_print_usb_config(void);
void sdr_print_irq(void);
void sdr_print_usb_status(void);
void sdr_print_system_info(void);
void sdr_print_system_status(void);
int sdr_rssi_enable(unsigned set, unsigned enabled);
int sdr_print_rx_rssi(void);
void sdr_print_clock_chain(void);
void sdr_rf_reinitialize(void);
void sdr_digital_self_test(void);

#endif
