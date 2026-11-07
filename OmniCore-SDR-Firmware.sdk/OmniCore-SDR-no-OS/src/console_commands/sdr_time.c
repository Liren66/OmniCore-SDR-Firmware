/* Hardware time base, PPS time loading, framed USB transport and timed RX.
 *
 * time_now? / time_now=<ticks>        read / load the sample counter now
 * time_next_pps=<ticks>               load at the next PPS edge of the source
 * time_last_pps?                      time of the first sample after the last PPS
 * time_source? / =none|external|internal (0/1/2; internal = AD9361 CLK_OUT / 40e6)
 * time_status? / time_invalidate!
 * USB transport is always framed: 16-byte header + 16368-byte payload per 16 KiB block.
 * rx_timed? / =0|1                    RX waits for rx_stream= (requires framing)
 * rx_stream=now|<ticks> <nsamp>       start (nsamp 0 = continuous); rx_stream_stop!
 *
 * FPGA timing events are printed by sdr_time_service() as
 *   EVT tseq=<n> type=<name> src=fpga_time block=<seq> time=<ticks> lost_before=<0|1>
 */
#include "sdr_time.h"
#include "sdr_runtime.h"
#include "console.h"
#include "ad9361_api.h"
#include "ad9361.h"
#include "parameters.h"
#include "platform.h"
#include "ch32h417_irq.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static CH32H417_IP *tb;
static unsigned rx_timed, source;
static uint32_t valid_rate, event_seq, events_total, events_lost;
static uint64_t next_rate_check;

static const char *source_name(unsigned s)
{
    return s == CH32H417_IP_PPS_EXTERNAL ? "external" :
           s == CH32H417_IP_PPS_INTERNAL ? "internal" : "none";
}

/* newlib-nano printf has no %llu; format 64-bit values by hand. */
static const char *u64s(uint64_t v, char *buf)
{
    char tmp[21];
    unsigned n = 0, i;
    do {
        tmp[n++] = (char)('0' + (unsigned)(v % 10U));
        v /= 10U;
    } while (v && n < sizeof(tmp));
    for (i = 0; i < n; ++i)
        buf[i] = tmp[n - 1 - i];
    buf[n] = 0;
    return buf;
}

static uint32_t current_rate(void)
{
    uint32_t rate = 0;
    if (!ad9361_phy || ad9361_get_rx_sampling_freq(ad9361_phy, &rate) < 0)
        return 0;
    return rate;
}

void sdr_time_init(CH32H417_IP *device)
{
    tb = device;
    rx_timed = 0;
    source = CH32H417_IP_PPS_NONE;
    valid_rate = 0;
    if (sdr_time_capable())
        (void)CH32H417_IP_TimeCommand(tb, CH32H417_IP_TIME_INVALIDATE, source, 0);
}

unsigned sdr_time_capable(void)
{
    return tb && (CH32H417_IP_mReadReg(tb->BaseAddress, CH32H417_IP_STATUS0) &
                  CH32H417_IP_TIME_CAPABLE) != 0;
}

unsigned sdr_time_mode_bits(void)
{
    return rx_timed ? CH32H417_IP_RX_TIMED : 0U;
}

static int need_capable(void)
{
    if (!sdr_time_capable())
        return sdr_command_error("hw_time_unsupported_fpga", -1);
    return 0;
}

static int print_status(void)
{
    CH32H417_IP_Time t;
    char a[21], b[21];
    if (need_capable() || CH32H417_IP_GetTime(tb, &t))
        return need_capable() ? -1 : sdr_command_error("time_read", -1);
    console_print(
        "time_status valid=%u armed=%u source=%s ref40_present=%u radio_clock_stalled=%u now=%s last_pps=%s pps_count=%lu tick_rate=%lu framing=%u rx_timed=%u version=0x%08lx header_bytes=16 payload_bytes=16368\n",
        (unsigned)((t.Flags >> 1) & 1U), (unsigned)(t.Flags & 1U),
        source_name((t.Flags >> 2) & 3U), (unsigned)((t.Flags >> 4) & 1U),
        (unsigned)((t.Flags >> 5) & 1U), u64s(t.Now, a), u64s(t.LastPps, b),
        (unsigned long)t.PpsCount, (unsigned long)current_rate(), 1U, rx_timed,
        (unsigned long)CH32H417_IP_Version(tb));
    return 0;
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t')
        ++p;
    return p;
}

/* Parses one unsigned 64-bit decimal argument; returns the end or NULL. */
static const char *parse_u64(const char *p, uint64_t *v)
{
    uint64_t x = 0;
    const char *start;
    p = skip_ws(p);
    start = p;
    while (*p >= '0' && *p <= '9') {
        uint64_t d = (uint64_t)(*p - '0');
        if (x > (UINT64_MAX - d) / 10U)
            return 0;
        x = x * 10U + d;
        ++p;
    }
    if (p == start)
        return 0;
    *v = x;
    return p;
}

