/* Multi-board synchronisation commands (OC_SYNC_AXI 1.0).
 *
 * sync_status?                  everything below in one line
 * ref_out=0|1                   10 MHz on REF_OUT (J13) for the next board's 10M_EXT
 * trig_out=0|1                  TRIG driver on J9 (0 = held low)
 * trig_edge=rising|falling      launch edge relative to the 40 MHz reference
 * trig_width=<cycles>           pulse width in 40 MHz cycles (1..65535, default 80 = 2 us)
 * trig_period=<cycles>          periodic TRIG, 0 = off, else >= 1000 (40000000 = 1 Hz)
 * trig_pulse!                   one TRIG pulse now
 * trig_at_pps!  / trig_disarm!  one TRIG pulse at the next PPS_EXT rising edge
 * sync_loopback=0|1             SYNC counter / time-base SYNC use the internal TRIG
 * sync_clear!                   clear SYNC / PPS / TRIG counters
 * mcs_step=0..5                 one AD9361 multi-chip-sync step (no SYNC pulse)
 * mcs_run!                      steps 0..5 with this board's TRIG at steps 2 and 4
 *
 * Intervals are counted in this board's 40 MHz cycles. With a common
 * reference (daisy-chained REF_OUT or a shared 10 MHz) every board measures
 * exactly the generator's period (+-1), which is the lock check: the ADF4002
 * lock detect only drives LED D5 and cannot be read.
 */
#include "sdr_sync.h"
#include "sdr_time.h"
#include "sdr_runtime.h"
#include "console.h"
#include "ad9361_api.h"
#include "ad9361.h"
#include "board_config.h"
#include "ch32h417_irq.h"
#include "xparameters.h"
#include "xil_io.h"
#include "util.h"
#include "platform.h"
#include <string.h>

#ifdef XPAR_OC_SYNC_AXI_0_BASEADDR
#define SYNC_BASE XPAR_OC_SYNC_AXI_0_BASEADDR
#else
#define SYNC_BASE 0U   /* bitstream without OC_SYNC_AXI: feature absent */
#endif

#define R_ID 0x00U
#define R_VERSION 0x04U
#define R_CTRL 0x08U
#define R_WIDTH 0x0CU
#define R_PERIOD 0x10U
#define R_CMD 0x14U
#define R_STATUS 0x18U
#define R_REF40_HZ 0x1CU
#define R_SYNC_COUNT 0x20U
#define R_SYNC_INTERVAL 0x24U
#define R_PPS_COUNT 0x28U
#define R_PPS_INTERVAL 0x2CU
#define R_TRIG_COUNT 0x30U

#define C_REF 1U
#define C_TRIG 2U
#define C_FALLING 4U
#define C_PERIODIC 8U
#define C_PPS_SYNC 16U
#define C_LOOP 32U

#define K_FIRE 1U
#define K_ARM 2U
#define K_DISARM 4U
#define K_CLEAR 8U

#define S_BUSY 1U
#define S_REF40 2U
#define S_ARMED 4U

#define SYNC_ID 0x4F435359U
#define EVT_GAP_US 250000U

static unsigned present;
static uint32_t ctrl, width = 80, period;   /* period 0 = periodic off */
static int mcs_last = -1;
static unsigned mcs_done;
static uint32_t ensm_saved;
static uint32_t evt_count, evt_seq;
static uint64_t evt_next;

static uint32_t rd(uint32_t off) { return Xil_In32(SYNC_BASE + off); }
static void wr(uint32_t off, uint32_t v) { Xil_Out32(SYNC_BASE + off, v); }

/* Waits until the 40 MHz domain has applied the last write. */
static int wait_applied(void)
{
    uint64_t end = CH32H417_IrqNowUs() + 20000U;
    while (rd(R_STATUS) & S_BUSY)
        if (CH32H417_IrqNowUs() > end)
            return -1;
    return 0;
}

static int apply_ctrl(uint32_t v)
{
    ctrl = v;
    wr(R_CTRL, ctrl);
    return wait_applied();
}

