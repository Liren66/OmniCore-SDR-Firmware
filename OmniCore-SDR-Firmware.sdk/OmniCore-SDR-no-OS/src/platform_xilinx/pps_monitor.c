#include "pps_monitor.h"
#include "xil_io.h"
#include "xtime_l.h"
#include <string.h>

#define CONFIG 0x000CU
#define RX_MASK 0x0010U
#define TX_MASK 0x4010U
#define COUNTER 0x00C0U
#define STATUS 0x00C4U
#define LOST_US 1500000ULL
static XScuGic *controller;
static uint32_t address, available, enabled, nominal;
static volatile uint32_t pulses, invalid, have_edge, have_interval, valid, measured, raw;
static volatile uint64_t last_us;
static uint64_t armed_us;

static void barrier(void)
{
#ifdef __arm__
    __asm__ volatile ("dsb sy" ::: "memory");
#endif
}
static uint64_t now_us(void)
{
    XTime time; XTime_GetTime(&time);
    return ((uint64_t)time/COUNTS_PER_SECOND)*1000000U +
        ((uint64_t)time%COUNTS_PER_SECOND)*1000000U/COUNTS_PER_SECOND;
}
static void disable_irq(void)
{
    XScuGic_Disable(controller, PPS_BOARD_IRQ_ID); barrier();
}
static void masks(unsigned masked)
{
    Xil_Out32(address+RX_MASK, (Xil_In32(address+RX_MASK)&~1U)|masked);
    Xil_Out32(address+TX_MASK, (Xil_In32(address+TX_MASK)&~1U)|masked);
    (void)Xil_In32(address+TX_MASK); barrier();
}
static void clear_pending(void)
{
    XScuGic_DistWriteReg(controller, XSCUGIC_PENDING_CLR_OFFSET+
        (PPS_BOARD_IRQ_ID/32U)*4U, 1U<<(PPS_BOARD_IRQ_ID%32U));
    barrier();
}
static void reset_measurement(void)
{
    have_edge=have_interval=valid=measured=raw=0; last_us=0; armed_us=now_us();
}
static void handler(void *unused)
{
    uint64_t now, interval; uint32_t count;
    (void)unused;
    if(!enabled || !nominal) return;
    now=now_us(); count=Xil_In32(address+COUNTER);
    interval=now-last_us;
    ++pulses; raw=count; valid=0; measured=0;
    /* The PS timer only rejects missing pulses/noise. Frequency comes from
     * the hardware-latched DATA_CLK counter, assuming an external 1 Hz PPS.
     * RTL clears to zero on an edge, hence an N-cycle interval reads N-1. */
    have_interval=have_edge;
    if(have_edge) {
        if(interval>=900000ULL && interval<=1100000ULL && count!=UINT32_MAX &&
           (uint64_t)(count+1U)*10U>=(uint64_t)nominal*9U &&
           (uint64_t)(count+1U)*10U<=(uint64_t)nominal*11U) {
            measured=count+1U; valid=1;
        } else ++invalid;
    }
    last_us=now; have_edge=1;
    /* No printing, RF writes or GIC EOI here; the shared dispatcher owns EOI. */
}
int pps_monitor_init(XScuGic *gic, uint32_t base)
{
    controller=gic; address=base; enabled=nominal=available=0;
    pulses=invalid=0; reset_measurement();
    if(!gic || !(Xil_In32(address+CONFIG)&256U)) return -1;
    disable_irq(); masks(1); clear_pending();
    if(XScuGic_Connect(gic,PPS_BOARD_IRQ_ID,handler,0)!=0) return -1;
    XScuGic_SetPriorityTriggerType(gic,PPS_BOARD_IRQ_ID,0xA8,0x3);
    available=1; return 0; /* Cold boot remains disabled. */
}
int pps_monitor_available(void) { return (int)available; }
int pps_monitor_enable(unsigned value)
{
    if(value>1 || !available) return -1;
    if(value==enabled) return 0;
    disable_irq(); masks(1); clear_pending();
    enabled=value; pulses=invalid=0; reset_measurement();
    if(enabled && nominal) {
        XScuGic_Enable(controller,PPS_BOARD_IRQ_ID); masks(0);
    }
    return 0;
}
void pps_monitor_nominal(uint32_t hz)
{
    if(!available || hz==nominal) return;
    disable_irq(); masks(1); clear_pending();
    nominal=hz; reset_measurement();
    if(enabled && nominal) {
        XScuGic_Enable(controller,PPS_BOARD_IRQ_ID); masks(0);
    }
}
void pps_monitor_status(PpsStatus *s)
{
    uint64_t now, age;
    memset(s,0,sizeof(*s)); s->age_ms=UINT32_MAX;
    s->available=available;
    if(!available) return;
    disable_irq(); now=now_us(); age=now-last_us;
    s->enabled=enabled; s->nominal_hz=nominal;
    s->pulse_count=pulses; s->invalid_count=invalid;
    s->hardware_timeout=Xil_In32(address+STATUS)&1U;
    if(!enabled) s->state=PPS_DISABLED;
    else if(!nominal) s->state=PPS_CLOCK_UNAVAILABLE;
    else if(!have_edge) s->state=now-armed_us>LOST_US?PPS_NO_SIGNAL:PPS_WAITING;
    else {
        s->age_ms=age/1000U>UINT32_MAX?UINT32_MAX:(uint32_t)(age/1000U);
        s->signal_present=age<=LOST_US;
        s->frequency_valid=s->signal_present && valid;
        s->state=!s->signal_present?PPS_NO_SIGNAL:valid?PPS_VALID:
            have_interval?PPS_INVALID_INTERVAL:PPS_WAITING;
        if(s->frequency_valid) {s->measured_hz=measured; s->raw_counter=raw;}
    }
    if(enabled && nominal) XScuGic_Enable(controller,PPS_BOARD_IRQ_ID);
}
