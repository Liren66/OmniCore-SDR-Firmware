#ifndef SDR_CONTROL_H
#define SDR_CONTROL_H
#include <stdint.h>
/* All callers run in the console main loop; the IRQ never calls these. */
int sdr_rf_boot(void);
int sdr_control_init(void);
void sdr_control_service(void);
/* 原有 AD9361 命令的 RF/时钟保护检查。 */
int sdr_guard(const char *name);
int sdr_set_attenuation(unsigned channel, uint32_t value);
int sdr_get_attenuation(unsigned channel, uint32_t *value);
#endif
