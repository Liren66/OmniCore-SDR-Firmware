/*
 * 40 MHz VCTCXO discipline, CH32H417 / Run_Core_V5F.
 * AD9361_MIX clock page: ADF4002 CP -> R109 100k -> Vc, R101/R123 1M/1M bias.
 * PA4 (DAC1_OUT) joins the CP node, so Vc = 0.833 * Vdac + 0.275 V and DAC
 * code 2048 (1.65 V) equals the high-Z bias: entering mode 2 is bumpless.
 * DAC1 buffer is off: datasheet Table 3-54 limits the buffered load to 50 pF,
 * while C276 (180 pF) sits directly on this node.
 *
 * Zynq -> MCU, one console line per PPS edge or 1 s heartbeat:
 *   VCXO req=<n> mode=<0..3> seq=<n> epoch=<n> new=<0|1> valid=<0|1>
 *        cnt=<DATA_CLK cycles> exp=<expected integer> expf=<expected Q16 fraction>
 * MCU -> Zynq, injected at a host command line boundary, at most once per
 * "VCXO" line and only while no host command is outstanding (VCXO_TakeReply):
 *   vcxo_ack req=<n> mode=<m> pa4=<0 high-Z|1 DAC> state=<VCXO_STATE_*> dac=<code>
 *            k=<milli-ppb/LSB> err=<milli-ppb> phase=<ns> n=<samples>
 * The Zynq enables the ADF4002 charge pump only after an acknowledgement
 * for its current req reports pa4=0. Reset leaves PA4 high-Z.
 */
#include "vcxo_discipline.h"
#include <string.h>
#if defined(Core_V5F)
#define VCXO_LINE_SIZE 128U
#define VCXO_DAC_MAX 4095.0f
#define VCXO_DAC_MID 2048U
#define VCXO_CAL_STEP 400.0f
#define VCXO_SETTLE 2U              /* samples skipped after every DAC step */
#define VCXO_CAL_AVERAGE 16U
#define VCXO_ACQ_WINDOW 8U
#define VCXO_ACQ_GAIN 0.7f
#define VCXO_ACQ_DONE_PPB 20.0f
/* Type-2 phase loop, T = 1 s, zeta = 0.707: kp = 2 zeta / tau, ki = 1 / tau^2.
 * A frequency ramp r leaves a static phase error r * tau^2; tau trades that
 * against PPS jitter, which is negligible next to the ~10 ppb DAC step. */
#ifndef VCXO_TRACK_TAU_S
/* 15 s. The board's 40 MHz moves ~140 ppb within a minute when streaming
 * starts or stops (thermal); a ramp r peaks at about r * tau^2: ~550 ns here
 * against ~2000 ns at 40 s. PPS jitter adds kp * jitter (~2 ppb at 20 ns),
 * below one ~10 ppb DAC step. */
#define VCXO_TRACK_TAU_S 15.0f
#endif
#define VCXO_TRACK_KP (1.414f / VCXO_TRACK_TAU_S)
#define VCXO_TRACK_KI (1.0f / (VCXO_TRACK_TAU_S * VCXO_TRACK_TAU_S))
#define VCXO_OUTLIER_PPB 500.0f
#define VCXO_OUTLIER_LIMIT 5U
#define VCXO_LOST_NS 2000.0f
#define VCXO_SAMPLE_PPB_LIMIT 50000.0f
#define VCXO_K_MIN 0.05f
#define VCXO_K_MAX 100.0f
#define VCXO_K_DEFAULT 4.0f
#define VCXO_SAMPLE_TIMEOUT_MS 3000U
#define VCXO_REACQUIRE_MS 60000U
#define VCXO_IDLE_WINDOW_MS 50U     /* reply only this soon after a "VCXO" line */
/* A host line without "RSP ... end" (bare terminal command) is assumed done
 * once a "VCXO" line arrives this long after it: the Zynq prints none while
 * a command runs, and executes any queued command right after printing one. */
#define VCXO_HOST_DONE_MS 1000U

