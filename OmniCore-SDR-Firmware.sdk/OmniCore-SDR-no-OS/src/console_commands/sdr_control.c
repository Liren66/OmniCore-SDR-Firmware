/* 鏈澘 RF 涓� USB 杩愯鐘舵�佹満銆傚懡浠ゅ瓧绗︿覆涓庡洖璋冭 sdr_commands.c銆�
 * 鍙湪涓诲惊鐜墽琛岋紱涓柇璐熻矗閿佸瓨浜嬩欢锛岃�楁椂鎭㈠鍜屾墦鍗板湪 service 涓鐞嗐��
 * OmniCore SDR command protocol 1.0, SDK 2026-10-26 (VCXO / PA4 tune modes).
 * Main-loop state machine. No MCU/FPGA changes. RX overflow recovery rearms
 * the existing 16 KiB preamble; never claim lost IQ has been restored.
 */
#include "sdr_control.h"
#include "sdr_iq_format.h"
#include "sdr_runtime.h"
#include "sdr_fir.h"
#include "sdr_correction.h"
#include "sdr_reference.h"
#include "sdr_external_lo.h"
#include "sdr_pps.h"
#include "sdr_vcxo.h"
#include "sdr_board.h"
#include "board_monitor.h"
#include "sdr_clock.h"
#include "console.h"
#include "ad9361_api.h"
#include "dac_core.h"
#include "ch32h417_irq.h"
#include "xparameters.h"
#include "platform.h"
#include "adf4002.h"
#include "pps_monitor.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "parameters.h"

extern struct dds_state dds_st[];
extern void ad9361_remove(struct ad9361_rf_phy *phy);
extern int32_t ad9361_digital_check(struct ad9361_rf_phy *phy, int tx);

#define RX_BITS (CH32H417_IP_RX_EN | CH32H417_IP_RX_DUAL | CH32H417_IP_RX_CH1)
#define TX_BITS (CH32H417_IP_TX_EN | CH32H417_IP_TX_DUAL | CH32H417_IP_TX_CH1 | CH32H417_IP_TX_RUN)
/* Match packaged rev8 Fsm_ctr: 1024 groups. Use a conservative prefill margin. */
#define PREFILL_SINGLE 1350U
#define PREFILL_DUAL 2700U
/* IQ8 fills about 2046 raw groups from the first 16 KiB USB block. Waiting
 * for 2700 would deadlock: block two is held off until TX_RUN drains space.
 * 1536 retains margin above the hardware's 1024-group start threshold. */
#define PREFILL_IQ8 1536U
#define EVENT_CAPACITY 16U

static CH32H417_IP bridge;
static CH32H417_Irq irq;
SdrRuntimeState sdr_state;
static uint32_t seen[8], counts[8], seq, dropped, last_error, last_status;
static uint32_t outstanding;
static unsigned rx_resync_pending, rx_resync_ready;
static uint32_t rx_resync_stream, rx_resyncs, rx_resync_failures;
static uint64_t rx_resync_retry_at;
static uint64_t last_notice[8];
static uint32_t notice_stream[8];
static const char *fault_name[8] = {"rx_overflow", "rx_underflow", "tx_overflow", "tx_underflow",
                                    "rx_protocol", "tx_protocol",  "rx_pair",     "tx_pair"};
static const unsigned tx_fault[8] = {0, 0, 1, 1, 0, 1, 0, 1};

typedef struct {
    uint32_t seq, stream, count, status;
    unsigned mask, kind, recovered;
} Event;

static Event events[EVENT_CAPACITY], announced[8];
static unsigned event_read, event_count;

int sdr_command_error(const char *what, int rc)
{
    console_print("ERR %s code=%d\n", what, rc);
    return -1;
}

static uint32_t config_bits(unsigned rm, unsigned tm, unsigned re, unsigned te, unsigned run)
{
    return (re ? CH32H417_IP_RX_EN : 0) | (te ? CH32H417_IP_TX_EN : 0) |
           (rm == 3   ? CH32H417_IP_RX_DUAL
            : rm == 2 ? CH32H417_IP_RX_CH1
                      : 0) |
           (tm == 3   ? CH32H417_IP_TX_DUAL
            : tm == 2 ? CH32H417_IP_TX_CH1
                      : 0) |
           (run ? CH32H417_IP_TX_RUN : 0) | sdr_iq_format_bits();
}

static int configure(uint32_t bits)
{
    int rc;
    if (!sdr_state.bridge_ready) {
        return sdr_command_error("bridge_unavailable", -1);
    }
    rc = CH32H417_IP_Configure(&bridge, bits);
    if (rc) {
        return sdr_command_error("bridge_config applied=partial inspect=usb_status?", rc);
    }
    return 0;
}

/* DATA_SEL_ZERO is after the CH32 DMA input mux, so both DDS and USB are gated.
 * Verify actual AXI registers, never infer output state from a cached variable. */
int sdr_set_tx_gate(unsigned enable)
{
    unsigned c;
    uint32_t actual;
    int rc = 0;
    if (!ad9361_phy) {
        return enable ? -1 : 0;
    }
    for (c = 0; c < 4; ++c) {
        unsigned sel = enable && (sdr_state.tx_mask & (1U << (c / 2)))
                           ? (sdr_state.tx_source ? DATA_SEL_DMA : DATA_SEL_DDS)
                           : DATA_SEL_ZERO;
        if (dac_datasel(ad9361_phy, c, sel) < 0) {
            rc = -1;
        }
        dac_read(ad9361_phy, DAC_REG_CHAN_CNTRL_7(c), &actual);
        if (actual != sel) {
            rc = -1;
        }
    }
    dac_start_sync(ad9361_phy, 0);
    return rc;
}

