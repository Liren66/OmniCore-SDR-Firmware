/*
 * ADF4002 driver for AD9361_MIX(3).pdf, pages 2 and 10.
 * CLK=MIO9, DATA=MIO10, CE=MIO12, LE=MIO13 (3.3 V PS bank 0).
 * MUXOUT -> R111 -> D5 -> GND; there is NO software readback wire.
 * J3 10 MHz -> REFIN; Y6 40 MHz VCTCXO -> LMK1C1104 -> RFINA.
 * AD9361 remains supplied with 40 MHz in both source modes.
 *
 * ADF4002 Rev. D, Figures 16-18 and CE-pin initialization, pp. 11-16:
 * https://www.analog.com/media/en/technical-documentation/data-sheets/adf4002.pdf
 * R=1, N=4; 2.9 ns antibacklash; five-cycle digital lock qualification;
 * positive PFD polarity; RSET=5.1 kohm, 625 uA charge pump, fastlock off.
 * The analog loop response still requires validation with a real reference.
 */
#include "adf4002.h"
#include "console.h"
#include <errno.h>
#include <string.h>
#include "xgpiops.h"
#include "xil_io.h"
#include "sleep.h"

extern XGpioPs gpio_instance;

#define ADF_CLK 9U
#define ADF_DATA 10U
#define ADF_CE 12U
#define ADF_LE 13U
#define MIO_BASE 0xF8000700U
#define MIO_MUX_TRI_MASK 0x000000FFU
#define MIO_IOTYPE_MASK 0x00000E00U
#define MIO_LVCMOS33 0x00000600U
#define FUNC_ADDR 2U
#define FUNC_PD_POSITIVE (1U << 7)
#define FUNC_CP_HIGH_Z (1U << 8)
#define FUNC_MUX(value) ((uint32_t)(value) << 4)
#define MUX_LOCK 1U
#define MUX_HIGH 3U
#define MUX_LOW 7U
#define R_WORD ((1U << 20) | (ADF4002_R_COUNTER << 2))
#define N_WORD ((ADF4002_N_COUNTER << 8) | 1U)

static uint8_t initialized;
static uint8_t external_reference;
static uint8_t gpio_verified;
static int32_t last_error;

static uint32_t function_word(uint8_t mux, uint8_t high_z)
{
    return FUNC_ADDR | FUNC_PD_POSITIVE | FUNC_MUX(mux) |
           (high_z ? FUNC_CP_HIGH_Z : 0U);
}

/* Read actual PS GPIO input pads, NOT the GPIO output register. This checks
 * the local MIO signal level only; it is not an acknowledgement from U22.
 */
static int32_t drive(unsigned pin, uint8_t level)
{
    XGpioPs_WritePin(&gpio_instance, pin, level);
    usleep(1); /* >= 10 ns DATA setup/hold, 25 ns CLK high/low, 20 ns LE. */
    /* A latch load takes ~1 ms; sdr_vcxo.c runs it from the idle loop while
     * host commands may arrive. Keep the console UART FIFO drained. */
    console_poll_rx();
    return XGpioPs_ReadPin(&gpio_instance, pin) == level ? 0 : -EIO;
}

static void fail_safe(int32_t error)
{
    /* Best effort even after a pad fault; never claim an unverified source. */
    XGpioPs_WritePin(&gpio_instance, ADF_CE, 0);
    XGpioPs_WritePin(&gpio_instance, ADF_LE, 0);
    XGpioPs_WritePin(&gpio_instance, ADF_CLK, 0);
    XGpioPs_WritePin(&gpio_instance, ADF_DATA, 0);
    external_reference = 0;
    gpio_verified = 0;
    last_error = error;
}

static int32_t write_latch(uint32_t word)
{
    int bit;
    int32_t ret;

    if ((ret = drive(ADF_LE, 0)) < 0 ||
        (ret = drive(ADF_CLK, 0)) < 0)
        return ret;
    for (bit = 23; bit >= 0; --bit) {
        if ((ret = drive(ADF_DATA, (word >> bit) & 1U)) < 0 ||
            (ret = drive(ADF_CLK, 1)) < 0 ||
            (ret = drive(ADF_CLK, 0)) < 0)
            return ret;
    }
    if ((ret = drive(ADF_LE, 1)) < 0 ||
        (ret = drive(ADF_LE, 0)) < 0)
        return ret;
    return drive(ADF_DATA, 0);
}