static int command(uint32_t k)
{
    wr(R_CMD, k);
    return wait_applied();
}

void sdr_sync_init(void)
{
    present = SYNC_BASE != 0U && rd(R_ID) == SYNC_ID;
    ctrl = 0;
    width = 80;
    period = 0;
    mcs_last = -1;
    mcs_done = 0;
    evt_count = 0;
    if (!present)
        return;
    wr(R_WIDTH, width);
    wr(R_PERIOD, 40000000U);
    (void)apply_ctrl(0);   /* REF_OUT and TRIG low, time base PPS = PPS_EXT */
    (void)command(K_DISARM | K_CLEAR);
}

unsigned sdr_sync_capable(void)
{
    return present;
}

int sdr_sync_select_pps(unsigned sync)
{
    if (!present)
        return sync ? -1 : 0;
    return apply_ctrl(sync ? (ctrl | C_PPS_SYNC) : (ctrl & ~C_PPS_SYNC));
}

unsigned sdr_sync_pps_is_sync(void)
{
    return present && (ctrl & C_PPS_SYNC) != 0U;
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t')
        ++p;
    return p;
}

static int at_end(const char *p)
{
    p = skip_ws(p);
    return *p == 0 || *p == '\r' || *p == '\n';
}

/* Unsigned 32-bit decimal; returns 0 on success. */
static int parse_u32(const char *p, uint32_t *v)
{
    uint64_t x = 0;
    const char *s;
    p = skip_ws(p);
    s = p;
    while (*p >= '0' && *p <= '9') {
        x = x * 10U + (uint64_t)(*p - '0');
        if (x > 0xFFFFFFFFULL)
            return -1;
        ++p;
    }
    if (p == s || !at_end(p))
        return -1;
    *v = (uint32_t)x;
    return 0;
}

static int print_status(void)
{
    uint32_t s = rd(R_STATUS);
    console_print(
        "sync_status capable=1 version=0x%08lx ref40_present=%u ref40_hz=%lu ref_out=%u trig_out=%u trig_edge=%s trig_width=%lu trig_period=%lu armed=%u loopback=%u time_pps=%s sync_count=%lu sync_interval=%lu pps_count=%lu pps_interval=%lu trig_count=%lu mcs_step=%d mcs_done=%u\n",
        (unsigned long)rd(R_VERSION), (unsigned)((s & S_REF40) != 0U), (unsigned long)rd(R_REF40_HZ),
        (unsigned)((ctrl & C_REF) != 0U), (unsigned)((ctrl & C_TRIG) != 0U),
        (ctrl & C_FALLING) ? "falling" : "rising", (unsigned long)width, (unsigned long)period,
        (unsigned)((s & S_ARMED) != 0U), (unsigned)((ctrl & C_LOOP) != 0U),
        (ctrl & C_PPS_SYNC) ? "sync" : "pps_ext", (unsigned long)rd(R_SYNC_COUNT),
        (unsigned long)rd(R_SYNC_INTERVAL), (unsigned long)rd(R_PPS_COUNT),
        (unsigned long)rd(R_PPS_INTERVAL), (unsigned long)rd(R_TRIG_COUNT), mcs_last, mcs_done);
    return 0;
}

static int need_present(void)
{
    if (!present)
        return sdr_command_error("sync_unsupported_fpga", -1);
    return 0;
}

static int need_ref40(void)
{
    if (!(rd(R_STATUS) & S_REF40))
        return sdr_command_error("ref40_absent CLK_FB_FPGA", -1);
    return 0;
}

static int set_flag(const char *arg, uint32_t bit, const char *name)
{
    uint32_t v;
    if (parse_u32(arg, &v) || v > 1)
        return sdr_command_error("invalid_parameters", -1);
    if (need_ref40())
        return -1;
    if (apply_ctrl(v ? (ctrl | bit) : (ctrl & ~bit)))
        return sdr_command_error("sync_apply_timeout", -1);
    console_print("%s=%lu\n", name, (unsigned long)v);
    return 0;
}