unsigned sdr_actual_tx_mask(void)
{
    unsigned c, mask = 0;
    uint32_t a, b;
    if (!ad9361_phy) {
        return 0;
    }
    for (c = 0; c < 2; ++c) {
        dac_read(ad9361_phy, DAC_REG_CHAN_CNTRL_7(c * 2), &a);
        dac_read(ad9361_phy, DAC_REG_CHAN_CNTRL_7(c * 2 + 1), &b);
        if (a != DATA_SEL_ZERO || b != DATA_SEL_ZERO) {
            mask |= 1U << c;
        }
    }
    return mask;
}

int sdr_stop_tx(void)
{
    int rc = sdr_set_tx_gate(0), r;
    unsigned c;
    sdr_state.tx_enabled = 0;
    if (ad9361_phy) {
        for (c = 0; c < 2; ++c) {
            if (ad9361_set_tx_attenuation(ad9361_phy, c, 89750) < 0) {
                rc = -1;
            }
        }
    }
    r = configure(config_bits(sdr_state.rx_mask, sdr_state.tx_mask, sdr_state.usb_rx, 0, 0));
    if (!r) {
        sdr_state.usb_tx = 0;
    }
    return rc || r ? sdr_command_error("tx_stop applied=unknown", -1) : 0;
}

int sdr_stop_all(void)
{
    int rc = sdr_stop_tx();
    if (configure(config_bits(sdr_state.rx_mask, sdr_state.tx_mask, 0, 0, 0))) {
        return -1;
    }
    sdr_state.usb_rx = sdr_state.usb_tx = 0;
    return rc;
}

int sdr_set_attenuation(unsigned c, uint32_t v)
{
    int rc;
    if (c > 1 || v > 89750 || v % 250 || !sdr_state.rf_ready) {
        return -1;
    }
    rc = ad9361_set_tx_attenuation(
        ad9361_phy, c, sdr_state.tx_enabled && (sdr_state.tx_mask & (1U << c)) ? v : 89750);
    if (rc < 0) {
        return rc;
    }
    sdr_state.attenuation[c] = v;
    return 0;
}

int sdr_get_attenuation(unsigned c, uint32_t *v)
{
    if (c > 1 || !v) {
        return -1;
    }
    *v = sdr_state.attenuation[c];
    return 0;
}

/*
 * AD9361_RESET = FCLK_RESET0_N & GPIO_O[0].
 * GPIO_RESET_PIN is 54: PS EMIO GPIO[0].
 * Call only after stopping RF/USB streaming.
 */
static void board_rf_reset(void)
{
    /* AD9361 复位会中断数据时钟，先使 PPS 测量失效。 */
    pps_monitor_nominal(0);

    gpio_set_value(GPIO_RESET_PIN, 0);
    mdelay(2);

    gpio_set_value(GPIO_RESET_PIN, 1);
    mdelay(5);
}

int sdr_rf_boot(void)
{
    AD9361_InitParam init = default_init_param;
    CH32H417_IP_Status retained;
    sdr_state.attenuation[0] = sdr_state.attenuation[1] = init.tx_attenuation_mdB;
    init.tx_attenuation_mdB = 89750;
    /* Stop retained bridge state even on an ELF-only restart. Preserve the
     * applied format here: changing IQ8 and EN in the same write is illegal.
     * Do not wait for RF clocks before RF initialization. control_init later
     * waits for stop/ready acknowledgement and selects the 12-bit default. */
    CH32H417_IP_Initialize(&bridge, XPAR_CH32H417_IP_0_S00_AXI_BASEADDR,
                          CH32H417_IP_DEFAULT_TIMEOUT_US);
    CH32H417_IP_GetStatus(&bridge, &retained);
    CH32H417_IP_mWriteReg(bridge.BaseAddress, CH32H417_IP_CTRL0,
                          retained.AppliedConfig &
                              ~(CH32H417_IP_RX_EN | CH32H417_IP_TX_EN | CH32H417_IP_TX_RUN));
    board_rf_reset();
    /* Stop retained RF/PL state before any fallible peripheral init. GPIO is
     * initialized by main. Only cold startup defaults the reference source. */
    sdr_state.rf_error = adf4002_init();
    if (sdr_state.rf_error < 0) {
        return sdr_state.rf_error;
    }
    sdr_state.rf_error = ad9361_init(&ad9361_phy, &init);
    if (sdr_state.rf_error < 0) {
        return sdr_state.rf_error;
    }
    dac_init(ad9361_phy, DATA_SEL_ZERO);
    sdr_state.rf_error = sdr_set_tx_gate(0);
    if (!sdr_state.rf_error) {
        sdr_state.rf_error = sdr_fir_boot();
    }
    if (!sdr_state.rf_error) {
        sdr_state.rf_error = sdr_correction_restore();
    }
    if (!sdr_state.rf_error) {
        sdr_state.rf_error = sdr_external_lo_restore();
    }
    sdr_fir_set_verified(sdr_state.rf_error == 0);
    sdr_state.rf_ready = sdr_state.digital_ready = sdr_state.rf_error == 0;
    return sdr_state.rf_error;
}

