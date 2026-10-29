#ifndef USB_EP3_DEBUG_H
#define USB_EP3_DEBUG_H
#include <stdint.h>
#define USB_DEBUG_ENDPOINT       0x83U
#define USB_DEBUG_USB_MPS        1024U
#define USB_DEBUG_TX_CHUNK       1023U
#define USB_DEBUG_RING_SIZE      4096U

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
#endif