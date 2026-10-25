#ifndef SDR_PPS_H
#define SDR_PPS_H

/* PPS 仅监控 DATA_CLK 频率，默认关闭，不调节时钟。
 * 无输入时返回 waiting/no_signal，频率有效性必须单独判断。 */
void sdr_pps_refresh(void);
void sdr_pps_status(void);
int sdr_pps_command(unsigned set, unsigned enabled);
#endif