int sdr_control_init(void)
{
    int rc = CH32H417_IP_Initialize(&bridge, XPAR_CH32H417_IP_0_S00_AXI_BASEADDR,
                                    CH32H417_IP_DEFAULT_TIMEOUT_US);
    sdr_state.bridge_ready = rc == 0;
    if (rc) {
        return rc;
    }
    rc = CH32H417_IP_Configure(&bridge, 0);
    if (rc) {
        sdr_state.bridge_ready = 0;
        return rc;
    }
    sdr_iq_format_init(&bridge);
    rc = CH32H417_IrqSetupStandalone(&irq, &bridge);
    if (rc) {
        sdr_state.bridge_ready = 0;
        return rc;
    }
    /* Console RX by interrupt: CH32 vcxo_ack lines and host commands may now
     * arrive while the main loop is busy (SPI, ADF4002, XADC); polling alone
     * overran the 16-byte UART FIFO. Failure keeps the polled console. */
    (void)console_enable_rx_irq(irq.Gic);
    /* Share the existing GIC; PPS failure must not disable the USB bridge. */
    (void)pps_monitor_init(irq.Gic, XPAR_AXI_AD9361_BASEADDR);
    sdr_pps_refresh();
    return 0;
}

/* Keep USB requests running. After the overflow latch can stay clear, toggle
 * only RX_EN. A 100 us quiet interval lets a previously admitted 4101-cycle
 * UHSIF read finish (31 us at 133.333 MHz). Its tail is invalid and belongs
 * BEFORE the new preamble. RX FIFO/packing phase and preamble reset together.
 * TX_EN, TX_RUN and both channel selections are copied from applied hardware.
 * No global reset, RF reinit, USB reset, or MCU control request is involved. */
static int restart_rx_header(void)
{
    uint32_t bits;
    int rc = CH32H417_IP_GetConfig(&bridge, &bits);
    if (rc) {
        return rc;
    }
    rc = CH32H417_IP_Configure(&bridge, bits & ~CH32H417_IP_RX_EN);
    if (rc) {
        return rc;
    }
    udelay(100);
    return CH32H417_IP_Configure(&bridge, bits | CH32H417_IP_RX_EN);
}

static void enqueue(unsigned kind, unsigned recovered, uint32_t status)
{
    Event e;
    if (recovered) {
        e = announced[kind];
        /* A user may have stopped/restarted a stream during the clear. Keep
         * the original fault's stream/mask; never claim its successor healed. */
        if (!((tx_fault[kind] ? sdr_state.usb_tx : sdr_state.usb_rx) &&
              e.stream == (tx_fault[kind] ? sdr_state.tx_stream : sdr_state.rx_stream))) {
            recovered = 2;
        }
    } else {
        e.kind = kind;
        e.stream = tx_fault[kind] ? sdr_state.tx_stream : sdr_state.rx_stream;
        e.mask = tx_fault[kind] ? sdr_state.tx_mask : sdr_state.rx_mask;
        e.count = counts[kind];
    }
    e.seq = ++seq;
    e.recovered = recovered;
    e.status = status;
    if (!recovered) {
        announced[kind] = e;
    }
    if (event_count == EVENT_CAPACITY) {
        ++dropped;
        return;
    }
    events[(event_read + event_count) % EVENT_CAPACITY] = e;
    ++event_count;
}

void sdr_control_service(void)
{
    unsigned k, before = irq.State;
    uint32_t action, n;
    uint64_t now = CH32H417_IrqNowUs();
    sdr_pps_refresh();
    if (rx_resync_pending && (!sdr_state.usb_rx || rx_resync_stream != sdr_state.rx_stream)) {
        rx_resync_pending = rx_resync_ready = 0;
    }
    action = CH32H417_IrqService(&irq, now);
    for (k = 0; k < 8; ++k) {
        n = irq.Observed[k];
        if (n == seen[k]) {
            continue;
        }
        /* Do not attribute stopped/prefill/previous-session faults to live TX. */
        if ((tx_fault[k] ? (sdr_state.usb_tx && ((k != 3 && k != 7) || sdr_state.tx_enabled))
                         : sdr_state.usb_rx)) {
            counts[k] += n - seen[k];
            last_error = 1U << k;
            last_status = irq.LastObservedStatus;
            if (k == 0) {
                rx_resync_pending = 1;
                rx_resync_stream = sdr_state.rx_stream;
                rx_resync_ready = 0;
            }
            if ((k == 0 && !(outstanding & 1U)) || !last_notice[k] ||
                now - last_notice[k] >= 500000U ||
                notice_stream[k] != (tx_fault[k] ? sdr_state.tx_stream : sdr_state.rx_stream)) {
                outstanding |= 1U << k;
                enqueue(k, 0, last_status);
                last_notice[k] = now;
                notice_stream[k] = tx_fault[k] ? sdr_state.tx_stream : sdr_state.rx_stream;
            }
        }
        seen[k] = n;
    }
    /* Do not restart repeatedly while the host is not draining RX. A verified
     * RX clear is sufficient even if an independent TX fault remains active. */
    if (rx_resync_pending && ((action & CH32H417_IRQ_CLEARED) ||
                              (before == CH32_IRQ_VERIFY && irq.State == CH32_IRQ_BACKOFF &&
                               !(irq.Snapshot0 & (0x53U | CH32H417_IP_CLEAR_BUSY))))) {
        rx_resync_ready = 1;
    }
    if (action & CH32H417_IRQ_CLEARED) {
        for (k = 0; k < 8; ++k) {
            if ((outstanding & (1U << k)) && !(k == 0 && rx_resync_pending)) {
                enqueue(k, 1, irq.Snapshot0);
                outstanding &= ~(1U << k);
            }
        }
    }
    if (rx_resync_pending && rx_resync_ready && now >= rx_resync_retry_at) {
        CH32H417_IP_Status s;
        int rc = restart_rx_header();
        CH32H417_IP_GetStatus(&bridge, &s);
        if (!rc) {
            ++rx_resyncs;
            rx_resync_pending = rx_resync_ready = 0;
            enqueue(0, 3, s.Status0);
            outstanding &= ~1U;
        } else {
            ++rx_resync_failures;
            enqueue(0, 4, s.Status0);
        }
        rx_resync_retry_at = now + 500000U;
    }
    /* One complete line, main context only. Coalescing bounds the UART load. */
    if (event_count) {
        Event *e = &events[event_read];
        console_print(
            "EVT seq=%lu stream=%lu type=%s mask=%u src=fpga count=%lu lost=-1 action=%s status=0x%08lx header_bytes=%u\n",
            (unsigned long)e->seq, (unsigned long)e->stream, fault_name[e->kind], e->mask,
            (unsigned long)e->count,
            e->recovered == 4   ? "rx_restart_retry"
            : e->recovered == 3 ? "rx_restarted"
            : e->recovered == 2 ? "errors_cleared"
            : e->recovered      ? "recovered"
            : e->kind == 0      ? "waiting_rx_resync"
                                : "clearing_continue",
            (unsigned long)e->status, e->recovered == 3 ? 16384U : 0U);
        event_read = (event_read + 1) % EVENT_CAPACITY;
        --event_count;
    }
    sdr_board_service(rx_resync_pending || irq.Pending || irq.State != CH32_IRQ_IDLE);
}

