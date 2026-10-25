#ifndef SDR_REFERENCE_H
#define SDR_REFERENCE_H

/* 本板 40 MHz 振荡器的 ADF4002 外部 10 MHz 参考控制。 */
int sdr_reference_command(unsigned set, unsigned external);
int sdr_reference_status(void);
int sdr_reference_self_test(void);
#endif
