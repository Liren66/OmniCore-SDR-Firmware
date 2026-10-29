/*
 * HD3SS3220IRNHT software I2C driver for CH32H417, 2026-10-19.
 * Board: AD9361_MIX(3).pdf, sheet 11, U14 / U24 (CH32H417MEU6).
 * PA5 = USB3_SCL, PA11 = USB3_SDA, PA12 = USB3_INT (active low).
 * MCU package pins: PA5=23, PA11=56, PA12=57 (MEU6 / QFN88).
 * ADDR is grounded: 7-bit address 0x47 (wire bytes 0x8E / 0x8F).
 * PORT is pulled low: hardware default UFP/Sink. ENn_CC/ENn_MUX are grounded.
 *
 * Add this .c and .h to your application. Requires WCH SPL GPIO/RCC,
 * debug.c Delay_Us(), and printf retargeting for the optional print functions.
 * Call SystemAndCoreClockUpdate(), Delay_Init(), USART_Printf_Init() first.
 * Call Init after all startup pin configuration, with VDD5/VCC33 stable.
 * PA11/PA12 must not also be used by USBFS/OTG or another peripheral.
 * SCL/SDA use open-drain GPIO and the board's 4.7k pullups to 3.3V.
 * R79 pulls INT to 5V_USB3 through 200k; PA12 is configured as a floating
 * digital input, with no internal pullup. Keep its FT input conditions valid.
 * The Type-C MUX switches automatically in the chip; no manual DIR drive.
 *
 * Single-master, blocking, NOT reentrant. Use from ONE core/main task only;
 * do not call from USB/UHSIF/EXTI IRQ callbacks. An EXTI ISR may set a flag.
 * Delay_Us uses SysTick; do not re-enter it on the same core or use this
 * delay backend concurrently from both cores (the SDK shares ISR bits).
 * USB/UHSIF interrupts remain enabled during these short transactions.
 *
 * Init and all read APIs leave CC role, MUX and fault/interrupt flags alone.
 * A status read is a sequential snapshot, not an atomic latch across a
 * hot-plug event. Poll periodically even when using the interrupt pin.
 * This chip reports Type-C CC information, NOT USB enumeration/link speed,
 * measured VBUS/current, USB-PD contracts, VID/PID or throughput.
 *
 * Quick use in main, after existing board/USB initialization:
 *   HD3SS3220_Result ret = HD3SS3220_Init();
 *   if (ret == HD3SS3220_OK) ret = HD3SS3220_PrintInfo();
 *   else printf("HD3SS3220: %s\r\n", HD3SS3220_ErrorString(ret));
 *
 * Periodic service in main (e.g. every 500 ms, not in the UHSIF callback):
 *   HD3SS3220_Status status;
 *   if (HD3SS3220_ReadStatus(&status) == HD3SS3220_OK) {
 *       HD3SS3220_PrintStatus(&status);
 *       if (status.interrupt_pending) {
 *           ret = HD3SS3220_ClearInterrupt(); // W1C; check ret
 *       }
 *   }
 *
 * References: TI HD3SS3220 SLLSES1E (July 2025), sections 6.5/6.6;
 * TI SLAAE24 section 8 (0x09 bit 4 is WRITE ONE TO CLEAR).
 * https://www.ti.com/lit/gpn/HD3SS3220
 * https://www.ti.com/lit/an/slaae24/slaae24.pdf
 */
#ifndef HD3SS3220_H
#define HD3SS3220_H

#include "ch32h417.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Board configuration. Change these together if your wiring differs. */
#ifndef HD3SS3220_SCL_PORT
#define HD3SS3220_SCL_PORT          GPIOA
#define HD3SS3220_SCL_PIN           GPIO_Pin_5
#define HD3SS3220_SCL_CLOCK         RCC_HB2Periph_GPIOA
#endif
#ifndef HD3SS3220_SDA_PORT
#define HD3SS3220_SDA_PORT          GPIOA
#define HD3SS3220_SDA_PIN           GPIO_Pin_11
#define HD3SS3220_SDA_CLOCK         RCC_HB2Periph_GPIOA
#endif
#ifndef HD3SS3220_INT_PORT
#define HD3SS3220_INT_PORT          GPIOA
#define HD3SS3220_INT_PIN           GPIO_Pin_12
#define HD3SS3220_INT_CLOCK         RCC_HB2Periph_GPIOA
#endif
#ifndef HD3SS3220_I2C_ADDRESS
#define HD3SS3220_I2C_ADDRESS       0x47u /* 7-bit, NOT 0x8E */
#endif
#ifndef HD3SS3220_DELAY_US
#define HD3SS3220_DELAY_US(us)      Delay_Us(us)
#endif
#ifndef HD3SS3220_EDGE_DELAY_US
#define HD3SS3220_EDGE_DELAY_US     5u /* conservative <=100 kHz, plus GPIO overhead */
#endif
#ifndef HD3SS3220_SCL_TIMEOUT_US
#define HD3SS3220_SCL_TIMEOUT_US    1000u
#endif
#ifndef HD3SS3220_ENABLE_PRINTF
#define HD3SS3220_ENABLE_PRINTF     1 /* set to 0 globally to remove print helpers */
#endif

