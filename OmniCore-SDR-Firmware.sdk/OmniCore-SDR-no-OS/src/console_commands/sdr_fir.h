#ifndef SDR_FIR_H
#define SDR_FIR_H

#include "ad9361_api.h"

typedef struct {
    AD9361_RXFIRConfig rx;
    AD9361_TXFIRConfig tx;
    uint32_t rx_clocks[6], tx_clocks[6], rx_bandwidth, tx_bandwidth;
    unsigned rx_enabled, tx_enabled;
} SdrFirState;

/* FIR/时钟修改共用快照、校验和回滚。调用前必须停止 TX 与 USB。 */
int sdr_fir_boot(void);
int sdr_fir_capture(SdrFirState *state);
int sdr_fir_validate(const SdrFirState *state);
int sdr_fir_program(const SdrFirState *state);
unsigned sdr_fir_recovery(SdrFirState *state);
void sdr_fir_set_verified(unsigned verified);
void sdr_fir_save_recovery(const SdrFirState *state);
int sdr_fir_profile(unsigned tx, unsigned set_default);
/* 0=不是本模块命令；1=已执行；-1=已拒绝并输出 ERR。 */
int sdr_fir_execute(const char *line);

#endif
