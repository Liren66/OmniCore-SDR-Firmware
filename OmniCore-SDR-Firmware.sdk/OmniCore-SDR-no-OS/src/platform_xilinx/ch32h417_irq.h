#ifndef CH32H417_IRQ_H
#define CH32H417_IRQ_H

#include <stdint.h>
#include "CH32H417_IP.h"
#include "xscugic.h"
#include "xparameters_ps.h"

/* Board mapping, verified in final_routed.dcp: concat In15 -> IRQF2P[15].
 * This IP has no interrupt interface metadata in its current package.
 * The existing BSP peripheral interrupt macros do not describe this wiring. */
#define CH32H417_BOARD_IRQ_ID XPS_FPGA15_INT_ID
#define CH32H417_IRQ_ERRORS (CH32H417_IP_ERROR_MASK | CH32H417_IP_CFG_ERROR)
#define CH32H417_IRQ_CAPTURED 1U
#define CH32H417_IRQ_CLEARED  2U
#define CH32H417_IRQ_RETRY    4U
#define CH32H417_IRQ_TIMEOUT  8U

typedef enum {
    CH32_IRQ_IDLE, CH32_IRQ_WAIT_IDLE, CH32_IRQ_LOW,
    CH32_IRQ_WAIT_ACK, CH32_IRQ_VERIFY, CH32_IRQ_BACKOFF
} CH32H417_IrqState;

typedef struct {
    CH32H417_IP *Device;
    XScuGic *Gic;
    u32 Id;
    int Initialized;
    volatile u32 Pending, Masked, InterruptCount;
    volatile u32 ErrorsSeen, Snapshot0, Snapshot1;
    volatile u32 LastIrqStatus0, LastIrqStatus1;
    volatile u32 Observed[8], LatchedErrors, LastObservedStatus;
    CH32H417_IrqState State;
    uint64_t SinceUs;
    u32 ClearCount, RetryCount, TimeoutCount;
} CH32H417_Irq;

/* Attach to a GIC that the application has already initialized. */
int CH32H417_IrqAttach(CH32H417_Irq *irq, CH32H417_IP *device,
                     XScuGic *gic, u32 id);
/* Current console app only: initializes the shared GIC and CPU IRQ handler.
 * Call once at startup. Do not combine with another GIC CfgInitialize owner;
 * applications with other interrupt drivers should use IrqAttach instead. */
int CH32H417_IrqSetupStandalone(CH32H417_Irq *irq, CH32H417_IP *device);
void CH32H417_IrqHandler(void *reference);
/* Single main-loop caller; never call ClearErrors concurrently with this API.
 * Service performs bounded work, including when a stream clock has stopped. */
u32 CH32H417_IrqService(CH32H417_Irq *irq, uint64_t now_us);
void CH32H417_IrqRequestClear(CH32H417_Irq *irq);
uint64_t CH32H417_IrqNowUs(void);

#endif
