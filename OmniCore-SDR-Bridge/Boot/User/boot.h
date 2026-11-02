/*
 * OmniCore V5F bootloader: flash update engine behind the shared USB stack
 * (Common/ch32h417_usbss_*.c, ch32h417_usbhs_device.c).
 */
#ifndef BOOT_H
#define BOOT_H

#include "ch32h417.h"
#include "ocfw.h"

/* Steps recorded in OCFW_SHARED->trace, readable after a watchdog recovery. */
#define BOOT_TRACE_MAIN        1U
#define BOOT_TRACE_INIT        2U
#define BOOT_TRACE_USB_READY   3U
#define BOOT_TRACE_CONFIGURED  4U
#define BOOT_TRACE_FLAG_CLEAR  5U
#define BOOT_TRACE_FLAG_DONE   6U
#define BOOT_TRACE_TIMEOUT     7U
#define BOOT_TRACE(step)       (OCFW_SHARED->trace = (step))

void Boot_Init(void);
void Boot_Task(void);
void Boot_Timeout(void);

#endif
