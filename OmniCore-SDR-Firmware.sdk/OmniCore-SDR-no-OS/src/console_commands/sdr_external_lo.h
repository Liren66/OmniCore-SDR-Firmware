#ifndef SDR_EXTERNAL_LO_H
#define SDR_EXTERNAL_LO_H

/* RX/TX LO 来源：0=内部，1=外部；每个方向的两个通道共用。
 * 外部输入频率为所设 RF LO 的两倍；寄存器回读不代表检测到信号。 */
int sdr_external_lo_command(unsigned tx, unsigned set, unsigned external);
int sdr_external_lo_verify(void);
void sdr_external_lo_save_frequencies(void);
int sdr_external_lo_restore(void);
#endif