/* Free-running 32-bit TIM11 at 10 kHz. The V5F main loop runs in user mode,
 * where reading mcycle traps (illegal instruction); SysTick1 is restarted by
 * every Delay_Ms() on V5F. TIM9..TIM11 are otherwise unused here. */
#define VCXO_TIMER TIM11
#define VCXO_TIMER_HZ 10000U

volatile VCXO_Stats_t VCXO_Stats;
static char line[VCXO_LINE_SIZE];
static uint16_t line_used;
static uint8_t line_invalid;
static uint32_t now_ms, tick_rest, tick_last;
static struct
{
    uint8_t mode, state, held_state, driving, have_k, seq_valid, reply_pending;
    uint8_t cal_phase, good_windows, outlier_run, zynq_idle, host_busy;
    uint32_t req, last_seq, last_epoch, last_sample_ms, hold_since_ms, idle_ms, host_ms;
    uint32_t settle, count;
    float dac, last_good_dac, k, sum, cal_first, center, integral;
    float err_ppb, phase_ns, residual;
    int64_t theta_q16;
} v;

static void timer_init(void)
{
    RCC_ClocksTypeDef clocks;
    TIM9_12_TimeBaseInitTypeDef base = {0};
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_TIM11, ENABLE);
    RCC_GetClocksFreq(&clocks);
    base.TIM_Period = 0xFFFFFFFFU;
    base.TIM_Prescaler = (uint16_t)(clocks.HCLK_Frequency / VCXO_TIMER_HZ - 1U);
    base.TIM_ClockDivision = TIM_CKD_DIV1;
    base.TIM_CounterMode = TIM_CounterMode_Up;
    TIM9_12_TimeBaseInit(VCXO_TIMER, &base);
    TIM_Cmd(VCXO_TIMER, ENABLE);
    tick_last = TIM9_12_GetCounter(VCXO_TIMER);
}
static float absf(float x)
{
    return x < 0.0f ? -x : x;
}
static float clamp_dac(float x)
{
    return x < 0.0f ? 0.0f : (x > VCXO_DAC_MAX ? VCXO_DAC_MAX : x);
}
/* TRACK uses first-order sigma-delta rounding at the 1 Hz loop rate: the
 * rounding error is carried into the next second, so the mean frequency is
 * the fractional PI output instead of a two-code limit cycle (one code is
 * ~10 ppb here). Each step adds at most ~1 code-second of time error
 * (~10 ns). Faster dithering would add modulation sidebands to the RF LO. */
static void dac_write(void)
{
    float target;
    int32_t code;
    v.dac = clamp_dac(v.dac);
    target = v.dac + (v.state == VCXO_STATE_TRACK ? v.residual : 0.0f);
    code = (int32_t)(target + 0.5f);
    if(code < 0)
        code = 0;
    if(code > (int32_t)VCXO_DAC_MAX)
        code = (int32_t)VCXO_DAC_MAX;
    v.residual = v.state == VCXO_STATE_TRACK ? target - (float)code : 0.0f;
    DAC_SetChannel1Data(DAC_Align_12b_R, (uint16_t)code);
}
/* Load the data first, then enable: the pin never shows a stale code. */
static void drive(uint8_t on)
{
    if(on)
    {
        dac_write();
        DAC_Cmd(DAC_Channel_1, ENABLE);
    }
    else DAC_Cmd(DAC_Channel_1, DISABLE);
    v.driving = on;
}
void VCXO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    DAC_InitTypeDef dac = {0};
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA, ENABLE);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_DAC, ENABLE);
    DAC_Cmd(DAC_Channel_1, DISABLE);
    gpio.GPIO_Pin = GPIO_Pin_4;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &gpio);
    DAC_StructInit(&dac);
    dac.DAC_Trigger = DAC_Trigger_None;
    dac.DAC_OutputBuffer = DAC_OutputBuffer_Disable;
    DAC_Init(DAC_Channel_1, &dac);
    memset(&v, 0, sizeof(v));
    memset((void *)&VCXO_Stats, 0, sizeof(VCXO_Stats));
    v.dac = v.last_good_dac = VCXO_DAC_MID;
    v.k = VCXO_K_DEFAULT;
    drive(0);
    VCXO_RxReset();
    timer_init();
}
void VCXO_RxReset(void)
{
    line_used = 0;
    line_invalid = 0;
}