static int32_t apply_source(uint8_t enabled)
{
    int32_t ret;

    /* CE-pin initialization keeps CP high-Z while all latches are loaded. */
    if ((ret = drive(ADF_CE, 0)) < 0 ||
        (ret = write_latch(function_word(MUX_LOCK, !enabled))) < 0 ||
        (ret = write_latch(R_WORD)) < 0 ||
        (ret = write_latch(N_WORD)) < 0 ||
        (ret = drive(ADF_CE, enabled)) < 0) {
        fail_safe(ret);
        return ret;
    }
    external_reference = enabled;
    gpio_verified = 1;
    last_error = 0;
    return 0;
}

int32_t adf4002_init(void)
{
    static const unsigned pins[] = { ADF_CE, ADF_CLK, ADF_DATA, ADF_LE };
    unsigned i;
    uint32_t cfg;
    int32_t ret;

    if (initialized)
        return last_error;
    if (gpio_instance.IsReady != XIL_COMPONENT_IS_READY) {
        last_error = -ENODEV;
        return -ENODEV;
    }
    /* The existing hardware export already selects GPIO/LVCMOS33. Refuse
     * a mismatched export instead of modifying global SLCR or other MIOs.
     */
    for (i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        cfg = Xil_In32(MIO_BASE + 4U * pins[i]);
        if ((cfg & MIO_MUX_TRI_MASK) != 0 ||
            (cfg & MIO_IOTYPE_MASK) != MIO_LVCMOS33) {
            last_error = -ENODEV;
            return -ENODEV;
        }
    }
    for (i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        XGpioPs_WritePin(&gpio_instance, pins[i], 0);
        XGpioPs_SetDirectionPin(&gpio_instance, pins[i], 1);
        XGpioPs_SetOutputEnablePin(&gpio_instance, pins[i], 1);
        if ((ret = drive(pins[i], 0)) < 0) {
            fail_safe(ret);
            return ret;
        }
    }
    if ((ret = apply_source(0)) < 0)
        return ret;
    initialized = 1;
    return 0;
}

int32_t adf4002_set_external_reference(uint8_t enabled)
{
    if (enabled > 1)
        return -EINVAL;
    if (!initialized)
        return -ENODEV;
    return apply_source(enabled);
}

int32_t adf4002_get_external_reference(uint8_t *enabled)
{
    if (!enabled)
        return -EINVAL;
    if (!initialized)
        return -ENODEV;
    if (last_error)
        return last_error;
    *enabled = external_reference;
    return 0;
}

int32_t adf4002_get_status(struct adf4002_status *status)
{
    if (!status)
        return -EINVAL;
    memset(status, 0, sizeof(*status));
    status->initialized = initialized;
    status->external_reference = external_reference;
    status->gpio_verified = gpio_verified;
    status->locked = -1;
    status->led_on = -1;
    status->led_mode = external_reference ? 1 : 0;
    status->source_known = initialized && !last_error;
    status->last_error = last_error;
    status->reference_hz = ADF4002_REFERENCE_HZ;
    status->oscillator_hz = ADF4002_OSCILLATOR_HZ;
    status->r_counter = ADF4002_R_COUNTER;
    status->n_counter = ADF4002_N_COUNTER;
    return 0;
}

int32_t adf4002_self_test(void)
{
    uint8_t saved_source;
    unsigned cycle;
    int32_t ret;

    if (!initialized)
        return -ENODEV;
    if (last_error)
        return last_error;
    saved_source = external_reference;
    /* Never let a missing 10 MHz reference pull the VCXO control rail.
     * Both counter reset/CE and the explicit CP high-Z bit protect the loop.
     */
    if ((ret = drive(ADF_CE, 0)) < 0 ||
        (ret = write_latch(function_word(MUX_LOW, 1))) < 0 ||
        (ret = write_latch(R_WORD)) < 0 ||
        (ret = write_latch(N_WORD)) < 0 ||
        (ret = drive(ADF_CE, 1)) < 0)
        goto fail;
    for (cycle = 0; cycle < 3; ++cycle) {
        if ((ret = write_latch(function_word(MUX_HIGH, 1))) < 0)
            goto fail;
        usleep(500000);
        if ((ret = write_latch(function_word(MUX_LOW, 1))) < 0)
            goto fail;
        usleep(500000);
    }
    return apply_source(saved_source);
fail:
    fail_safe(ret);
    return ret;
}
