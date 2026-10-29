/* V5F printf queue -> USBSS/USBHS EP3 IN (0x83), raw text, no header.
 * EP0 retains its original control/enumeration handlers.
 * Submit one short packet (1..1023 bytes) per hardware chain.
 * Tail advances only on IN completion; reset/CLEAR_HALT retries data.
 * Reconnection can repeat bytes; this is best-effort diagnostic output.
 */
#include "ch32h417.h"
#include "ch32h417_usb.h"
#include "ch32h417_usbss_device.h"
#include "ch32h417_usbhs_device.h"
#include "usb_ep3_debug.h"
#if defined(Core_V5F)
#if (USB_DEBUG_RING_SIZE & (USB_DEBUG_RING_SIZE - 1U)) != 0
#error USB_DEBUG_RING_SIZE must be a power of two
#endif
#if USB_DEBUG_TX_CHUNK == 0 || USB_DEBUG_TX_CHUNK >= USB_DEBUG_USB_MPS
#error Use a nonzero short packet
#endif
static uint8_t log_ring[USB_DEBUG_RING_SIZE];
static __attribute__((aligned(32), section(".da1")))
    uint8_t ep3_tx_buf[USB_DEBUG_USB_MPS];
static volatile uint32_t log_head, log_tail;
volatile uint32_t USB_Debug_Dropped, USB_Debug_Sent;
static volatile uint16_t in_length;
static volatile uint8_t initialized, configured, in_busy;
static uint8_t usb_hs;
static uint16_t usb_mps = USB_DEBUG_USB_MPS;
static uint32_t dbg_lock(void)
{
    uint32_t key;
    __asm volatile("csrrc %0, 0x800, %1" : "=r"(key) : "r"(0x88U) : "memory");
    return key & 0x88U;
}
static void dbg_unlock(uint32_t key)
{
    __asm volatile("csrs 0x800, %0" : : "r"(key) : "memory");
}
static void dbg_fence(void)
{
    __asm volatile("fence iorw, iorw" : : : "memory");
}
void USB_Debug_Init(void)
{
    uint32_t key = dbg_lock();
    if(!initialized)
    {
        log_head = log_tail = USB_Debug_Dropped = USB_Debug_Sent = 0;
        configured = in_busy = 0;
        in_length = 0;
        initialized = 1;
    }
    dbg_unlock(key);
    }