static void enter(uint8_t state)
{
    v.state = state;
    v.settle = VCXO_SETTLE;
    v.count = 0;
    v.sum = 0.0f;
    v.good_windows = 0;
    v.outlier_run = 0;
    if(state == VCXO_STATE_CAL)
        v.cal_phase = 0;
    if(state == VCXO_STATE_TRACK)
    {
        v.center = v.dac;
        v.integral = 0.0f;
        v.theta_q16 = 0;
        v.phase_ns = 0.0f;
    }
}
static void set_mode(uint8_t mode)
{
    if(mode == v.mode)
        return;
    v.mode = mode;
    v.seq_valid = 0;
    if(mode != VCXO_MODE_PPS)
    {
        drive(0);
        v.state = VCXO_STATE_OFF;
        return;
    }
    /* A calibrated slope restarts from the last good code; otherwise start at
     * the 1.65 V bias, which the high-Z divider already holds. */
    v.dac = v.have_k ? v.last_good_dac : (float)VCXO_DAC_MID;
    drive(1);
    v.last_sample_ms = now_ms;
    enter(v.have_k ? VCXO_STATE_ACQ : VCXO_STATE_CAL);
}

/* d_q16: (cnt - expected) * 65536; exp_q16: expected * 65536. */
static void sample(int64_t d_q16, uint64_t exp_q16, uint8_t contiguous)
{
    float e = (float)d_q16 / (float)exp_q16 * 1.0e9f;
    float x, step;
    v.err_ppb = e;
    VCXO_Stats.samples++;
    if(absf(e) > VCXO_SAMPLE_PPB_LIMIT)
    {
        VCXO_Stats.outliers++;
        v.seq_valid = 0;
        return;
    }
    if(v.settle)
    {
        --v.settle;
        return;
    }
    switch(v.state)
    {
    case VCXO_STATE_CAL:
        v.sum += e;
        if(++v.count < VCXO_CAL_AVERAGE)
            break;
        if(!v.cal_phase)
        {
            v.cal_first = v.sum / (float)v.count;
            v.dac += VCXO_CAL_STEP;
            dac_write();
            v.cal_phase = 1;
            v.settle = VCXO_SETTLE;
            v.count = 0;
            v.sum = 0.0f;
            break;
        }
        e = v.sum / (float)v.count;
        v.k = (e - v.cal_first) / VCXO_CAL_STEP;
        v.have_k = absf(v.k) >= VCXO_K_MIN && absf(v.k) <= VCXO_K_MAX;
        if(!v.have_k)
            v.k = VCXO_K_DEFAULT; /* No usable response: retried on next mode entry. */
        v.dac -= e / v.k;
        dac_write();
        enter(VCXO_STATE_ACQ);
        break;
    case VCXO_STATE_ACQ:
        v.sum += e;
        if(++v.count < VCXO_ACQ_WINDOW)
            break;
        e = v.sum / (float)v.count;
        v.dac -= VCXO_ACQ_GAIN * e / v.k;
        dac_write();
        v.good_windows = absf(e) < VCXO_ACQ_DONE_PPB ? v.good_windows + 1U : 0U;
        v.count = 0;
        v.sum = 0.0f;
        v.settle = VCXO_SETTLE;
        if(v.good_windows >= 2U)
            enter(VCXO_STATE_TRACK);
        break;
    case VCXO_STATE_TRACK:
        if(absf(e) > VCXO_OUTLIER_PPB)
        {
            /* A glitched edge corrupts this and the next interval: rezero phase. */
            VCXO_Stats.outliers++;
            v.seq_valid = 0;
            if(++v.outlier_run > VCXO_OUTLIER_LIMIT)
                enter(VCXO_STATE_ACQ);
            break;
        }
        v.outlier_run = 0;
        if(!contiguous)
            v.theta_q16 = 0;
        v.theta_q16 += d_q16;
        x = (float)v.theta_q16 / (float)exp_q16 * 1.0e9f;
        v.phase_ns = x;
        if(absf(x) > VCXO_LOST_NS)
        {
            /* Phase slip: keep the frequency integrator and DAC, rezero the
             * phase. Falling back to ACQ discarded the frequency estimate and
             * lost lock for a minute after a thermal step. */
            VCXO_Stats.phase_slips++;
            v.theta_q16 = 0;
            v.phase_ns = 0.0f;
            break;
        }
        step = VCXO_TRACK_KI * x;
        v.integral += step;
        v.dac = v.center - (VCXO_TRACK_KP * x + v.integral) / v.k;
        if(v.dac < 0.0f || v.dac > VCXO_DAC_MAX)
            v.integral -= step; /* anti-windup at the DAC rail */
        dac_write();
        v.last_good_dac = v.dac;
        break;
    default:
        break;
    }
}

