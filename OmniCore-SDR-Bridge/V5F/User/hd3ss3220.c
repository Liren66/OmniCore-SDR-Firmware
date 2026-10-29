/* See hd3ss3220.h for wiring, integration and ownership requirements. */
#include "hd3ss3220.h"
#include "ch32h417_gpio.h"
#include "ch32h417_rcc.h"
#include "debug.h"
#include <stddef.h>
#include <string.h>
#if HD3SS3220_ENABLE_PRINTF
#include <stdio.h>
#endif

#if (HD3SS3220_I2C_ADDRESS != 0x47u) && (HD3SS3220_I2C_ADDRESS != 0x67u)
#error "HD3SS3220 address must be 7-bit 0x47 or 0x67"
#endif
#if HD3SS3220_EDGE_DELAY_US < 5u
#error "Use at least 5 us edge delay for this conservative software I2C driver"
#endif

static uint8_t gpio_ready;

static void edge_delay(void) { HD3SS3220_DELAY_US(HD3SS3220_EDGE_DELAY_US); }
static void scl_low(void) { GPIO_ResetBits(HD3SS3220_SCL_PORT, HD3SS3220_SCL_PIN); }
static void scl_release(void) { GPIO_SetBits(HD3SS3220_SCL_PORT, HD3SS3220_SCL_PIN); }
static void sda_low(void) { GPIO_ResetBits(HD3SS3220_SDA_PORT, HD3SS3220_SDA_PIN); }
static void sda_release(void) { GPIO_SetBits(HD3SS3220_SDA_PORT, HD3SS3220_SDA_PIN); }
static uint8_t sda_read(void)
{
    return GPIO_ReadInputDataBit(HD3SS3220_SDA_PORT, HD3SS3220_SDA_PIN) != Bit_RESET;
}

/* SetBits on an open-drain pin RELEASES the wire; it never drives high. */
static HD3SS3220_Result scl_high(void)
{
    uint32_t left = HD3SS3220_SCL_TIMEOUT_US;
    scl_release();
    while (GPIO_ReadInputDataBit(HD3SS3220_SCL_PORT, HD3SS3220_SCL_PIN) == Bit_RESET) {
        if (left == 0u) return HD3SS3220_ERR_SCL_TIMEOUT;
        --left;
        HD3SS3220_DELAY_US(1u);
    }
    return HD3SS3220_OK;
}

static void release_bus(void)
{
    sda_release();
    scl_release();
}

static HD3SS3220_Result i2c_start(void)
{
    HD3SS3220_Result ret;
    sda_release();
    ret = scl_high();
    if (ret != HD3SS3220_OK) return ret;
    edge_delay();
    if (!sda_read()) return HD3SS3220_ERR_BUS_BUSY;
    sda_low();
    edge_delay();
    scl_low();
    edge_delay();
    return HD3SS3220_OK;
}

static HD3SS3220_Result i2c_stop(void)
{
    HD3SS3220_Result ret;
    scl_low();
    sda_low();
    edge_delay();
    ret = scl_high();
    if (ret != HD3SS3220_OK) {
        release_bus();
        return ret;
    }
    edge_delay();
    sda_release();
    edge_delay(); /* also provides bus-free time before the next START */
    return sda_read() ? HD3SS3220_OK : HD3SS3220_ERR_BUS_BUSY;
}

static HD3SS3220_Result finish_transaction(HD3SS3220_Result ret)
{
    HD3SS3220_Result stopped = i2c_stop();
    return (ret == HD3SS3220_OK) ? stopped : ret;
}