uint32_t USB_Debug_Write(const uint8_t *data, uint32_t length)
{
    uint32_t offset = 0, key, free_bytes, chunk, i;
    USB_Debug_Init();
    while(offset < length)
    {
        key = dbg_lock();
        free_bytes = USB_DEBUG_RING_SIZE - (log_head - log_tail);
        if(free_bytes == 0)
        {
            USB_Debug_Dropped += length - offset;
            dbg_unlock(key);
            break; /* Drop new bytes; never overwrite queued/in-flight data. */
        }
        chunk = length - offset;
        if(chunk > 64U) chunk = 64U;
        if(chunk > free_bytes) chunk = free_bytes;
        for(i = 0; i < chunk; ++i)
            log_ring[(log_head + i) & (USB_DEBUG_RING_SIZE - 1U)] = data[offset + i];
        log_head += chunk;
        offset += chunk;
        dbg_unlock(key);
    }
    /* Intentionally report consumed to stdio even if diagnostic bytes drop. */
    return length;
}
/* Software reset; caller subsequently clears/disables hardware chains. */
static void debug_reset(void)
{
    uint32_t key = dbg_lock();
    configured = in_busy = 0;
    in_length = 0;
    /* Preserve the queue, including startup logs. */
    dbg_unlock(key);
}
void USB_Debug_USBReset(void)
{
    if(!usb_hs) debug_reset();
}
void USB_Debug_HSReset(void)
{
    if(usb_hs) debug_reset();
}
static void debug_configure(uint8_t enabled, uint8_t high_speed_controller)
{
    uint32_t key;
    USB_Debug_Init();
    key = dbg_lock();
    configured = in_busy = 0;
    in_length = 0;
    usb_hs = high_speed_controller;
    usb_mps = usb_hs ? USBHS_DevMaxPackLen : USB_DEBUG_USB_MPS;
    if(usb_hs)
    {
        USBHSD->UEP3_MAX_LEN = usb_mps;
        USBHSD->UEP3_TX_LEN = 0;
        USBHSD->UEP3_TX_CTRL = USBHS_UEP_T_RES_NAK;
    }
    else
    {
        USBSSD->EP3_TX.UEP_TX_CFG = USBSS_EP_TX_CHAIN_AUTO |
            USBSS_EP_TX_ERDY_AUTO | USBSS_EP_TX_SEQ_AUTO;
        USBSSD->EP3_TX.UEP_TX_CR = USBSS_EP_TX_CLR | USBSS_EP_TX_CHAIN_CLR | 1U;
    }
    configured = enabled ? 1U : 0U;
    dbg_unlock(key);
}
void USB_Debug_SetConfigured(uint8_t enabled)
{
    debug_configure(enabled, 0U);
}
void USB_Debug_HSSetConfigured(uint8_t enabled)
{
    debug_configure(enabled, 1U);
}
void USB_Debug_ClearHalt(void)
{
    uint32_t key = dbg_lock();
    if(usb_hs)
    {
        USBHSD->UEP3_TX_CTRL = USBHS_UEP_T_RES_NAK;
        USBHSD->UEP3_TX_LEN = 0;
    }
    else USBSSD->EP3_TX.UEP_TX_CR = USBSS_EP_TX_CLR | USBSS_EP_TX_CHAIN_CLR | 1U;
    in_busy = 0;
    in_length = 0;
    dbg_unlock(key);
}
void USB_Debug_InIRQ(void)
{
    uint32_t key = dbg_lock();
    if(usb_hs)
    {
        USBHSD->UEP3_TX_CTRL = (USBHSD->UEP3_TX_CTRL & USBHS_UEP_T_TOG_MASK) | USBHS_UEP_T_RES_NAK;
        USBHSD->UEP3_TX_LEN = 0;
    }
    else USBSSD->EP3_TX.UEP_TX_CHAIN_ST |= USBSS_EP_TX_CHAIN_IF;
    if(in_busy)
    {
        log_tail += in_length;
        USB_Debug_Sent += in_length;
        in_busy = 0;
        in_length = 0;
    }
    dbg_unlock(key);
}
void USB_Debug_Task(void)
{
    uint32_t key, available, count, i;
    if(!initialized) return;
    key = dbg_lock();
    if(!configured || !(usb_hs ? USBHS_DevEnumStatus : USBSS_DevEnumStatus) || in_busy)
    {
        dbg_unlock(key);
        return;
    }
    available = log_head - log_tail;
    if(!available || (usb_hs
        ? ((USBHSD->UEP3_TX_CTRL & USBHS_UEP_T_RES_MASK) == USBHS_UEP_T_RES_STALL)
        : ((USBSSD->EP3_TX.UEP_TX_CR & USBSS_EP_TX_HALT) != 0)))
    {
        dbg_unlock(key);
        return;
    }
    count = available >= usb_mps ? usb_mps - 1U : available;
    for(i = 0; i < count; ++i)
        ep3_tx_buf[i] = log_ring[(log_tail + i) & (USB_DEBUG_RING_SIZE - 1U)];
    if(usb_hs)
    {
        USBHSD->UEP3_TX_DMA = (uint32_t)ep3_tx_buf;
        USBHSD->UEP3_TX_LEN = (uint16_t)count;
    }
    else
    {
        USBSSD->EP3_TX.UEP_TX_DMA = (uint32_t)ep3_tx_buf;
        USBSSD->EP3_TX.UEP_TX_DMA_OFS = USB_DEBUG_USB_MPS;
        USBSSD->EP3_TX.UEP_TX_CHAIN_LEN = (uint16_t)count;
    }
    in_length = (uint16_t)count;
    in_busy = 1;
    dbg_fence();
    if(usb_hs)
        USBHSD->UEP3_TX_CTRL = (USBHSD->UEP3_TX_CTRL & USBHS_UEP_T_TOG_MASK) | USBHS_UEP_T_RES_ACK;
    else USBSSD->EP3_TX.UEP_TX_CHAIN_EXP_NUMP = 1U;
    dbg_unlock(key);
}
#endif /* Core_V5F */