int sdr_guard(const char *name)
{
    if (!strcmp(name, "help?")) {
        return 1;
    }
    if (!sdr_state.rf_ready || !ad9361_phy) {
        sdr_command_error("rf_not_initialized use=rf_reinit!", sdr_state.rf_error);
        return 0;
    }
    if ((strstr(name, "_samp_freq=") || strstr(name, "_fir_en=")) &&
        (sdr_state.tx_enabled || sdr_state.usb_rx || sdr_state.usb_tx)) {
        sdr_command_error("busy stop_tx_and_usb_before_clock_or_fir_change", -1);
        return 0;
    }
    if (strstr(name, "_samp_freq=") || strstr(name, "_fir_en=")) {
        pps_monitor_nominal(0);
    }
    return 1;
}

static void print_history(void)
{
    console_print(
        "faults rx_overflow=%lu rx_underflow=%lu tx_overflow=%lu tx_underflow=%lu rx_protocol=%lu tx_protocol=%lu rx_pair=%lu tx_pair=%lu last=0x%02lx last_status=0x%08lx event_seq=%lu event_dropped=%lu lost=-1\n",
        (unsigned long)counts[0], (unsigned long)counts[1], (unsigned long)counts[2],
        (unsigned long)counts[3], (unsigned long)counts[4], (unsigned long)counts[5],
        (unsigned long)counts[6], (unsigned long)counts[7], (unsigned long)last_error,
        (unsigned long)last_status, (unsigned long)seq, (unsigned long)dropped);
}

void sdr_print_usb_config(void)
{
    CH32H417_IP_Status s;
    CH32H417_IP_GetStatus(&bridge, &s);
    console_print("usb_config=requested:0x%02x applied:0x%02x cfg_busy:%u cfg_error:%u\n",
                  (unsigned)s.RequestedConfig, (unsigned)s.AppliedConfig,
                  !!(s.Status0 & CH32H417_IP_CFG_BUSY), !!(s.Status0 & CH32H417_IP_CFG_ERROR));
}

void sdr_print_irq(void)
{
    console_print(
        "usb_irq=id:%u initialized:%u masked:%u pending:%u state:%u count:%u history:0x%08x clear:%u retry:%u timeout:%u last_irq:0x%08x/0x%08x\n",
        (unsigned)irq.Id, (unsigned)irq.Initialized, (unsigned)irq.Masked, (unsigned)irq.Pending,
        (unsigned)irq.State, (unsigned)irq.InterruptCount, (unsigned)irq.ErrorsSeen,
        (unsigned)irq.ClearCount, (unsigned)irq.RetryCount, (unsigned)irq.TimeoutCount,
        (unsigned)irq.LastIrqStatus0, (unsigned)irq.LastIrqStatus1);
    print_history();
}

void sdr_print_usb_status(void)
{
    CH32H417_IP_Status s;
    uint32_t r = 0, t = 0;
    CH32H417_IP_GetStatus(&bridge, &s);
    if (sdr_state.rf_ready) {
        ad9361_get_rx_sampling_freq(ad9361_phy, &r);
        ad9361_get_tx_sampling_freq(ad9361_phy, &t);
    }
    console_print("usb_status=0x%08x 0x%08x\n", (unsigned)s.Status0, (unsigned)s.Status1);
    console_print("errors=0x%02x cfg_error=%u irq=%u cfg_busy=%u clear_busy=%u\n",
                  (unsigned)(s.Status0 & 255), !!(s.Status0 & CH32H417_IP_CFG_ERROR),
                  !!(s.Status0 & 256), !!(s.Status0 & CH32H417_IP_CFG_BUSY),
                  !!(s.Status0 & CH32H417_IP_CLEAR_BUSY));
    console_print(
        "rx_ready=%u/%u tx_ready=%u/%u tx_started=%u rdne=%u wrnf=%u rx_groups=%u tx_main_fifo_groups=%u group_bytes=%u fifo_group_bits=96\n",
        !!(s.Status0 & (1U << 16)), !!(s.Status0 & (1U << 18)), !!(s.Status0 & (1U << 17)),
        !!(s.Status0 & (1U << 19)), !!(s.Status0 & (1U << 11)), !!(s.Status0 & 512),
        !!(s.Status0 & 1024), (unsigned)s.RxGroups, (unsigned)s.TxGroups,
        (s.AppliedConfig & CH32H417_IP_IQ8) ? 8U : 12U);
    console_print(
        "usb_stream rx=%lu tx=%lu rx_mask=%u tx_mask=%u expected_rx_Bps=%lu expected_tx_Bps=%lu actual_rx_Bps=unknown actual_tx_Bps=unknown window_ms=unknown usb_speed=unknown\n",
        (unsigned long)sdr_state.rx_stream, (unsigned long)sdr_state.tx_stream, sdr_state.rx_mask,
        sdr_state.tx_mask,
        (unsigned long)(sdr_state.usb_rx ? r * ((s.AppliedConfig & CH32H417_IP_IQ8) ? 4 : (sdr_state.rx_mask == 3 ? 6 : 3)) : 0),
        (unsigned long)(sdr_state.usb_tx ? t * ((s.AppliedConfig & CH32H417_IP_IQ8) ? 4 : (sdr_state.tx_mask == 3 ? 6 : 3)) : 0));
    sdr_iq_format_status();
    print_history();
    console_print("rx_resync completed=%lu pending=%u failures=%lu header_bytes=16384\n",
                  (unsigned long)rx_resyncs, rx_resync_pending, (unsigned long)rx_resync_failures);
}