static int streams_idle(void)
{
    if (sdr_state.usb_rx || sdr_state.usb_tx || sdr_state.tx_enabled)
        return sdr_command_error("busy stop_streams_before_mcs", -1);
    if (!ad9361_phy || !sdr_state.rf_ready)
        return sdr_command_error("rf_not_ready", -1);
    return 0;
}

/* One MCS step. Step 0 saves the ENSM mode and parks both directions in
 * ALERT (as ad9361_do_mcs does); step 5 restores it. The SYNC_IN pulses of
 * steps 2 and 4 come from TRIG (this board's or the master's), not here. */
static int mcs_step(unsigned step)
{
    if (step == 0) {
        ad9361_get_en_state_machine_mode(ad9361_phy, &ensm_saved);
        ad9361_set_en_state_machine_mode(ad9361_phy, ENSM_MODE_ALERT);
        mcs_done = 0;
    } else if (mcs_last < 0) {
        return sdr_command_error("mcs_step_0_first", -1);
    }
    ad9361_mcs(ad9361_phy, (int32_t)step);
    mcs_last = (int)step;
    if (step == 5) {
        ad9361_set_en_state_machine_mode(ad9361_phy, ensm_saved);
        mcs_done = 1;
        mcs_last = -1;
    }
    return 0;
}

static int fire_and_wait(void)
{
    uint32_t before = rd(R_TRIG_COUNT);
    uint64_t end;
    if (command(K_FIRE))
        return -1;
    end = CH32H417_IrqNowUs() + 10000U;
    while (rd(R_TRIG_COUNT) == before)
        if (CH32H417_IrqNowUs() > end)
            return -1;
    return 0;
}

