#include "ch32h417_irq.h"
#include "xparameters.h"
#include "xil_exception.h"
#include "xtime_l.h"
#include "config.h"
#include <string.h>


#define IRQ_LEVEL (1U << 8)
#define RETRY_US 100000U

static void barrier(void)
{
#ifdef __arm__
    __asm__ volatile ("dsb sy" ::: "memory");
#else
    __asm__ volatile ("" ::: "memory");
#endif
}

static u32 read_status(CH32H417_Irq *irq)
{
    u32 status = CH32H417_IP_mReadReg(irq->Device->BaseAddress, CH32H417_IP_STATUS0);
    u32 bits = status & CH32H417_IRQ_ERRORS;
    u32 fresh = bits & ~irq->LatchedErrors;
    unsigned i;
    for (i = 0; i < 8; ++i) if (fresh & (1U << i)) ++irq->Observed[i];
    if (fresh) irq->LastObservedStatus = status;
    irq->LatchedErrors = bits;
    irq->ErrorsSeen |= bits;
    return status;
}

static void capture(CH32H417_Irq *irq)
{
    irq->Snapshot0 = read_status(irq);
    irq->Snapshot1 = CH32H417_IP_mReadReg(irq->Device->BaseAddress,
                                        CH32H417_IP_STATUS1);
    irq->ErrorsSeen |= irq->Snapshot0 & CH32H417_IRQ_ERRORS;
}

static void clear_level(CH32H417_Irq *irq, u32 value)
{
    CH32H417_IP_mWriteReg(irq->Device->BaseAddress, CH32H417_IP_CTRL1, value);
    /* Complete the AXI write before starting the pulse/ack interval. */
    (void)CH32H417_IP_mReadReg(irq->Device->BaseAddress, CH32H417_IP_CTRL1);
    barrier();
}

void CH32H417_IrqHandler(void *reference)
{
    CH32H417_Irq *irq = reference;
    if (irq == NULL || !irq->Initialized) return;
    XScuGic_Disable(irq->Gic, irq->Id);
    barrier();
    irq->Masked = 1;
    capture(irq);
    irq->LastIrqStatus0 = irq->Snapshot0;
    irq->LastIrqStatus1 = irq->Snapshot1;
    ++irq->InterruptCount;
    barrier();
    irq->Pending = 1;
    /* The GIC dispatcher performs EOI. No clearing, printing or waits here. */
}

int CH32H417_IrqAttach(CH32H417_Irq *irq, CH32H417_IP *device,
                     XScuGic *gic, u32 id)
{
    int rc;
    if (irq == NULL || device == NULL || gic == NULL ||
        device->TimeoutUs == 0 || id != CH32H417_BOARD_IRQ_ID)
        return XST_FAILURE;
    memset(irq, 0, sizeof(*irq));
    irq->Device = device;
    irq->Gic = gic;
    irq->Id = id;
    XScuGic_Disable(gic, id);
    irq->Masked = 1;
    rc = XScuGic_Connect(gic, id, CH32H417_IrqHandler, irq);
    if (rc != XST_SUCCESS) return rc;
    XScuGic_SetPriorityTriggerType(gic, id, 0xA0, 0x1); /* High level. */
    irq->Initialized = 1;
    irq->Masked = 0;
    barrier();
    XScuGic_Enable(gic, id);
    return XST_SUCCESS;
}

int CH32H417_IrqSetupStandalone(CH32H417_Irq *irq, CH32H417_IP *device)
{
    static XScuGic gic;
    static int ready;
    XScuGic_Config *config;
    int rc;
    if (!ready) {
        config = XScuGic_LookupConfig(XPAR_SCUGIC_SINGLE_DEVICE_ID);
        if (config == NULL) return XST_FAILURE;
        rc = XScuGic_CfgInitialize(&gic, config, config->CpuBaseAddress);
        if (rc != XST_SUCCESS) return rc;
        Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
            (Xil_ExceptionHandler)XScuGic_InterruptHandler, &gic);
        ready = 1;
    }
    rc = CH32H417_IrqAttach(irq, device, &gic, CH32H417_BOARD_IRQ_ID);
    if (rc == XST_SUCCESS) Xil_ExceptionEnable();
    return rc;
}