void sdr_print_system_info(void)
{
    console_print(
        "system_info board=OmniCore_SDR chip=AD9361 firmware=SDK_CMD_1.0_20261026_VCXO protocol=1.0 fpga=unknown channels_rx=2 channels_tx=2 sources=0:DDS,1:USB\n");
    console_print(
        "capabilities channel=1 tx_gate=1 usb=1 clock_chain=1 supported_fir_ids=0 fir_upload=1 clock_config=1 rx_correction=1 adf4002=1 ref_source=1 external_lo=1 rf_reinit=1 digital_self_test=1 events=1 overflow_policy=rx_restart_header_report clock_ref_source=adf4002_10MHz_discipline lock_readback=0 pps_monitor=%u pps_adjustment=1 vcxo=1\n",
        pps_monitor_available());
    console_print("iq_capabilities iq_format=1 iq8_2t2r=%u default_bits=12\n", sdr_iq_format_capable());
    console_print("rf_capabilities rssi=1 console_baud=%lu console_rx=%s\n",
                  (unsigned long)CONSOLE_BAUD_RATE, console_rx_irq_active() ? "irq" : "polled");
    console_print("board_capabilities board_rgb=%u fan_control=%u core_temp=1\n",
                  board_monitor_available(), board_monitor_available());
    if (sdr_iq_format_bits()) {
        console_print("iq format=signed8_twos_complement valid_bits=8 container_bits=8 sample_bytes=2 order=I1,Q1,I2,Q2 rx_conversion=bits11_4 tx_conversion=append_4_zero_bits header_version=1 header_bytes=16384 header_pattern=00..FF_repeat64 header_on=rx_start_and_overflow_recovery\n");
    } else console_print(
        "iq format=packed12_signed_twos_complement valid_bits=12 container_bits=12 sample_bytes=3 order=I1,Q1,I2,Q2 single=selected_channel byte_order=I[11:4],I[3:0]<<4|Q[11:8],Q[7:0] header_version=1 header_bytes=16384 header_pattern=00..FF_repeat64 header_on=rx_start_and_overflow_recovery\n");
}

/* RSSI reporting is off after cold boot and must be enabled explicitly. */
static unsigned rssi_enabled;

int sdr_rssi_enable(unsigned set, unsigned enabled)
{
    if (set) {
        if (enabled > 1U) {
            return sdr_command_error("rssi_enable_must_be_0_or_1", -1);
        }
        rssi_enabled = enabled;
    }
    console_print("rssi_enable=%u\n", rssi_enabled);
    return 0;
}

/* Symbol RSSI per receiver. rx_channel only selects USB upload: in 2R2T both
 * receivers measure while the ENSM is in RX or FDD. */
int sdr_print_rx_rssi(void)
{
    struct rf_rssi rss;
    double db[2] = {0.0, 0.0};
    unsigned valid[2] = {0, 0}, c, receiving;
    uint32_t ensm = 0;
    if (!sdr_state.rf_ready) {
        return sdr_command_error("rf_not_ready", -1);
    }
    ad9361_get_en_state_machine_mode(ad9361_phy, &ensm);
    receiving = ensm == ENSM_MODE_RX || ensm == ENSM_MODE_FDD || ensm == ENSM_MODE_PINCTRL_FDD_INDEP;
    for (c = 0; rssi_enabled && receiving && c < (ad9361_phy->pdata->rx2tx2 ? 2U : 1U); ++c) {
        memset(&rss, 0, sizeof(rss));
        if (ad9361_get_rx_rssi(ad9361_phy, c, &rss) == 0 && rss.multiplier != 0) {
            valid[c] = 1;
            db[c] = -(double)rss.symbol / rss.multiplier;
        }
    }
    console_print("rx_rssi enabled=%u rx1_valid=%u rx1_db=%.3f rx2_valid=%u rx2_db=%.3f rx_channel=%u ensm=%lu "
                  "unit=dB_AD9361_relative calibrated_input_power=0\n",
                  rssi_enabled, valid[0], db[0], valid[1], db[1], sdr_state.rx_mask, (unsigned long)ensm);
    return 0;
}

