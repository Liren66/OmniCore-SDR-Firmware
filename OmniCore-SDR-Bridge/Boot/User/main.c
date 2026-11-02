/*
 * OmniCore SDR V5F bootloader.
 *
 * Stage-0 on V3F wakes V5F here (0x40000) when the application asked for an
 * update or when the application is missing. The bootloader runs the
 * application's USB stack (USB3, USB2 fallback), so it enumerates as the
 * same device (1A86:5537, same serial and endpoints) and answers the OCFW
 * vendor requests of Common/ocfw.h. It only writes the application region
 * and the boot flag sector.
 */
#include "debug.h"
#include "boot.h"
#include "ocfw.h"
#include "fw_version.h"
#include "usb_desc.h"
#include "ch32h417_usbss_device.h"

__attribute__((section(".bootid"), used))
const ocfw_bootid_t Boot_Id =
{
    OCFW_BOOTID_MAGIC,
    OMNICORE_BOOT_VERSION,
    OMNICORE_BUILD_ID,
};

int main(void)
{
    BOOT_TRACE(BOOT_TRACE_MAIN);
    ocfw_watchdog_feed();
    SystemAndCoreClockUpdate();
    Delay_Init();

    Chip = (DBGMCU_GetCHIPID() >> 4) & 0x0F;

    Boot_Init();
    BOOT_TRACE(BOOT_TRACE_INIT);

    /* Same start-up as the application: USB3 first, USB2 if that fails. */
    USB_SerialNumber_Init();
    USB_Timer_Init();
    USBSS_Device_Init(ENABLE);
    BOOT_TRACE(BOOT_TRACE_USB_READY);

    while(1)
    {
        ocfw_watchdog_feed();
        OCFW_SHARED->loops++;
        Boot_Task();
        Boot_Timeout();
    }
}
