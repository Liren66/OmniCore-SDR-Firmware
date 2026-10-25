#ifndef SDR_CORRECTION_H
#define SDR_CORRECTION_H

/* RX1/RX2 共用的 DC/IQ 跟踪校正；关闭表示冻结当前校正值。 */
int sdr_correction_command(unsigned iq, unsigned set, unsigned enabled);
int sdr_correction_restore(void);
int sdr_correction_verify(void);
#endif