static int at_end(const char *p)
{
    p = skip_ws(p);
    return *p == 0 || *p == '\r' || *p == '\n';
}

static int time_load(unsigned code, uint64_t value)
{
    char a[21];
    int rc;
    if (code == CH32H417_IP_TIME_NEXT_PPS && source == CH32H417_IP_PPS_NONE)
        return sdr_command_error("time_next_pps_requires time_source=external|internal", -1);
    rc = CH32H417_IP_TimeCommand(tb, code, source, value);
    if (rc)
        return sdr_command_error("time_command_timeout radio_clock_stopped?", rc);
    valid_rate = current_rate();
    if (code == CH32H417_IP_TIME_NOW)
        console_print("time_now=%s applied=now tick_rate=%lu\n", u64s(value, a),
                      (unsigned long)valid_rate);
    else
        console_print("time_next_pps=%s armed=1 source=%s tick_rate=%lu\n", u64s(value, a),
                      source_name(source), (unsigned long)valid_rate);
    return 0;
}

static int cmd_flag(const char *arg, unsigned *flag, const char *name)
{
    uint64_t v;
    const char *e;
    if (!arg) {
        console_print("%s=%u\n", name, *flag);
        return 0;
    }
    e = parse_u64(arg, &v);
    if (!e || !at_end(e) || v > 1)
        return sdr_command_error("invalid_parameters", -1);
    *flag = (unsigned)v;
    return 0;
}

int sdr_time_execute(const char *line)
{
    const char *p;
    uint64_t v, n;
    char a[21];
    CH32H417_IP_Time t;

#define IS(cmd) (!strncmp(line, cmd, sizeof(cmd) - 1) && (p = line + sizeof(cmd) - 1))
    if (IS("time_status?"))
        return print_status() ? -1 : 1;
    if (IS("time_now?")) {
        if (need_capable() || CH32H417_IP_GetTime(tb, &t))
            return -1;
        console_print("time_now=%s valid=%u tick_rate=%lu\n", u64s(t.Now, a),
                      (unsigned)((t.Flags >> 1) & 1U), (unsigned long)current_rate());
        return 1;
    }
    if (IS("time_last_pps?")) {
        if (need_capable() || CH32H417_IP_GetTime(tb, &t))
            return -1;
        console_print("time_last_pps=%s pps_count=%lu source=%s\n", u64s(t.LastPps, a),
                      (unsigned long)t.PpsCount, source_name((t.Flags >> 2) & 3U));
        return 1;
    }
    if (IS("time_now=") || IS("time_next_pps=")) {
        unsigned code = line[5] == 'n' && line[6] == 'o' ? CH32H417_IP_TIME_NOW
                                                         : CH32H417_IP_TIME_NEXT_PPS;
        const char *e;
        if (need_capable())
            return -1;
        e = parse_u64(p, &v);
        if (!e || !at_end(e))
            return sdr_command_error("invalid_parameters", -1);
        return time_load(code, v) ? -1 : 1;
    }
    if (IS("time_invalidate!")) {
        if (need_capable())
            return -1;
        if (CH32H417_IP_TimeCommand(tb, CH32H417_IP_TIME_INVALIDATE, source, 0))
            return sdr_command_error("time_command_timeout", -1);
        valid_rate = 0;
        console_print("time_invalidate=1\n");
        return 1;
    }
    if (IS("time_source?")) {
        console_print("time_source=%s\n", source_name(source));
        return 1;
    }
    if (IS("time_source=")) {
        unsigned s;
        p = skip_ws(p);
        if (!strncmp(p, "none", 4) || !strncmp(p, "0", 1)) s = CH32H417_IP_PPS_NONE;
        else if (!strncmp(p, "external", 8) || !strncmp(p, "1", 1)) s = CH32H417_IP_PPS_EXTERNAL;
        else if (!strncmp(p, "internal", 8) || !strncmp(p, "2", 1)) s = CH32H417_IP_PPS_INTERNAL;
        else return sdr_command_error("time_source_requires none|external|internal", -1);
        if (need_capable())
            return -1;
        if (CH32H417_IP_TimeCommand(tb, 0, s, 0))
            return sdr_command_error("time_command_timeout", -1);
        source = s;
        console_print("time_source=%s\n", source_name(source));
        return 1;
    }
    if (IS("rx_timed?")) {
        console_print("rx_timed=%u\n", rx_timed);
        return 1;
    }
    if (IS("rx_timed=")) {
        unsigned want = rx_timed;
        if (cmd_flag(p, &want, "rx_timed"))
            return -1;
        if (want != rx_timed && sdr_state.usb_rx)
            return sdr_command_error("busy stop_rx_before_rx_timed_change", -1);
        rx_timed = want;
        console_print("rx_timed=%u\n", rx_timed);
        return 1;
    }
    if (IS("rx_stream_stop!")) {
        if (need_capable())
            return -1;
        if (CH32H417_IP_RxCommand(tb, CH32H417_IP_RX_CMD_STOP, 0, 0))
            return sdr_command_error("rx_command_timeout", -1);
        console_print("rx_stream=stop\n");
        return 1;
    }
    if (IS("rx_stream=")) {
        const char *e;
        unsigned now = 0;
        if (need_capable())
            return -1;
        if (!rx_timed)
            return sdr_command_error("rx_stream_requires rx_timed=1", -1);
        p = skip_ws(p);
        if (!strncmp(p, "now", 3)) {
            now = 1;
            v = 0;
            e = p + 3;
        } else {
            e = parse_u64(p, &v);
        }
        if (!e || !(e = parse_u64(e, &n)) || !at_end(e) || n > 0xFFFFFFFFULL)
            return sdr_command_error("invalid_parameters use=rx_stream=now|<ticks> <nsamp>", -1);
        if (CH32H417_IP_RxCommand(tb, now ? CH32H417_IP_RX_CMD_NOW : 0, v, (u32)n))
            return sdr_command_error("rx_command_timeout", -1);
        console_print("rx_stream=%s nsamp=%lu\n", now ? "now" : u64s(v, a), (unsigned long)n);
        return 1;
    }
    /* AD9361 data-port loopback (TX words -> RX words) for timing tests. */
    if (IS("bist_loopback?")) {
        int32_t mode = 0;
        if (!ad9361_phy)
            return sdr_command_error("rf_not_ready", -1);
        ad9361_get_bist_loopback(ad9361_phy, &mode);
        console_print("bist_loopback=%ld\n", (long)mode);
        return 1;
    }
    if (IS("bist_loopback=")) {
        uint64_t m;
        const char *e = parse_u64(p, &m);
        if (!e || !at_end(e) || m > 1)
            return sdr_command_error("bist_loopback_requires 0|1", -1);
        if (!ad9361_phy || ad9361_bist_loopback(ad9361_phy, (int32_t)m) < 0)
            return sdr_command_error("bist_loopback", -1);
        console_print("bist_loopback=%lu path=ad9361_data_port_tx_to_rx\n", (unsigned long)m);
        return 1;
    }
#undef IS
    return 0;
}

