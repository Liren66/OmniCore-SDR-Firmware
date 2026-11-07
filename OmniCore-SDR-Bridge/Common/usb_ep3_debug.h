#ifndef USB_EP3_DEBUG_H
#define USB_EP3_DEBUG_H
#include <stdint.h>
#define USB_DEBUG_ENDPOINT       0x83U
#define USB_DEBUG_USB_MPS        1024U
#define USB_DEBUG_TX_CHUNK       1023U
#define USB_DEBUG_RING_SIZE      4096U
/* Application 1.0.1+: EP3 OUT (0x03) takes text queries, one per line,
 * answered on EP3 IN in the same log stream:
 *   "typec?"  -> "HD3SS3220: 08=xx 09=xx 0A=xx" (live read, as at startup)
 *                or "HD3SS3220: ERR <reason>"
 *   other     -> "EP3: ERR unknown <query>"
 * Queries are executed by USB_Debug_Task in the main loop, never in an IRQ. */
#define USB_DEBUG_OUT_ENDPOINT   0x03U
#define USB_DEBUG_QUERY_MAX      64U

extern volatile uint32_t USB_Debug_Dropped;
extern volatile uint32_t USB_Debug_Sent;
void USB_Debug_Init(void);
uint32_t USB_Debug_Write(const uint8_t *data, uint32_t length);
void USB_Debug_Task(void);
void USB_Debug_SetConfigured(uint8_t enabled);
void USB_Debug_USBReset(void);
void USB_Debug_HSReset(void);
void USB_Debug_HSSetConfigured(uint8_t enabled);
void USB_Debug_ClearHalt(void);
void USB_Debug_InIRQ(void);
void USB_Debug_OutIRQ(void);
void USB_Debug_ClearHaltOut(void);
void USB_Debug_EnableTypeC(uint8_t ready);
#endif