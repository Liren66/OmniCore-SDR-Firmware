#ifndef SDR_PPS_H
#define SDR_PPS_H
#include <stdint.h>

/* PPS 计数器测量 DATA_CLK 频率，默认关闭，计数器本身不调节时钟。
 * 仅 PPS（无 10M）时由 CH32 根据计数驱动 PA4 驯服 40M，见 sdr_vcxo.h。
 * 无输入时返回 waiting/no_signal，频率有效性必须单独判断。 */
void sdr_pps_refresh(void);
void sdr_pps_status(void);
int sdr_pps_command(unsigned set, unsigned enabled);
/* 精确的 DATA_CLK 期望值（Hz，Q16）；时钟不可用时为 0。 */
uint64_t sdr_pps_expected_q16(void);
#endif