#define HD3SS3220_REG_DEVICE_ID     0x00u
#define HD3SS3220_REG_STATUS        0x08u
#define HD3SS3220_REG_STATUS_CTRL   0x09u
#define HD3SS3220_REG_CONTROL       0x0Au
#define HD3SS3220_REG_REVISION      0xA0u

typedef enum {
    HD3SS3220_OK = 0,
    HD3SS3220_ERR_PARAM = -1,
    HD3SS3220_ERR_NOT_INITIALIZED = -2,
    HD3SS3220_ERR_NACK = -3,
    HD3SS3220_ERR_SCL_TIMEOUT = -4,
    HD3SS3220_ERR_BUS_BUSY = -5,
    HD3SS3220_ERR_DEVICE_ID = -6
} HD3SS3220_Result;

typedef enum {
    HD3SS3220_UNATTACHED = 0,
    HD3SS3220_ATTACHED_DFP = 1, /* local port Source/Host */
    HD3SS3220_ATTACHED_UFP = 2, /* local port Sink/Device */
    HD3SS3220_ATTACHED_ACCESSORY = 3
} HD3SS3220_AttachedState;

typedef enum {
    HD3SS3220_CC_NOT_VALID = 0,
    HD3SS3220_CC1 = 1,
    HD3SS3220_CC2 = 2
} HD3SS3220_ActiveCC;

typedef struct {
    uint8_t id_raw[8]; /* address order 0x00..0x07, normally 32 32 33 42 53 55 54 00 */
    char id_ascii[8];  /* reverse 0x06..0x00 -> "TUSB322", plus NUL */
    uint8_t revision;  /* read 0xA0; not required to equal any one revision */
    uint8_t id_matches;
} HD3SS3220_DeviceInfo;

typedef struct {
    uint8_t raw_08;
    uint8_t raw_09;
    uint8_t raw_0a;
    HD3SS3220_AttachedState attached;
    HD3SS3220_ActiveCC active_cc; /* valid for ordinary DFP/UFP attach only */
    uint8_t cable_dir_bit;       /* raw 0x09[5]: 1=CC1, 0=CC2 per TI */
    uint8_t current_advertised;  /* 0 default, 1 1.5A, 2 3A, 3 reserved; DFP */
    uint8_t current_detected;    /* 0 default, 1 1.5A, 2 charge-through 500mA, 3 3A */
    uint8_t accessory;           /* 0 none, 4 audio, 5 charge-through, 6/7 debug */
    uint8_t active_cable;
    uint8_t interrupt_pending;   /* latched CSR flag, separate from physical pin */
    uint8_t interrupt_pin_low;
    uint8_t vconn_fault;
    uint8_t drp_dfp_percent;     /* 30/40/50/60 */
    uint8_t ufp_accessory_disabled;
    uint16_t debounce_ms;
    uint8_t mode_select;         /* 0 PORT strap default, 1 UFP, 2 DFP, 3 DRP */
    uint8_t source_preference;   /* 0 standard, 1 Try.SNK, 2 reserved, 3 Try.SRC */
    uint8_t term_disabled;
    uint8_t soft_reset_active;
} HD3SS3220_Status;

/* Init configures only the three GPIO pins, recovers an idle stuck bus if
 * needed, retries address ACK briefly, and validates the device ID.
 * An error leaves GPIO configured so raw reads/recovery can diagnose it.
 * It does NOT reset or reconfigure the HD3SS3220. */
HD3SS3220_Result HD3SS3220_Init(void);
HD3SS3220_Result HD3SS3220_Probe(void);
HD3SS3220_Result HD3SS3220_RecoverBus(void); /* at most 9 SCL pulses, then STOP */
HD3SS3220_Result HD3SS3220_ReadReg(uint8_t reg, uint8_t *value);
HD3SS3220_Result HD3SS3220_ReadRegs(uint8_t reg, uint8_t *data, uint16_t length);

/* Advanced raw write to 0x08/0x09/0x0A ONLY. Caller owns field semantics:
 * 0x09[4] is W1C; MODE_SELECT may change only while unattached; writing
 * reset/termination/role fields may interrupt USB. Not called by Init. */
HD3SS3220_Result HD3SS3220_WriteReg(uint8_t reg, uint8_t value);
HD3SS3220_Result HD3SS3220_ReadDeviceInfo(HD3SS3220_DeviceInfo *info);
HD3SS3220_Result HD3SS3220_ReadStatus(HD3SS3220_Status *status);
/* Explicitly acknowledges 0x09[4], preserves configuration bits [2:0].
 * Consume status first; status changes can coalesce around this operation.
 * Does not deliberately acknowledge the separate VCONN fault field. */
HD3SS3220_Result HD3SS3220_ClearInterrupt(void);
/* Returns 1 for active-low INT, 0 for high, -2 if GPIO not initialized. */
int HD3SS3220_IsInterruptActive(void);
const char *HD3SS3220_ErrorString(HD3SS3220_Result result);

#if HD3SS3220_ENABLE_PRINTF
HD3SS3220_Result HD3SS3220_PrintInfo(void); /* reads ID, revision and status */
void HD3SS3220_PrintStatus(const HD3SS3220_Status *status);
#endif

#ifdef __cplusplus
}
#endif
#endif