void sdr_print_system_status(void)
{
    int32_t temp = 0;
    struct rf_rssi rss;
    unsigned c;
    uint32_t ensm = 0;
    console_print(
        "system_status init=%s init_code=%d digital=%s rx_channel=%u tx_channel=%u tx_source=%u tx_request=%u tx_actual_mask=%u usb_data_en=%u,%u\n",
        sdr_state.rf_ready ? "ready" : "failed", sdr_state.rf_error,
        sdr_state.digital_ready ? "ready" : "unknown_or_failed", sdr_state.rx_mask,
        sdr_state.tx_mask, sdr_state.tx_source, sdr_state.tx_enabled, sdr_actual_tx_mask(),
        sdr_state.usb_rx, sdr_state.usb_tx);
    if (sdr_state.rf_ready) {
        int rc = ad9361_get_temperature(ad9361_phy, &temp);
        console_print("temperature valid=%u Celsius=%.3f source=ad9361\n", rc == 0, temp / 1000.0);
        for (c = 0; c < 2; ++c) {
            memset(&rss, 0, sizeof(rss));
            rc = ad9361_get_rx_rssi(ad9361_phy, c, &rss);
            console_print(
                "rssi rx=%u valid=%u value=%.3f unit=dB_AD9361_relative calibrated_input_power=0\n",
                c + 1, rc == 0 && rss.multiplier != 0,
                rss.multiplier ? -(double)rss.symbol / rss.multiplier : 0.0);
        }
        ad9361_get_en_state_machine_mode(ad9361_phy, &ensm);
        console_print("rf ensm=%lu tx_silence=zero_samples_and_max_attenuation_when_off\n",
                      (unsigned long)ensm);
    }
    sdr_board_status();
    sdr_print_usb_status();
    sdr_reference_status();
    sdr_pps_status();
    sdr_vcxo_status();
}

int sdr_set_usb_data(unsigned re, unsigned te)
{
    int rc;
    unsigned oldrx = sdr_state.usb_rx, oldtx = sdr_state.usb_tx;
    if ((re || te) && sdr_iq_format_check_start()) {
        return -1;
    }
    if (re && !sdr_state.rx_mask) {
        return sdr_command_error("rx_channel_zero", -1);
    }
    if (te && (!sdr_state.tx_mask || !sdr_state.tx_source)) {
        return sdr_command_error("usb_tx_requires_source_1_and_channel", -1);
    }
    if ((re || te) && (!sdr_state.rf_ready || !sdr_state.digital_ready)) {
        return sdr_command_error("rf_not_ready", -1);
    }
    if (!te && sdr_state.tx_source && sdr_state.tx_enabled && sdr_stop_tx()) {
        return -1;
    }
    sdr_control_service();
    rc = configure(config_bits(sdr_state.rx_mask, sdr_state.tx_mask, re, te,
                               sdr_state.tx_source && sdr_state.tx_enabled));
    if (rc) {
        sdr_set_tx_gate(0);
        sdr_state.tx_enabled = 0;
        return rc;
    }
    sdr_state.usb_rx = re;
    sdr_state.usb_tx = te;
    if (re && !oldrx) {
        ++sdr_state.rx_stream;
    }
    if (te && !oldtx) {
        ++sdr_state.tx_stream;
    }
    return 0;
}

int sdr_set_tx_enabled(unsigned on)
{
    CH32H417_IP_Status s;
    unsigned c;
    uint32_t rate = 0;
    uint64_t start;
    if (!on) {
        return sdr_stop_tx();
    }
    if (sdr_state.tx_enabled) {
        return 0;
    }
    if (sdr_state.tx_source && sdr_iq_format_check_start()) {
        return -1;
    }
    if (!sdr_state.rf_ready || !sdr_state.digital_ready || !sdr_state.tx_mask) {
        return sdr_command_error("tx_not_configured", -1);
    }
    if (ad9361_get_tx_sampling_freq(ad9361_phy, &rate) < 0 || !rate) {
        return sdr_command_error("tx_clock_invalid", -1);
    }
    if (!sdr_state.tx_source) {
        for (c = 0; c < 8; ++c) {
            if ((sdr_state.tx_mask & (1U << (c / 4))) &&
                dds_st[ad9361_phy->id_no].cached_freq[c] > rate / 2) {
                return sdr_command_error("dds_frequency_exceeds_nyquist", -1);
            }
        }
    }
    if (sdr_state.tx_source) {
        CH32H417_IP_GetStatus(&bridge, &s);
        if (!sdr_state.usb_tx || (s.Status0 & CH32H417_IP_TX_READY) != CH32H417_IP_TX_READY ||
            s.TxGroups < (sdr_iq_format_bits() ? PREFILL_IQ8 :
                           (sdr_state.tx_mask == 3 ? PREFILL_DUAL : PREFILL_SINGLE))) {
            return sdr_command_error("usb_tx_not_prefilled no_deferred_start", -1);
        }
        /* While TX_EN=1 and TX_RUN=0 the bridge outputs zero on every lane.
         * Select all DMA lanes BEFORE RUN: sequential AXI data-select writes
         * after RUN would briefly deassert only one lane and raise tx_pair. */
        if (sdr_set_tx_gate(1)) {
            sdr_stop_tx();
            return sdr_command_error("tx_gate_readback", -1);
        }
    }
    for (c = 0; c < 2; ++c) {
        if (ad9361_set_tx_attenuation(ad9361_phy, c,
                                      (sdr_state.tx_mask & (1U << c)) ? sdr_state.attenuation[c]
                                                                      : 89750) < 0) {
            sdr_stop_tx();
            return sdr_command_error("tx_attenuation", -1);
        }
    }
    if (sdr_state.tx_source) {
        if (configure(config_bits(sdr_state.rx_mask, sdr_state.tx_mask, sdr_state.usb_rx, 1, 1))) {
            sdr_stop_tx();
            return -1;
        }
        start = CH32H417_IrqNowUs();
        do {
            CH32H417_IP_GetStatus(&bridge, &s);
        } while (!(s.Status0 & (1U << 11)) && CH32H417_IrqNowUs() - start < 10000U);
        if (!(s.Status0 & (1U << 11))) {
            sdr_stop_tx();
            return sdr_command_error("tx_start_timeout", -1);
        }
    } else {
        if (sdr_set_tx_gate(1)) {
            sdr_stop_tx();
            return sdr_command_error("tx_gate_readback", -1);
        }
        ++sdr_state.tx_stream;
    }
    sdr_state.tx_enabled = 1;
    return 0;
}

