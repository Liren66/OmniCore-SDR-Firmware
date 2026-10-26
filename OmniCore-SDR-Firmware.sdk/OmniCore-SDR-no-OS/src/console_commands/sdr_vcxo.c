#include "sdr_vcxo.h"
#include "sdr_runtime.h"
#include "sdr_reference.h"
#include "sdr_pps.h"
#include "console.h"
#include "adf4002.h"
#include "pps_monitor.h"
#include "xtime_l.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Zynq -> CH32 (main loop only, outside any RSP envelope):
 *   VCXO req=<n> mode=<m> seq=<n> epoch=<n> new=<0|1> valid=<0|1> cnt=<n> exp=<n> expf=<n>
 * new=1 lines carry one PPS edge (mode 2 only); new=0 lines are mode heartbeats.
 * CH32 -> Zynq: vcxo_ack req= mode= pa4= state= dac= k= err= phase= n=
 * (k and err in milli-ppb, phase in ns). See OmniCore-SDR-Bridge vcxo_discipline.c. */
#define HEARTBEAT_US 1000000ULL
#define PENDING_RESEND_US 250000ULL
#define ACK_TIMEOUT_US 3000000ULL
#define BRIDGE_ALIVE_US 3000000ULL
#define LOCK_PHASE_NS 1000

static struct {
    unsigned ref, pps, pending, announce, confirmed, ack_valid;
    uint32_t req;
    int error;
    uint64_t req_us, sent_us, ack_us;
    long long ack_req, ack_mode, ack_pa4, ack_state, ack_dac, ack_k, ack_err, ack_phase, ack_n;
} vx;

static uint64_t now_us(void)
{
    XTime time;
    XTime_GetTime(&time);
    return ((uint64_t)time / COUNTS_PER_SECOND) * 1000000U +
           ((uint64_t)time % COUNTS_PER_SECOND) * 1000000U / COUNTS_PER_SECOND;
}

/* 0 when the charge pump is high-Z or the ADF4002 state is unknown. */
static unsigned pll_on(void)
{
    uint8_t source = 0;
    return adf4002_get_external_reference(&source) == 0 && source;
}

unsigned sdr_vcxo_requested_ref(void) { return vx.ref; }
unsigned sdr_vcxo_requested_pps(void) { return vx.pps; }
unsigned sdr_vcxo_requested_mode(void) { return vx.ref + 2U * vx.pps; }
unsigned sdr_vcxo_pending(void) { return vx.pending; }

int sdr_vcxo_request(unsigned ref, unsigned pps)
{
    uint8_t source = 0;
    int rc;
    if (ref > 1U || pps > 1U) {
        return sdr_command_error("vcxo_invalid_mode", -1);
    }
    vx.ref = ref;
    vx.pps = pps;
    vx.error = 0;
    vx.pending = vx.confirmed = 0;
    if (++vx.req == 0U) {
        ++vx.req;
    }
    vx.req_us = now_us();
    vx.announce = 1;
    if (!ref) {
        /* Releasing the node never waits. A faulted ADF4002 is also rewritten:
         * this is the documented clock_ref_source=0 recovery path. */
        if (adf4002_get_external_reference(&source) < 0 || source) {
            rc = sdr_reference_apply(0);
            if (rc < 0) {
                vx.error = rc;
                return sdr_command_error(
                    "adf4002_apply reference=unknown recover=clock_ref_source=0_then_rf_reinit!", rc);
            }
        }
    } else if (!pll_on()) {
        vx.pending = 1; /* completed by sdr_vcxo_service() after pa4=0 */
    }
    return 0;
}

static int ack_field(const char *line, const char *key, long long *value)
{
    char pattern[16];
    const char *p;
    char *end;
    snprintf(pattern, sizeof(pattern), " %s=", key);
    p = strstr(line, pattern);
    if (!p) {
        return -1;
    }
    p += strlen(pattern);
    *value = strtoll(p, &end, 10);
    return end == p || (*end && *end != ' ') ? -1 : 0;
}