static const char *event_name(u32 code)
{
    switch (code) {
    case CH32H417_IP_EV_TX_ACK: return "tx_burst_ack";
    case CH32H417_IP_EV_TX_UNDERFLOW: return "tx_underflow";
    case CH32H417_IP_EV_TX_LATE: return "tx_late";
    case CH32H417_IP_EV_TX_SEQ: return "tx_seq_error";
    case CH32H417_IP_EV_TX_HEADER: return "tx_header_error";
    case CH32H417_IP_EV_RX_OVERFLOW: return "rx_overflow_gap";
    case CH32H417_IP_EV_RX_LATE: return "rx_late_command";
    case CH32H417_IP_EV_RX_CMD_IGNORED: return "rx_command_ignored";
    default: return "unknown";
    }
}

void sdr_time_service(void)
{
    CH32H417_IP_Event e;
    unsigned k;
    char a[21];
    uint64_t now;
    if (!tb || !sdr_state.bridge_ready || !sdr_time_capable())
        return;
    for (k = 0; k < 4 && CH32H417_IP_PopEvent(tb, &e) == 1; ++k) {
        ++events_total;
        events_lost += e.Lost;
        console_print("EVT tseq=%lu type=%s src=fpga_time code=%lu block=%lu time=%s lost_before=%lu\n",
                      (unsigned long)++event_seq, event_name(e.Code), (unsigned long)e.Code,
                      (unsigned long)e.Seq, u64s(e.Time, a), (unsigned long)e.Lost);
    }
    /* A sample-rate change re-times the counter; the old epoch is invalid. */
    now = CH32H417_IrqNowUs();
    if (valid_rate && now >= next_rate_check) {
        uint32_t rate = current_rate();
        next_rate_check = now + 250000U;
        if (rate && rate != valid_rate) {
            (void)CH32H417_IP_TimeCommand(tb, CH32H417_IP_TIME_INVALIDATE, source, 0);
            console_print("EVT tseq=%lu type=time_invalidated src=sdk old_rate=%lu new_rate=%lu\n",
                          (unsigned long)++event_seq, (unsigned long)valid_rate,
                          (unsigned long)rate);
            valid_rate = 0;
        }
    }
}

void sdr_time_help(void)
{
    console_print(
        "Time: time_now?/time_now=<ticks>, time_next_pps=<ticks>, time_last_pps?, time_source=none|external|internal, time_status?, time_invalidate!; 1 tick = 1 sample.\n");
    console_print(
        "Framed USB (16 B header + 16368 B payload per 16 KiB block, always on): rx_timed=0|1, rx_stream=now|<ticks> <nsamp>, rx_stream_stop!; EVT src=fpga_time reports TX ack/late/underflow and RX gaps with sample time.\n");
}