static HD3SS3220_Result write_byte(uint8_t value)
{
    uint8_t bit, nack;
    HD3SS3220_Result ret;
    for (bit = 0u; bit < 8u; ++bit) {
        if ((value & 0x80u) != 0u) sda_release(); else sda_low();
        edge_delay();
        ret = scl_high();
        if (ret != HD3SS3220_OK) return ret;
        edge_delay();
        scl_low();
        edge_delay();
        value = (uint8_t)(value << 1);
    }
    sda_release(); /* let the target drive the ninth ACK bit */
    edge_delay();
    ret = scl_high();
    if (ret != HD3SS3220_OK) return ret;
    edge_delay();
    nack = sda_read();
    scl_low();
    edge_delay();
    return nack ? HD3SS3220_ERR_NACK : HD3SS3220_OK;
}

static HD3SS3220_Result read_byte(uint8_t *value, uint8_t ack)
{
    uint8_t bit, data = 0u;
    HD3SS3220_Result ret;
    sda_release();
    for (bit = 0u; bit < 8u; ++bit) {
        edge_delay();
        ret = scl_high();
        if (ret != HD3SS3220_OK) return ret;
        edge_delay();
        data = (uint8_t)((data << 1) | sda_read());
        scl_low();
        edge_delay();
    }
    if (ack) sda_low(); else sda_release(); /* last byte must be NACK */
    edge_delay();
    ret = scl_high();
    if (ret != HD3SS3220_OK) return ret;
    edge_delay();
    scl_low();
    edge_delay();
    sda_release();
    *value = data;
    return HD3SS3220_OK;
}

HD3SS3220_Result HD3SS3220_RecoverBus(void)
{
    HD3SS3220_Result ret;
    uint8_t pulse;
    if (!gpio_ready) return HD3SS3220_ERR_NOT_INITIALIZED;
    sda_release();
    ret = scl_high();
    if (ret != HD3SS3220_OK) { release_bus(); return ret; }
    edge_delay();
    if (sda_read()) return HD3SS3220_OK;
    for (pulse = 0u; pulse < 9u; ++pulse) {
        scl_low();
        edge_delay();
        ret = scl_high();
        if (ret != HD3SS3220_OK) { release_bus(); return ret; }
        edge_delay();
        if (sda_read()) break;
    }
    return i2c_stop();
}

HD3SS3220_Result HD3SS3220_Probe(void)
{
    HD3SS3220_Result ret;
    if (!gpio_ready) return HD3SS3220_ERR_NOT_INITIALIZED;
    ret = i2c_start();
    if (ret != HD3SS3220_OK) { release_bus(); return ret; }
    ret = write_byte((uint8_t)(HD3SS3220_I2C_ADDRESS << 1));
    return finish_transaction(ret);
}

HD3SS3220_Result HD3SS3220_Init(void)
{
    GPIO_InitTypeDef gpio;
    HD3SS3220_Result ret;
    HD3SS3220_DeviceInfo info;
    uint8_t attempt;
    gpio_ready = 0u;
    RCC_HB2PeriphClockCmd(HD3SS3220_SCL_CLOCK | HD3SS3220_SDA_CLOCK |
                         HD3SS3220_INT_CLOCK, ENABLE);
    /* Load released output state before enabling open-drain output. */
    release_bus();
    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    gpio.GPIO_Speed = GPIO_Speed_Medium;
    gpio.GPIO_Pin = HD3SS3220_SCL_PIN;
    GPIO_Init(HD3SS3220_SCL_PORT, &gpio);
    gpio.GPIO_Pin = HD3SS3220_SDA_PIN;
    GPIO_Init(HD3SS3220_SDA_PORT, &gpio);
    /* INT has a board pullup; floating input avoids changing its pull voltage. */
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    gpio.GPIO_Pin = HD3SS3220_INT_PIN;
    GPIO_Init(HD3SS3220_INT_PORT, &gpio);
    gpio_ready = 1u;
    ret = HD3SS3220_RecoverBus();
    if (ret != HD3SS3220_OK) return ret;
    /* Allow a short readiness retry window. This cannot fix power sequencing. */
    for (attempt = 0u; attempt < 20u; ++attempt) {
        ret = HD3SS3220_Probe();
        if (ret == HD3SS3220_OK) break;
        if (ret != HD3SS3220_ERR_NACK) return ret;
        HD3SS3220_DELAY_US(1000u);
    }
    if (ret != HD3SS3220_OK) return ret;
    ret = HD3SS3220_ReadDeviceInfo(&info);
    if (ret != HD3SS3220_OK) return ret;
    return info.id_matches ? HD3SS3220_OK : HD3SS3220_ERR_DEVICE_ID;
}

