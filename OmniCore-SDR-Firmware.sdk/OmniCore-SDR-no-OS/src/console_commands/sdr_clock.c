#include "sdr_clock.h"
#include "console.h"
#include <string.h>

/* Staging does not touch hardware. A matching,
 * complete transaction is consumed before invoking the checked RF apply. */
typedef enum {
    CLOCK_NONE = 0,
    CLOCK_BEGIN,
    CLOCK_RX,
    CLOCK_TX,
    CLOCK_COMMIT,
    CLOCK_ABORT,
    CLOCK_SAMPLE_RATE
} ClockOperation;

static struct {
    uint32_t token, rx[6], tx[6];
    unsigned present;
} clock_upload;

static int clock_parse_u32(const char *p, uint32_t *v, unsigned wanted)
{
    unsigned n;
    for (n = 0; n < wanted; ++n) {
        uint32_t number = 0;
        unsigned digits = 0;
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        while (*p >= '0' && *p <= '9') {
            uint32_t d = (uint32_t)(*p++ - '0');
            if (number > (UINT32_MAX - d) / 10U) {
                return -1;
            }
            number = number * 10U + d;
            ++digits;
        }
        if (!digits) {
            return -1;
        }
        v[n] = number;
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (n + 1 < wanted) {
            if (*p++ != ',') {
                return -1;
            }
        } else if (*p) {
            return -1;
        }
    }
    return 0;
}

int sdr_clock_execute(const char *line)
{
    const char *body = 0;
    ClockOperation op = CLOCK_NONE;
    unsigned n = 1, i;
    uint32_t v[7];
    int rc;
    if (!strncmp(line, "clock_begin=", 12)) {
        body = line + 12;
        op = CLOCK_BEGIN;
    } else if (!strncmp(line, "clock_rx=", 9)) {
        body = line + 9;
        op = CLOCK_RX;
        n = 7;
    } else if (!strncmp(line, "clock_tx=", 9)) {
        body = line + 9;
        op = CLOCK_TX;
        n = 7;
    } else if (!strncmp(line, "clock_commit=", 13)) {
        body = line + 13;
        op = CLOCK_COMMIT;
    } else if (!strncmp(line, "clock_abort=", 12)) {
        body = line + 12;
        op = CLOCK_ABORT;
    } else if (!strncmp(line, "clock_sample_rate=", 18)) {
        body = line + 18;
        op = CLOCK_SAMPLE_RATE;
    } else {
        return 0;
    }
    if (clock_parse_u32(body, v, n) < 0 || !v[0]) {
        console_print("ERR clock invalid_integer_or_parameters\n");
        return -1;
    }
    if (op == CLOCK_SAMPLE_RATE) {
        if (v[0] > 61440000U) {
            console_print("ERR clock sample_rate_out_of_range\n");
            return -1;
        }
        rc = sdr_clock_set_rate(v[0]);
        if (rc < 0) {
            return -1;
        }
        console_print("clock_sample_rate=%lu verified=1\n", (unsigned long)v[0]);
        return 1;
    }
    if (op == CLOCK_BEGIN) {
        memset(&clock_upload, 0, sizeof(clock_upload));
        clock_upload.token = v[0];
    } else {
        if (clock_upload.token != v[0]) {
            console_print("ERR clock token_mismatch\n");
            return -1;
        }
        if (op == CLOCK_RX || op == CLOCK_TX) {
            for (i = 1; i < 7; ++i) {
                if (!v[i]) {
                    console_print("ERR clock zero_frequency\n");
                    return -1;
                }
            }
            memcpy(op == CLOCK_RX ? clock_upload.rx : clock_upload.tx, &v[1], 6 * sizeof(uint32_t));
            clock_upload.present |= op == CLOCK_RX ? 1U : 2U;
        } else if (op == CLOCK_COMMIT) {
            if (clock_upload.present != 3) {
                console_print("ERR clock incomplete_transaction\n");
                return -1;
            }
            clock_upload.token = 0;
            clock_upload.present = 0;
            if (sdr_clock_apply(clock_upload.rx, clock_upload.tx) < 0) {
                return -1;
            }
        } else {
            memset(&clock_upload, 0, sizeof(clock_upload));
        }
    }
    console_print("clock_transaction token=%lu op=%u ok=1\n", (unsigned long)v[0], op);
    return 1;
}