/* Debug CTRL0 entry points are subject to exactly the same state policy. */
int sdr_set_usb_config(unsigned bits)
{
    unsigned re = !!(bits & 1), te = !!(bits & 2), run = !!(bits & 64);
    unsigned rm = (bits & 4) ? 3 : (bits & 8) ? 2 : 1, tm = (bits & 16) ? 3 : (bits & 32) ? 2 : 1;
    if ((bits & ~CH32H417_IP_CONFIG_MASK) ||
        ((bits ^ sdr_iq_format_bits()) & CH32H417_IP_IQ8)) {
        return sdr_command_error("use_iq_format_command_for_wire_format", -1);
    }
    if (sdr_iq_format_guard(rm, tm, sdr_state.tx_source)) {
        return -1;
    }
    if (run && (!sdr_state.tx_enabled || !sdr_state.tx_source || !te)) {
        return sdr_command_error("run_requires_explicit_tx_enable", -1);
    }
    if ((sdr_state.usb_rx && rm != sdr_state.rx_mask) ||
        ((sdr_state.usb_tx || sdr_state.tx_enabled) && tm != sdr_state.tx_mask)) {
        return sdr_command_error("busy stop_direction_before_channel_change", -1);
    }
    if (te && !sdr_state.tx_source) {
        return sdr_command_error("usb_tx_requires_source_1", -1);
    }
    if (sdr_state.tx_enabled && sdr_state.tx_source && !run) {
        if (sdr_stop_tx()) {
            return -1;
        }
        te = 0;
    }
    sdr_state.rx_mask = rm;
    sdr_state.tx_mask = tm;
    return sdr_set_usb_data(re, te);
}

void sdr_print_clock_chain(void)
{
    uint32_t r[6], t[6];
    int rc = ad9361_get_trx_path_clks(ad9361_phy, r, t);
    if (rc < 0) {
        sdr_command_error("clock_chain", rc);
        return;
    }
    console_print(
        "clock_chain ref=%lu unit=Hz rx=%lu,%lu,%lu,%lu,%lu,%lu tx=%lu,%lu,%lu,%lu,%lu,%lu\n",
        (unsigned long)ad9361_phy->clk_refin->rate, (unsigned long)r[0], (unsigned long)r[1],
        (unsigned long)r[2], (unsigned long)r[3], (unsigned long)r[4], (unsigned long)r[5],
        (unsigned long)t[0], (unsigned long)t[1], (unsigned long)t[2], (unsigned long)t[3],
        (unsigned long)t[4], (unsigned long)t[5]);
}

/* Snapshot validated device configuration, not a replay of arbitrary command text. */
typedef struct {
    uint64_t rxlo, txlo;
    uint32_t r[6], t[6], rxbw, txbw;
    uint8_t rxfir, txfir, gc[2];
    int32_t gain[2];
    struct dds_state dds;
} Target;

static Target saved;
static unsigned saved_valid;

static int snapshot(Target *v)
{
    unsigned c;
    if (ad9361_get_rx_lo_freq(ad9361_phy, &v->rxlo) < 0 ||
        ad9361_get_tx_lo_freq(ad9361_phy, &v->txlo) < 0 ||
        ad9361_get_trx_path_clks(ad9361_phy, v->r, v->t) < 0 ||
        ad9361_get_rx_rf_bandwidth(ad9361_phy, &v->rxbw) < 0 ||
        ad9361_get_tx_rf_bandwidth(ad9361_phy, &v->txbw) < 0 ||
        ad9361_get_rx_fir_en_dis(ad9361_phy, &v->rxfir) < 0 ||
        ad9361_get_tx_fir_en_dis(ad9361_phy, &v->txfir) < 0) {
        return -1;
    }
    for (c = 0; c < 2; ++c) {
        if (ad9361_get_rx_gain_control_mode(ad9361_phy, c, &v->gc[c]) < 0) {
            return -1;
        }
        /* Only MGC restores a fixed gain. Fast AGC without a signal can return
         * EAGAIN while acquiring gain; that must not prevent RF recovery. */
        v->gain[c] = 0;
        if (!v->gc[c] && ad9361_get_rx_rf_gain(ad9361_phy, c, &v->gain[c]) < 0) {
            return -1;
        }
    }
    /* Preserve the driver's validated requested LO; feeding quantized readback
     * into each reinit would accumulate fractional-PLL rounding error. */
    v->rxlo = ad9361_from_clk(ad9361_phy->current_rx_lo_freq);
    v->txlo = ad9361_from_clk(ad9361_phy->current_tx_lo_freq);
    v->dds = dds_st[ad9361_phy->id_no];
    return 0;
}

static void restore_dds(const Target *v)
{
    unsigned c;
    for (c = 0; c < 8; ++c) {
        dds_set_frequency(ad9361_phy, c, v->dds.cached_freq[c]);
        dds_set_phase(ad9361_phy, c, v->dds.cached_phase[c]);
        dds_set_scale(ad9361_phy, c, v->dds.cached_scale[c]);
    }
}

