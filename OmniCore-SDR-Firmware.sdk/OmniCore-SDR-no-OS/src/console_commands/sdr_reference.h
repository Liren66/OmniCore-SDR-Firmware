#ifndef SDR_REFERENCE_H
#define SDR_REFERENCE_H

/* 本板 40 MHz 振荡器的 ADF4002 外部 10 MHz 参考控制。
 * clock_ref_source=1 异步完成：CH32 确认 PA4 高阻后才打开电荷泵。 */
int sdr_reference_command(unsigned set, unsigned external);
int sdr_reference_status(void);
int sdr_reference_self_test(void);
/* 仅供 sdr_vcxo.c 调用：0=CE 低且电荷泵高阻，1=锁相环。失败时关闭 RF。 */
int sdr_reference_apply(unsigned enabled);
#endif