int sdr_sync_execute(const char *line)
{
    const char *p;
    uint32_t v;
#define IS(cmd) (!strncmp(line, cmd, sizeof(cmd) - 1) && (p = line + sizeof(cmd) - 1))
    if (strncmp(line, "sync_", 5) && strncmp(line, "trig_", 5) && strncmp(line, "ref_out", 7) &&
        strncmp(line, "mcs_", 4))
        return 0;
    if (IS("sync_status?")) {
        if (!present) {
            console_print("sync_status capable=0\n");
            return 1;
        }
        return print_status() ? -1 : 1;
    }
    if (need_present())
        return -1;
    if (IS("ref_out?")) {
        console_print("ref_out=%u\n", (unsigned)((ctrl & C_REF) != 0U));
        return 1;
    }
    if (IS("ref_out="))
        return set_flag(p, C_REF, "ref_out") ? -1 : 1;
    if (IS("trig_out?")) {
        console_print("trig_out=%u\n", (unsigned)((ctrl & C_TRIG) != 0U));
        return 1;
    }
    if (IS("trig_out="))
        return set_flag(p, C_TRIG, "trig_out") ? -1 : 1;
    if (IS("sync_loopback="))
        return set_flag(p, C_LOOP, "sync_loopback") ? -1 : 1;
    if (IS("trig_edge=")) {
        p = skip_ws(p);
        if (!strncmp(p, "rising", 6) || (p[0] == '0' && at_end(p + 1))) v = 0;
        else if (!strncmp(p, "falling", 7) || (p[0] == '1' && at_end(p + 1))) v = 1;
        else return sdr_command_error("trig_edge_requires rising|falling", -1);
        if (need_ref40() || apply_ctrl(v ? (ctrl | C_FALLING) : (ctrl & ~C_FALLING)))
            return -1;
        console_print("trig_edge=%s\n", v ? "falling" : "rising");
        return 1;
    }
    if (IS("trig_width=")) {
        if (parse_u32(p, &v) || v < 1 || v > 65535)
            return sdr_command_error("trig_width_range 1..65535", -1);
        if (period && v >= period)
            return sdr_command_error("trig_width_must_be_below_trig_period", -1);
        width = v;
        wr(R_WIDTH, width);
        if (need_ref40() || wait_applied())
            return -1;
        console_print("trig_width=%lu\n", (unsigned long)width);
        return 1;
    }
    if (IS("trig_period=")) {
        if (parse_u32(p, &v) || (v && (v < 1000U || v <= width)))
            return sdr_command_error("trig_period_requires 0|>=1000_and_>trig_width", -1);
        period = v;
        if (v)
            wr(R_PERIOD, v);
        if (need_ref40() || apply_ctrl(v ? (ctrl | C_PERIODIC) : (ctrl & ~C_PERIODIC)))
            return -1;
        console_print("trig_period=%lu\n", (unsigned long)period);
        return 1;
    }
    if (IS("trig_pulse!")) {
        if (!(ctrl & C_TRIG))
            return sdr_command_error("trig_out_disabled", -1);
        if (need_ref40() || fire_and_wait())
            return sdr_command_error("trig_timeout", -1);
        console_print("trig_pulse=1 trig_count=%lu\n", (unsigned long)rd(R_TRIG_COUNT));
        return 1;
    }
    if (IS("trig_at_pps!")) {
        if (!(ctrl & C_TRIG))
            return sdr_command_error("trig_out_disabled", -1);
        if (need_ref40() || command(K_ARM))
            return -1;
        console_print("trig_at_pps=armed\n");
        return 1;
    }
    if (IS("trig_disarm!")) {
        if (command(K_DISARM))
            return sdr_command_error("sync_apply_timeout", -1);
        console_print("trig_at_pps=disarmed\n");
        return 1;
    }
    if (IS("sync_clear!")) {
        if (need_ref40() || command(K_CLEAR))
            return -1;
        console_print("sync_clear=1\n");
        return 1;
    }
    if (IS("mcs_step=")) {
        if (parse_u32(p, &v) || v > 5)
            return sdr_command_error("mcs_step_range 0..5", -1);
        if (streams_idle() || mcs_step(v))
            return -1;
        console_print("mcs_step=%lu%s\n", (unsigned long)v,
                      v == 2 || v == 4 ? " pulse=trig_required" : v == 5 ? " done=1" : "");
        return 1;
    }
    if (IS("mcs_run!")) {
        unsigned s;
        if (!(ctrl & C_TRIG))
            return sdr_command_error("trig_out_disabled", -1);
        if (streams_idle() || need_ref40())
            return -1;
        for (s = 0; s <= 5; ++s) {
            if (mcs_step(s))
                return -1;
            if ((s == 2 || s == 4) && fire_and_wait()) {
                (void)mcs_step(5);
                return sdr_command_error("trig_timeout", -1);
            }
            mdelay(100);
        }
        console_print("mcs_run=done pulses=2 note=sync_pulse_reaches_ad9361_only_via_J9_to_J1_cable\n");
        return 1;
    }
#undef IS
    return 0;
}

void sdr_sync_service(void)
{
    uint32_t n;
    uint64_t now;
    if (!present)
        return;
    n = rd(R_SYNC_COUNT);
    if (n < evt_count)
        evt_count = 0;   /* counters were cleared */
    if (n == evt_count)
        return;
    now = CH32H417_IrqNowUs();
    if (now < evt_next)
        return;
    console_print("EVT tseq=%lu type=sync_in src=sync count=%lu new=%lu interval=%lu\n",
                  (unsigned long)++evt_seq, (unsigned long)n, (unsigned long)(n - evt_count),
                  (unsigned long)rd(R_SYNC_INTERVAL));
    evt_count = n;
    evt_next = now + EVT_GAP_US;
}

void sdr_sync_help(void)
{
    console_print(
        "Sync: sync_status?, ref_out=0|1, trig_out=0|1, trig_edge=rising|falling, trig_width=<cycles>, trig_period=0|<cycles>, trig_pulse!, trig_at_pps!, trig_disarm!, sync_loopback=0|1, sync_clear!, mcs_step=0..5, mcs_run!; time_source=sync; cycles of the 40 MHz reference.\n");
}

void sdr_sync_print_capabilities(void)
{
    console_print("sync_capabilities sync=%u ref_out=%u trig=%u mcs=%u time_source_sync=%u version=0x%08lx\n",
                  present, present, present, present, present,
                  (unsigned long)(present ? rd(R_VERSION) : 0U));
}