static int parse_u32(const char *key, uint32_t *value)
{
    const char *p = line;
    size_t n = strlen(key);
    uint32_t x = 0, d;
    unsigned digits = 0;
    while(p && *p)
    {
        if((p == line || p[-1] == ' ') && !strncmp(p, key, n) && p[n] == '=')
        {
            p += n + 1U;
            while(*p >= '0' && *p <= '9')
            {
                d = (uint32_t)(*p++ - '0');
                if(x > (0xFFFFFFFFU - d) / 10U)
                    return -1;
                x = x * 10U + d;
                ++digits;
            }
            if(!digits || (*p && *p != ' '))
                return -1;
            *value = x;
            return 0;
        }
        p = strchr(p, ' ');
        if(p)
            ++p;
    }
    return -1;
}
static void handle_line(void)
{
    uint32_t req, mode, seq, epoch, fresh, valid, cnt, exp, expf;
    uint8_t contiguous;
    if(!strncmp(line, "RSP id=", 7) && strstr(line, " end result="))
    {
        const char *p = line + 7;
        uint32_t id = 0;
        while(*p >= '0' && *p <= '9')
            id = id * 10U + (uint32_t)(*p++ - '0');
        VCXO_Stats.rsp_ends++;
        VCXO_Stats.last_rsp_id = id;
        v.host_busy = 0; /* the enveloped host command has finished */
        return;
    }
    if(strncmp(line, "VCXO ", 5))
        return;
    VCXO_Stats.lines++;
    if(parse_u32("req", &req) || parse_u32("mode", &mode) || mode > VCXO_MODE_REF_PPS ||
       parse_u32("new", &fresh) || parse_u32("valid", &valid))
    {
        VCXO_Stats.line_errors++;
        return;
    }
    v.req = req;
    set_mode((uint8_t)mode);
    v.reply_pending = 1U;
    if(v.host_busy && now_ms - v.host_ms > VCXO_HOST_DONE_MS)
        v.host_busy = 0;
    v.zynq_idle = !v.host_busy;
    v.idle_ms = now_ms;
    if(v.mode != VCXO_MODE_PPS || !fresh)
        return;
    if(!valid)
    {
        v.seq_valid = 0;
        return;
    }
    if(parse_u32("seq", &seq) || parse_u32("epoch", &epoch) || parse_u32("cnt", &cnt) ||
       parse_u32("exp", &exp) || parse_u32("expf", &expf) || !exp || expf > 0xFFFFU)
    {
        VCXO_Stats.line_errors++;
        return;
    }
    contiguous = v.seq_valid && epoch == v.last_epoch && seq == v.last_seq + 1U;
    v.seq_valid = 1U;
    v.last_seq = seq;
    v.last_epoch = epoch;
    v.last_sample_ms = now_ms;
    if(v.state == VCXO_STATE_HOLD)
    {
        if(v.held_state == VCXO_STATE_TRACK && now_ms - v.hold_since_ms < VCXO_REACQUIRE_MS)
            v.state = VCXO_STATE_TRACK; /* keep frequency integral, rezero phase */
        else
            enter(v.held_state == VCXO_STATE_CAL ? VCXO_STATE_CAL : VCXO_STATE_ACQ);
        v.settle = 0;
        contiguous = 0;
    }
    sample(((int64_t)cnt << 16) - (((int64_t)exp << 16) | expf),
           ((uint64_t)exp << 16) | expf, contiguous);
}
void VCXO_RxByte(uint8_t ch)
{
    if(ch == '\r' || ch == '\n')
    {
        if(line_used && !line_invalid)
        {
            line[line_used] = '\0';
            handle_line();
        }
        VCXO_RxReset();
        return;
    }
    if(line_invalid)
        return;
    if(line_used + 1U >= VCXO_LINE_SIZE || ch < 32U || ch > 126U)
        line_invalid = 1U;
    else
        line[line_used++] = (char)ch;
}

