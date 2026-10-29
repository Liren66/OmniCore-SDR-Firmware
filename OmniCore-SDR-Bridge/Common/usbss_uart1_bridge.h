#ifndef USBSS_UART1_BRIDGE_H
#define USBSS_UART1_BRIDGE_H
#include "ch32h417.h"
/* PA9 TX / PA10 RX, AF7, 8 data bits, no parity, 1 stop bit. */
/* Must equal CONSOLE_BAUD_RATE in the Zynq SDK. USART1 runs from HCLK = 100 MHz
 * with 16x oversampling; the Zynq UART16550 has an integer divisor on 100 MHz,
 * so exact shared rates are 6.25 MBd / N and 6.25 MBd is the ceiling. */
#define UART1_BRIDGE_BAUD          3125000UL
#define UART1_BRIDGE_RX_SIZE       8192U
#define UART1_BRIDGE_USB_MPS       1024U
/* Always send a short USB packet, including for a continuous UART stream. */
#define UART1_BRIDGE_IN_CHUNK      1023U
typedef struct
{
    uint32_t usb_out_bytes;
    uint32_t uart_tx_dma_bytes;
    uint32_t usb_in_bytes;
    uint32_t rx_dropped_bytes;
    uint32_t uart_error_events;
    uint32_t dma_rx_errors;
    uint32_t dma_tx_errors;
    uint32_t usb_out_errors;
    uint32_t uart_tx_aborts; /* UART TX DMA stopped before completion */
} UART1_BridgeStats_t;

/* Inspect using the debugger; never printf on USART1 during bridging. */
extern volatile UART1_BridgeStats_t UART1_BridgeStats;
void UART1_Bridge_Init(uint32_t baudrate);
void UART1_Bridge_Task(void);
void UART1_Bridge_USBReset(void);
void UART1_Bridge_HSReset(void);
void UART1_Bridge_HSSetConfigured(uint8_t enabled);
void UART1_Bridge_SetConfigured(uint8_t enabled);
void UART1_Bridge_ClearHalt(uint8_t endpoint_address);
void UART1_Bridge_OutIRQ(void);
void UART1_Bridge_InIRQ(void);
#endif