HD3SS3220_Result HD3SS3220_ReadRegs(uint8_t reg, uint8_t *data, uint16_t length)
{
    HD3SS3220_Result ret;
    uint16_t i;
    if (data == NULL || length == 0u || length > (256u - (uint16_t)reg))
        return HD3SS3220_ERR_PARAM;
    if (!gpio_ready) return HD3SS3220_ERR_NOT_INITIALIZED;
    ret = i2c_start();
    if (ret != HD3SS3220_OK) { release_bus(); return ret; }
    ret = write_byte((uint8_t)(HD3SS3220_I2C_ADDRESS << 1));
    if (ret == HD3SS3220_OK) ret = write_byte(reg);
    /* TI section 6.5: set sub-address, STOP, then START the read. */
    ret = finish_transaction(ret);
    if (ret != HD3SS3220_OK) return ret;
    ret = i2c_start();
    if (ret != HD3SS3220_OK) { release_bus(); return ret; }
    ret = write_byte((uint8_t)((HD3SS3220_I2C_ADDRESS << 1) | 1u));
    for (i = 0u; ret == HD3SS3220_OK && i < length; ++i) {
        ret = read_byte(&data[i], (uint8_t)(i + 1u < length));
    }
    /* On error data may be partially updated; discard the entire result. */
    return finish_transaction(ret);
}

HD3SS3220_Result HD3SS3220_ReadReg(uint8_t reg, uint8_t *value)
{
    return HD3SS3220_ReadRegs(reg, value, 1u);
}

HD3SS3220_Result HD3SS3220_WriteReg(uint8_t reg, uint8_t value)
{
    HD3SS3220_Result ret;
    if (reg < HD3SS3220_REG_STATUS || reg > HD3SS3220_REG_CONTROL)
        return HD3SS3220_ERR_PARAM;
    if (!gpio_ready) return HD3SS3220_ERR_NOT_INITIALIZED;
    ret = i2c_start();
    if (ret != HD3SS3220_OK) { release_bus(); return ret; }
    ret = write_byte((uint8_t)(HD3SS3220_I2C_ADDRESS << 1));
    if (ret == HD3SS3220_OK) ret = write_byte(reg);
    if (ret == HD3SS3220_OK) ret = write_byte(value);
    return finish_transaction(ret);
}

HD3SS3220_Result HD3SS3220_ReadDeviceInfo(HD3SS3220_DeviceInfo *info)
{
    static const uint8_t expected[8] = {0x32u, 0x32u, 0x33u, 0x42u,
                                      0x53u, 0x55u, 0x54u, 0x00u};
    HD3SS3220_DeviceInfo temp = {0};
    HD3SS3220_Result ret;
    uint8_t i;
    if (info == NULL) return HD3SS3220_ERR_PARAM;
    ret = HD3SS3220_ReadRegs(HD3SS3220_REG_DEVICE_ID, temp.id_raw, 8u);
    if (ret != HD3SS3220_OK) return ret;
    ret = HD3SS3220_ReadReg(HD3SS3220_REG_REVISION, &temp.revision);
    if (ret != HD3SS3220_OK) return ret;
    temp.id_matches = (uint8_t)(memcmp(temp.id_raw, expected, sizeof expected) == 0);
    for (i = 0u; i < 7u; ++i) temp.id_ascii[i] = (char)temp.id_raw[6u - i];
    temp.id_ascii[7] = '\0';
    *info = temp;
    return HD3SS3220_OK;
}