void VCXO_Task(void)
{
    uint32_t now = TIM9_12_GetCounter(VCXO_TIMER), per_ms = VCXO_TIMER_HZ / 1000U;
    tick_rest += now - tick_last;
    tick_last = now;
    if(tick_rest >= per_ms)
    {
        now_ms += tick_rest / per_ms;
        tick_rest %= per_ms;
    }
    /* Hold the last DAC code while the Zynq is silent or busy; only a
     * received mode change releases PA4. */
    if(v.mode == VCXO_MODE_PPS && v.state != VCXO_STATE_HOLD &&
       now_ms - v.last_sample_ms > VCXO_SAMPLE_TIMEOUT_MS)
    {
        v.held_state = v.state;
        v.state = VCXO_STATE_HOLD;
        v.hold_since_ms = now_ms;
        v.seq_valid = 0;
    }
}

static char *put_s(char *p, const char *s)
{
    while(*s)
        *p++ = *s++;
    return p;
}
static char *put_u(char *p, uint32_t x)
{
    char digits[10];
    unsigned n = 0;
    do
    {
        digits[n++] = (char)('0' + x % 10U);
        x /= 10U;
    } while(x);
    while(n)
        *p++ = digits[--n];
    return p;
}
static char *put_i(char *p, float x)
{
    if(x > 2.0e9f)
        x = 2.0e9f;
    if(x < -2.0e9f)
        x = -2.0e9f;
    if(x < 0.0f)
    {
        *p++ = '-';
        x = -x;
    }
    return put_u(p, (uint32_t)(x + 0.5f));
}
uint16_t VCXO_TakeReply(uint8_t *out, uint16_t capacity)
{
    char text[VCXO_LINE_SIZE], *p = text;
    uint16_t n;
    VCXO_Task(); /* refresh now_ms */
    if(!v.reply_pending || !v.zynq_idle || now_ms - v.idle_ms > VCXO_IDLE_WINDOW_MS)
        return 0;
    p = put_s(p, "vcxo_ack req=");
    p = put_u(p, v.req);
    p = put_s(p, " mode=");
    p = put_u(p, v.mode);
    p = put_s(p, " pa4=");
    p = put_u(p, v.driving);
    p = put_s(p, " state=");
    p = put_u(p, v.state);
    p = put_s(p, " dac=");
    p = put_u(p, (uint32_t)(v.dac + 0.5f));
    p = put_s(p, " k=");
    p = put_i(p, v.have_k ? v.k * 1000.0f : 0.0f);
    p = put_s(p, " err=");
    p = put_i(p, v.err_ppb * 1000.0f);
    p = put_s(p, " phase=");
    p = put_i(p, v.phase_ns);
    p = put_s(p, " n=");
    p = put_u(p, VCXO_Stats.samples);
    *p++ = '\n';
    n = (uint16_t)(p - text);
    if(n > capacity)
        return 0;
    memcpy(out, text, n);
    v.reply_pending = 0;
    v.zynq_idle = 0; /* one reply per idle "VCXO" line */
    VCXO_Stats.replies++;
    return n;
}
void VCXO_HostActivity(void)
{
    VCXO_Task(); /* refresh now_ms */
    v.zynq_idle = 0;
    v.host_busy = 1U;
    v.host_ms = now_ms;
}
#endif /* Core_V5F: PA4/DAC1 belong to the V5F bridge firmware. */
