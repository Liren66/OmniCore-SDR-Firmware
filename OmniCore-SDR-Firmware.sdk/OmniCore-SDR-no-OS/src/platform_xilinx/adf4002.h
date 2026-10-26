/* AD9361_MIX board reference PLL (U22). See schematic pages 2 and 10. */
#ifndef ADF4002_H_
#define ADF4002_H_

#include <stdint.h>

#define ADF4002_REFERENCE_HZ 10000000U
#define ADF4002_OSCILLATOR_HZ 40000000U
#define ADF4002_R_COUNTER 1U
#define ADF4002_N_COUNTER 4U

struct adf4002_status {
    uint8_t initialized;
    uint8_t external_reference;
    uint8_t gpio_verified;
    uint8_t chip_verified; /* Always 0: no ADF4002 return path on this PCB. */
    uint8_t lock_known;    /* Always 0: MUXOUT drives D5, not a GPIO. */
    int8_t locked;        /* -1 = unavailable, never inferred from CE. */
    uint8_t led_known;
    int8_t led_on;        /* -1 = unavailable, no LED feedback. */
    uint8_t led_mode;     /* 0 = disabled; 1 = hardware digital lock detect. */
    uint8_t source_known;
    int32_t last_error;
    uint32_t reference_hz;
    uint32_t oscillator_hz;
    uint16_t r_counter;
    uint16_t n_counter;
};

/* Call once after platform gpio_init(). Repeated calls preserve source. */
int32_t adf4002_init(void);
/* 0: free-running on-board 40 MHz, CE low; 1: discipline it to J3 10 MHz.
 * Enabling does not prove that an external reference is present or locked.
 * CH32 PA4 shares the CP node: call with 1 only via sdr_vcxo.c, after the
 * CH32 acknowledged PA4 high-Z for the current request.
 */
int32_t adf4002_set_external_reference(uint8_t enabled);
int32_t adf4002_get_external_reference(uint8_t *enabled);
int32_t adf4002_get_status(struct adf4002_status *status);
/* Three 500 ms on/off D5 pulses; charge pump remains high-Z throughout.
 * Restores source/normal lock LED mode. Returns only GPIO pad validation;
 * chip communication requires observing the LED or measuring MUXOUT.
 */
int32_t adf4002_self_test(void);

#endif