int HD3SS3220_IsInterruptActive(void)
{
    if (!gpio_ready) return HD3SS3220_ERR_NOT_INITIALIZED;
    return GPIO_ReadInputDataBit(HD3SS3220_INT_PORT, HD3SS3220_INT_PIN) == Bit_RESET;
}

HD3SS3220_Result HD3SS3220_ReadStatus(HD3SS3220_Status *status)
{
    static const uint16_t debounce[4] = {168u, 118u, 134u, 152u};
    HD3SS3220_Status temp = {0};
    uint8_t raw[3];
    HD3SS3220_Result ret;
    if (status == NULL) return HD3SS3220_ERR_PARAM;
    ret = HD3SS3220_ReadRegs(HD3SS3220_REG_STATUS, raw, 3u);
    if (ret != HD3SS3220_OK) return ret;
    temp.raw_08 = raw[0]; temp.raw_09 = raw[1]; temp.raw_0a = raw[2];
    temp.current_advertised = (raw[0] >> 6) & 3u;
    temp.current_detected = (raw[0] >> 4) & 3u;
    temp.accessory = (raw[0] >> 1) & 7u;
    temp.active_cable = raw[0] & 1u;
    temp.attached = (HD3SS3220_AttachedState)((raw[1] >> 6) & 3u);
    temp.cable_dir_bit = (raw[1] >> 5) & 1u;
    temp.active_cc = HD3SS3220_CC_NOT_VALID;
    if (temp.attached == HD3SS3220_ATTACHED_DFP || temp.attached == HD3SS3220_ATTACHED_UFP)
        temp.active_cc = temp.cable_dir_bit ? HD3SS3220_CC1 : HD3SS3220_CC2;
    temp.interrupt_pending = (raw[1] >> 4) & 1u;
    temp.interrupt_pin_low = (uint8_t)(HD3SS3220_IsInterruptActive() == 1);
    temp.vconn_fault = (raw[1] >> 3) & 1u;
    temp.drp_dfp_percent = (uint8_t)(30u + 10u * ((raw[1] >> 1) & 3u));
    temp.ufp_accessory_disabled = raw[1] & 1u;
    temp.debounce_ms = debounce[(raw[2] >> 6) & 3u];
    temp.mode_select = (raw[2] >> 4) & 3u;
    temp.soft_reset_active = (raw[2] >> 3) & 1u;
    temp.source_preference = (raw[2] >> 1) & 3u;
    temp.term_disabled = raw[2] & 1u;
    *status = temp; /* caller's struct is untouched on a bus error */
    return HD3SS3220_OK;
}

HD3SS3220_Result HD3SS3220_ClearInterrupt(void)
{
    uint8_t value;
    HD3SS3220_Result ret = HD3SS3220_ReadReg(HD3SS3220_REG_STATUS_CTRL, &value);
    if (ret != HD3SS3220_OK) return ret;
    /* SLAAE24 section 8: write ONE to bit 4. Do not copy back status/fault
     * bits; preserve writable configuration bits [2:0] only. */
    return HD3SS3220_WriteReg(HD3SS3220_REG_STATUS_CTRL,
                             (uint8_t)((value & 0x07u) | 0x10u));
}

const char *HD3SS3220_ErrorString(HD3SS3220_Result result)
{
    switch (result) {
    case HD3SS3220_OK: return "OK";
    case HD3SS3220_ERR_PARAM: return "Invalid argument/register/range";
    case HD3SS3220_ERR_NOT_INITIALIZED: return "Call HD3SS3220_Init first";
    case HD3SS3220_ERR_NACK: return "I2C NACK: check VDD5, ADDR=GND, wiring";
    case HD3SS3220_ERR_SCL_TIMEOUT: return "SCL held low / clock stretch timeout";
    case HD3SS3220_ERR_BUS_BUSY: return "SDA held low / bus busy";
    case HD3SS3220_ERR_DEVICE_ID: return "Unexpected ID: inspect raw ID bytes";
    default: return "Unknown error";
    }
}