int sdr_vcxo_execute(const char *line)
{
    long long v[9];
    static const char *keys[] = {"req", "mode", "pa4", "state", "dac", "k", "err", "phase", "n"};
    unsigned i;
    if (strncmp(line, "vcxo_ack ", 9)) {
        return 0;
    }
    for (i = 0; i < 9U; ++i) {
        if (ack_field(line, keys[i], &v[i]) < 0) {
            return 1; /* malformed bridge line: ignore silently */
        }
    }
    vx.ack_req = v[0];
    vx.ack_mode = v[1];
    vx.ack_pa4 = v[2];
    vx.ack_state = v[3];
    vx.ack_dac = v[4];
    vx.ack_k = v[5];
    vx.ack_err = v[6];
    vx.ack_phase = v[7];
    vx.ack_n = v[8];
    vx.ack_us = now_us();
    vx.ack_valid = 1;
    if (vx.ack_req == (long long)vx.req && vx.ack_mode == (long long)sdr_vcxo_requested_mode() &&
        vx.ack_pa4 == 0) {
        vx.confirmed = 1;
    }
    return 1;
}

void sdr_vcxo_service(void)
{
    PpsSample s;
    unsigned mode = sdr_vcxo_requested_mode();
    uint64_t now = now_us(), expected;
    int fresh = pps_monitor_take_sample(&s), rc;
    if (vx.pending) {
        if (vx.confirmed) {
            vx.pending = 0;
            rc = sdr_reference_apply(1);
            if (rc < 0) {
                vx.error = rc;
            }
        } else if (now - vx.req_us > ACK_TIMEOUT_US) {
            vx.pending = 0; /* charge pump stays high-Z */
            vx.error = -ETIMEDOUT;
        }
    }
    expected = sdr_pps_expected_q16();
    if (mode == SDR_VCXO_PPS && fresh && expected) {
        console_print(
            "VCXO req=%lu mode=%u seq=%lu epoch=%lu new=1 valid=%lu cnt=%lu exp=%lu expf=%lu\n",
            (unsigned long)vx.req, mode, (unsigned long)s.seq, (unsigned long)s.epoch,
            (unsigned long)s.valid, (unsigned long)s.count, (unsigned long)(expected >> 16),
            (unsigned long)(expected & 0xFFFFU));
    } else if (vx.announce ||
               now - vx.sent_us >= (vx.pending ? PENDING_RESEND_US : HEARTBEAT_US)) {
        console_print("VCXO req=%lu mode=%u new=0 valid=0\n", (unsigned long)vx.req, mode);
    } else {
        return;
    }
    vx.sent_us = now;
    vx.announce = 0;
}

void sdr_vcxo_status(void)
{
    static const char *states[] = {"off", "cal", "acq", "track", "hold"};
    unsigned mode = sdr_vcxo_requested_mode();
    uint64_t age = vx.ack_valid ? now_us() - vx.ack_us : UINT64_MAX;
    unsigned bridge = vx.ack_valid && age <= BRIDGE_ALIVE_US;
    int locked = -1;
    if (mode == SDR_VCXO_PPS) {
        locked = bridge && vx.ack_state == 3 && llabs(vx.ack_phase) <= LOCK_PHASE_NS;
    }
    console_print(
        "vcxo_status requested_mode=%u applied_mode=%u pending=%u error=%d pll=%u bridge=%u ack_age_ms=%lu pa4=%d state=%s dac=%ld k_mppb=%ld err_mppb=%ld phase_ns=%ld samples=%lu locked=%d req=%lu\n",
        mode, pll_on() + 2U * vx.pps, vx.pending, vx.error, pll_on(), bridge,
        (unsigned long)(age / 1000U > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : age / 1000U),
        bridge ? (int)vx.ack_pa4 : -1,
        bridge && vx.ack_state >= 0 && vx.ack_state <= 4 ? states[vx.ack_state] : "unknown",
        bridge ? (long)vx.ack_dac : -1L, bridge ? (long)vx.ack_k : 0L,
        bridge ? (long)vx.ack_err : 0L, bridge ? (long)vx.ack_phase : 0L,
        bridge ? (unsigned long)vx.ack_n : 0UL, locked, (unsigned long)vx.req);
}
