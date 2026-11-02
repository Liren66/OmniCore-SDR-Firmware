/*
 * OmniCore V5F bootloader: the shared USB stack (Common/ch32h417_usbss_*.c,
 * ch32h417_usbhs_device.c) calls into the application's UHSIF stream, USART1
 * bridge and EP3 debug log. The bootloader has none of them; the bulk
 * endpoints stay idle and the 0xD0 diagnostics request STALLs.
 */
#include "ch32h417.h"
#include "hardware.h"

pack_t UHSIF_WR_Pack[DEF_UHSIF_TXBUF_CNT];
comm_t UHSIF_WR_COMM;
pack_t UHSIF_RD_Pack[DEF_UHSIF_RXBUF_CNT];
comm_t UHSIF_RD_COMM;
volatile uint32_t StreamDiag[64];

void UHSIF_Stream_Reset(void) {}
void UHSIF_Stream_ResetHS(void) {}
void UHSIF_USBOut_Complete(uint32_t length) { (void)length; }
uint8_t UHSIF_Trans_Cfg(uint8_t line_num, uint8_t buff_cur, uint32_t trans_size)
{
    (void)line_num; (void)buff_cur; (void)trans_size;
    return 0;
}

void StreamDiag_Event(uint32_t kind) { (void)kind; }
uint16_t StreamDiag_Read(uint8_t *out, uint16_t page, uint16_t reset)
{
    (void)out; (void)page; (void)reset;
    return 0;
}

void USB_Debug_SetConfigured(uint8_t enabled) { (void)enabled; }
void USB_Debug_USBReset(void) {}
void USB_Debug_HSReset(void) {}
void USB_Debug_HSSetConfigured(uint8_t enabled) { (void)enabled; }
void USB_Debug_ClearHalt(void) {}
void USB_Debug_InIRQ(void) {}

void UART1_Bridge_USBReset(void) {}
void UART1_Bridge_HSReset(void) {}
void UART1_Bridge_HSSetConfigured(uint8_t enabled) { (void)enabled; }
void UART1_Bridge_SetConfigured(uint8_t enabled) { (void)enabled; }
void UART1_Bridge_ClearHalt(uint8_t endpoint_address) { (void)endpoint_address; }
void UART1_Bridge_OutIRQ(void) {}
void UART1_Bridge_InIRQ(void) {}
