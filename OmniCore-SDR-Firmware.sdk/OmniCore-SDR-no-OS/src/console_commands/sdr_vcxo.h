#ifndef SDR_VCXO_H
#define SDR_VCXO_H

/* 40 MHz VCTCXO 调谐节点由 ADF4002 电荷泵和 CH32 PA4(DAC1) 共用。
 * 模式 = clock_ref_source + 2 * pps_input_en：
 *   0 两者高阻；1 ADF4002 锁 10M，PA4 高阻；
 *   2 ADF4002 高阻，CH32 按 PPS 计数驱动 PA4；3 ADF4002 锁 10M，PA4 高阻，PPS 仅作参考。
 * 只有收到 CH32 对当前 req 的 pa4=0 应答后才打开电荷泵；关闭电荷泵立即生效。 */
enum { SDR_VCXO_OFF = 0, SDR_VCXO_REF = 1, SDR_VCXO_PPS = 2, SDR_VCXO_REF_PPS = 3 };

int sdr_vcxo_request(unsigned ref, unsigned pps);
unsigned sdr_vcxo_requested_ref(void);
unsigned sdr_vcxo_requested_pps(void);
unsigned sdr_vcxo_requested_mode(void);
unsigned sdr_vcxo_pending(void);
/* 处理 CH32 注入的 "vcxo_ack ..." 行：0=不是该行，1=已处理（不输出）。 */
int sdr_vcxo_execute(const char *line);
/* 仅在 main.c 主循环、两条命令之间调用：VCXO 行不能出现在 RSP 信封内。 */
void sdr_vcxo_service(void);
void sdr_vcxo_status(void);
#endif