void sdr_rf_reinitialize(void)
{
    AD9361_InitParam init = default_init_param;
    const char *stage = "stop";
    int rc;
    unsigned c, was_rf_ok = sdr_state.rf_ready;
    SdrFirState restore;
    uint8_t reference_source;
    sdr_control_service();
    if (sdr_stop_all()) {
        sdr_command_error("rf_reinit stage=stop applied=unknown", -1);
        return;
    }
    /* A reference GPIO fault must be explicitly recovered before RF can be
     * declared ready again. Preserve an already selected external reference. */
    rc = adf4002_get_external_reference(&reference_source);
    if (rc < 0) {
        sdr_state.rf_ready = sdr_state.digital_ready = 0;
        sdr_state.rf_error = rc;
        sdr_command_error(
            "rf_reinit stage=reference applied=stopped use=clock_ref_source=0_then_rf_reinit!", rc);
        return;
    }
    if (sdr_state.rf_ready) {
        if (snapshot(&saved)) {
            sdr_command_error("rf_reinit stage=snapshot applied=unchanged", -1);
            return;
        }
        saved_valid = 1;
    }
    if (saved_valid) {
        init.rx_synthesizer_frequency_hz = saved.rxlo;
        init.tx_synthesizer_frequency_hz = saved.txlo;
        /* Boot with the original valid no-FIR clock plan; restore below. */
        init.rf_rx_bandwidth_hz = saved.rxbw;
        init.rf_tx_bandwidth_hz = saved.txbw;
    }
    init.tx_attenuation_mdB = 89750;
    /* Preserve external nominal frequencies before replacing the PHY. The
     * saved rxlo/txlo remain the last internal-PLL requested frequencies. */
    sdr_external_lo_save_frequencies();
    sdr_state.rf_ready = sdr_state.digital_ready = 0;
    ad9361_remove(ad9361_phy);
    ad9361_phy = NULL;
    board_rf_reset();
    stage = "init";
    rc = ad9361_init(&ad9361_phy, &init);
    if (rc < 0) {
        goto fail;
    }
    dac_init(ad9361_phy, DATA_SEL_ZERO);
    stage = "correction_restore";
    rc = sdr_correction_restore();
    if (rc < 0) {
        goto fail;
    }
    stage = "fir_restore";
    if (sdr_fir_capture(&restore) < 0) {
        rc = -1;
        goto fail;
    }
    if (!was_rf_ok && sdr_fir_recovery(&restore)) {
        /* 涓婃澶辫触浜嬪姟淇濈暀鐨勫凡楠岃瘉 FIR/鏃堕挓蹇収銆� */
    } else if (saved_valid) {
        memcpy(restore.rx_clocks, saved.r, sizeof(restore.rx_clocks));
        memcpy(restore.tx_clocks, saved.t, sizeof(restore.tx_clocks));
        restore.rx_bandwidth = saved.rxbw;
        restore.tx_bandwidth = saved.txbw;
        restore.rx_enabled = saved.rxfir;
        restore.tx_enabled = saved.txfir;
    }
    rc = sdr_fir_validate(&restore);
    if (rc < 0) {
        goto fail;
    }
    rc = sdr_fir_program(&restore);
    if (rc < 0) {
        goto fail;
    }
    sdr_fir_save_recovery(&restore);
    /* Restore the selected LO band before user gain values: external nominal
     * frequency can use a different gain table from the saved internal PLL. */
    stage = "external_lo_restore";
    rc = sdr_external_lo_restore();
    if (rc < 0) {
        goto fail;
    }
    if (saved_valid) {
        stage = "gain";
        for (c = 0; c < 2; ++c) {
            rc = ad9361_set_rx_gain_control_mode(ad9361_phy, c, saved.gc[c]);
            if (rc < 0) {
                goto fail;
            }
            if (!saved.gc[c]) {
                rc = ad9361_set_rx_rf_gain(ad9361_phy, c, saved.gain[c]);
                if (rc < 0) {
                    goto fail;
                }
            }
        }
        stage = "dds";
        restore_dds(&saved);
    }
    stage = "gate";
    rc = sdr_set_tx_gate(0);
    if (rc < 0) {
        goto fail;
    }
    sdr_state.rf_ready = sdr_state.digital_ready = 1;
    sdr_state.rf_error = 0;
    console_print("rf_reinit=ok applied=restored tx_enable=0 usb_data_en=0,0 history=retained\n");
    return;
fail:
    sdr_state.rf_error = rc;
    sdr_fir_set_verified(0);
    sdr_set_tx_gate(0);
    console_print("ERR rf_reinit stage=%s code=%d applied=partial tx_enable=0 usb_data_en=0,0\n",
                  stage, rc);
}

void sdr_digital_self_test(void)
{
    int r, t;
    uint32_t rate;
    if (sdr_stop_all()) {
        sdr_command_error("digital_self_test stage=stop applied=unknown", -1);
        return;
    }
    if (!sdr_state.rf_ready) {
        sdr_command_error("rf_not_initialized", sdr_state.rf_error);
        return;
    }
    pps_monitor_nominal(0);
    ad9361_get_rx_sampling_freq(ad9361_phy, &rate);
    r = ad9361_digital_check(ad9361_phy, 0);
    t = ad9361_digital_check(ad9361_phy, 1);
    sdr_set_tx_gate(0);
    sdr_state.digital_ready = r == 0 && t == 0;
    console_print(
        "digital_self_test rx_prbs=%s rx_code=%d tx_loopback=%s tx_code=%d rate_Hz=%lu delay_adjusted=0 rf_test=not_executed usb_throughput=not_executed tx_enable=0 usb_data_en=0,0\n",
        r == 0 ? "pass" : "fail", r, t == 0 ? "pass" : "fail", t, (unsigned long)rate);
}

void sdr_get_usb_config(uint32_t *bits)
{
    CH32H417_IP_GetConfig(&bridge, bits);
}

void sdr_request_usb_clear(void)
{
    CH32H417_IrqRequestClear(&irq);
}
