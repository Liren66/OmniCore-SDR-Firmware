#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H
#include "ad9361_api.h"

/* 本板唯一 AD9361：启动参数和默认 FIR 系数在 board_config.c。 */
extern struct ad9361_rf_phy *ad9361_phy;
extern AD9361_InitParam default_init_param;
extern AD9361_RXFIRConfig rx_fir_config;
extern AD9361_TXFIRConfig tx_fir_config;
void board_peripherals_init(void);
#endif