void CH32H417_IrqRequestClear(CH32H417_Irq *irq)
{
    if (irq == NULL || !irq->Initialized) return;
    XScuGic_Disable(irq->Gic, irq->Id);
    barrier();
    irq->Masked = 1;
    if (irq->State == CH32_IRQ_IDLE && !irq->Pending) {
        capture(irq);
        irq->Pending = 1;
    }
}

static u32 backoff(CH32H417_Irq *irq, uint64_t now, int timeout)
{
    irq->State = CH32_IRQ_BACKOFF;
    irq->SinceUs = now;
    ++irq->RetryCount;
    if (timeout) ++irq->TimeoutCount;
    return timeout ? CH32H417_IRQ_TIMEOUT : CH32H417_IRQ_RETRY;
}

u32 CH32H417_IrqService(CH32H417_Irq *irq, uint64_t now)
{
    u32 status;
    if (irq == NULL || !irq->Initialized) return 0;
    switch (irq->State) {
    case CH32_IRQ_IDLE:
        if (!irq->Pending) return 0;
        /* The ISR masked the source before publishing this snapshot. */
        barrier();
        irq->Pending = 0;
        irq->State = CH32_IRQ_WAIT_IDLE;
        irq->SinceUs = now;
        return CH32H417_IRQ_CAPTURED;
    case CH32_IRQ_WAIT_IDLE:
        status = read_status(irq);
        irq->ErrorsSeen |= status & CH32H417_IRQ_ERRORS;
        if (status & CH32H417_IP_CLEAR_BUSY) {
            if (now - irq->SinceUs >= irq->Device->TimeoutUs)
                return backoff(irq, now, 1);
            return 0;
        }
        clear_level(irq, 0);
        irq->State = CH32_IRQ_LOW;
        irq->SinceUs = now;
        return 0;
    case CH32_IRQ_LOW:
        if (now - irq->SinceUs < 1U) return 0;
        /* Record faults that appeared while masked before clearing latches. */
        capture(irq);
        clear_level(irq, 1);
        irq->State = CH32_IRQ_WAIT_ACK;
        irq->SinceUs = now;
        return 0;
    case CH32_IRQ_WAIT_ACK:
        if (now - irq->SinceUs < 1U) return 0;
        status = read_status(irq);
        irq->ErrorsSeen |= status & CH32H417_IRQ_ERRORS;
        if (status & CH32H417_IP_CLEAR_BUSY) {
            if (now - irq->SinceUs >= irq->Device->TimeoutUs) {
                clear_level(irq, 0);
                return backoff(irq, now, 1);
            }
            return 0;
        }
        clear_level(irq, 0);
        irq->State = CH32_IRQ_VERIFY;
        irq->SinceUs = now;
        return 0;
    case CH32_IRQ_VERIFY:
        if (now - irq->SinceUs < 2U) return 0;
        capture(irq);
        if (irq->Snapshot0 & (CH32H417_IRQ_ERRORS | IRQ_LEVEL))
            return backoff(irq, now, 0);
        ++irq->ClearCount;
        /* Remove only this source's stale pending latch before rearming.
         * A new active level at/after enable will be delivered again. */
        XScuGic_DistWriteReg(irq->Gic,
            XSCUGIC_PENDING_CLR_OFFSET + (irq->Id / 32U) * 4U,
            1U << (irq->Id % 32U));
        irq->State = CH32_IRQ_IDLE;
        irq->Masked = 0;
        barrier();
        XScuGic_Enable(irq->Gic, irq->Id);
        return CH32H417_IRQ_CLEARED;
    case CH32_IRQ_BACKOFF:
        if (now - irq->SinceUs < RETRY_US) return 0;
        capture(irq);
        irq->State = CH32_IRQ_WAIT_IDLE;
        irq->SinceUs = now;
        return 0;
    default:
        return 0;
    }
}

uint64_t CH32H417_IrqNowUs(void)
{
    XTime ticks;
    XTime_GetTime(&ticks);
    return (ticks / COUNTS_PER_SECOND) * 1000000ULL +
        ((ticks % COUNTS_PER_SECOND) * 1000000ULL) / COUNTS_PER_SECOND;
}