#if HD3SS3220_ENABLE_PRINTF
void HD3SS3220_PrintStatus(const HD3SS3220_Status *s)
{
    static const char *const attach[4] = {"Unattached", "DFP/Source (local host)",
        "UFP/Sink (local device)", "Accessory"};
    static const char *const detected[4] = {"USB default (not a measured current)",
        "1.5 A", "Charge-through accessory: 500 mA", "3 A"};
    static const char *const advertised[4] = {"USB default", "1.5 A", "3 A", "Reserved"};
    static const char *const accessory[8] = {"None", "Reserved", "Reserved", "Reserved",
        "Audio", "Charge-through audio", "Debug, local DFP", "Debug, local UFP"};
    static const char *const mode[4] = {"PORT strap (this board: UFP)", "UFP", "DFP", "DRP"};
    static const char *const pref[4] = {"Standard DRP", "Try.SNK", "Reserved", "Try.SRC"};
    if (s == NULL) return;
    printf("HD3SS3220: 08=%02X 09=%02X 0A=%02X\r\n",
           (unsigned)s->raw_08, (unsigned)s->raw_09, (unsigned)s->raw_0a);
    printf("  Attach=%s; active_CC=%s; raw_direction_bit=%u\r\n",
           attach[(unsigned)s->attached & 3u],
           s->active_cc == HD3SS3220_CC1 ? "CC1" :
           s->active_cc == HD3SS3220_CC2 ? "CC2" : "N/A",
           (unsigned)s->cable_dir_bit);
    if (s->attached == HD3SS3220_ATTACHED_UFP)
        printf("  Source current capability=%s\r\n", detected[s->current_detected & 3u]);
    else if (s->attached == HD3SS3220_ATTACHED_DFP)
        printf("  Local current advertisement=%s\r\n", advertised[s->current_advertised & 3u]);
    else
        printf("  Current fields (raw): detected=%u advertised=%u; check attach/accessory\r\n",
               (unsigned)s->current_detected, (unsigned)s->current_advertised);
    printf("  Accessory=%s; active_cable=%u; VCONN_fault=%u\r\n",
           accessory[s->accessory & 7u], (unsigned)s->active_cable, (unsigned)s->vconn_fault);
    printf("  IRQ_latched=%u; INT_pin_low=%u\r\n",
           (unsigned)s->interrupt_pending, (unsigned)s->interrupt_pin_low);
    printf("  Mode=%s; preference=%s; debounce=%u ms; DRP_DFP=%u%%\r\n",
           mode[s->mode_select & 3u], pref[s->source_preference & 3u],
           (unsigned)s->debounce_ms, (unsigned)s->drp_dfp_percent);
    printf("  Term_disabled=%u; UFP_accessory_disabled=%u; reset_active=%u\r\n",
           (unsigned)s->term_disabled, (unsigned)s->ufp_accessory_disabled,
           (unsigned)s->soft_reset_active);
}

HD3SS3220_Result HD3SS3220_PrintInfo(void)
{
    HD3SS3220_DeviceInfo info;
    HD3SS3220_Status status;
    HD3SS3220_Result ret;
    uint8_t i;
    ret = HD3SS3220_ReadDeviceInfo(&info);
    if (ret != HD3SS3220_OK) return ret;
    printf("HD3SS3220 @ 0x%02X: ID=%s revision=0x%02X match=%u\r\n",
           (unsigned)HD3SS3220_I2C_ADDRESS, info.id_ascii,
           (unsigned)info.revision, (unsigned)info.id_matches);
    printf("  ID[00..07]:");
    for (i = 0u; i < 8u; ++i) printf(" %02X", (unsigned)info.id_raw[i]);
    printf("\r\n");
    ret = HD3SS3220_ReadStatus(&status);
    if (ret == HD3SS3220_OK) HD3SS3220_PrintStatus(&status);
    return ret;
}
#endif
