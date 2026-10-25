#include "board_monitor.h"
#include "xadcps.h"
#include "xil_io.h"
#include "xparameters.h"
#include "xtime_l.h"
#include <string.h>

#define BOARD_RGB_OFFSET 0x00U
#define BOARD_FAN_PERIOD_OFFSET 0x04U
#define BOARD_FAN_HIGH_OFFSET 0x08U
#define BOARD_CLOCK_HZ_OFFSET 0x0CU
#define BOARD_FAN_FREQUENCY_HZ 25000U
#define BOARD_RGB_LEVEL 38U
#define BOARD_SERVICE_US 20000ULL
#define BOARD_SAMPLE_US 1000000ULL
#define BOARD_SAMPLE_STALE_US 2500000ULL
#define BOARD_FAN_BOOST_US 500000ULL
#define BOARD_FAN_HYSTERESIS_MC 2000
#define BOARD_ALARM_ON_MC 80000
#define BOARD_ALARM_OFF_MC 75000

/* 温度使用 Zynq XADC，单位为毫摄氏度；AD9361 温度不参与调速。 */
static const int32_t fan_threshold_mc[] = {40000, 50000, 60000, 70000};
static const unsigned fan_duty_permille[] = {0, 300, 500, 700, 1000};
static const char *rgb_names[] = {"off",    "initializing", "idle",  "rx",      "tx",
                                  "duplex", "recovering",   "fault", "overtemp"};
static XAdcPs xadc;
static BoardMonitorStatus state;
static unsigned initialized, xadc_ready, have_sample, fan_band;
static unsigned initializing, fault, recovering, receiving, transmitting;
static uint32_t fan_period, last_rgb;
static uint64_t service_at, sample_at, last_sample_at, boost_until, rgb_since;

static uint64_t board_now_us(void)
{
    XTime ticks;
    XTime_GetTime(&ticks);
    return ((uint64_t)ticks / COUNTS_PER_SECOND) * 1000000U +
           ((uint64_t)ticks % COUNTS_PER_SECOND) * 1000000U / COUNTS_PER_SECOND;
}

static int temperature_init(void)
{
    XAdcPs_Config *config = XAdcPs_LookupConfig(XPAR_XADCPS_0_DEVICE_ID);
    if (!config || XAdcPs_CfgInitialize(&xadc, config, config->BaseAddress) != XST_SUCCESS) {
        return -1;
    }
    /* 只使用内部温度及校准通道，不占用外部模拟引脚或 GIC 中断。 */
    XAdcPs_SetSequencerMode(&xadc, XADCPS_SEQ_MODE_SAFE);
    XAdcPs_SetAdcClkDivisor(&xadc, 32);
    XAdcPs_SetAvg(&xadc, XADCPS_AVG_16_SAMPLES);
    if (XAdcPs_SetSeqChEnables(&xadc, XADCPS_SEQ_CH_TEMP | XADCPS_SEQ_CH_CALIB) != XST_SUCCESS ||
        XAdcPs_SetSeqAvgEnables(&xadc, XADCPS_SEQ_CH_TEMP) != XST_SUCCESS) {
        return -1;
    }
    XAdcPs_SetSequencerMode(&xadc, XADCPS_SEQ_MODE_CONTINPASS);
    return 0;
}

static void temperature_sample(uint64_t now)
{
    uint16_t raw;
    int32_t temperature;
    state.temperature_valid = 0;
    if (!xadc_ready || XAdcPs_GetSequencerMode(&xadc) != XADCPS_SEQ_MODE_CONTINPASS ||
        !(XAdcPs_GetSeqChEnables(&xadc) & XADCPS_SEQ_CH_TEMP)) {
        return;
    }
    raw = XAdcPs_GetAdcData(&xadc, XADCPS_CH_TEMP);
    temperature = (int32_t)(XAdcPs_RawToTemperature(raw) * 1000.0f);
    /* 排除未转换、总线异常或不合理的数据，故障时不沿用旧温度调速。 */
    if (raw == 0 || raw == UINT16_MAX || temperature < -40000 || temperature > 125000) {
        return;
    }
    state.temperature_mc = temperature;
    state.temperature_valid = have_sample = 1;
    last_sample_at = now;
    if (temperature >= BOARD_ALARM_ON_MC) {
        state.overtemperature = 1;
    } else if (temperature < BOARD_ALARM_OFF_MC) {
        state.overtemperature = 0;
    }
}

static void fan_update(uint64_t now)
{
    unsigned target = 0, duty;
    state.fan_failsafe = state.fan_enabled && !state.temperature_valid;
    if (state.fan_enabled && state.temperature_valid) {
        while (fan_band < 4 && state.temperature_mc >= fan_threshold_mc[fan_band]) {
            ++fan_band;
        }
        while (fan_band > 0 &&
               state.temperature_mc < fan_threshold_mc[fan_band - 1] - BOARD_FAN_HYSTERESIS_MC) {
            --fan_band;
        }
        target = fan_duty_permille[fan_band];
    } else {
        fan_band = 0;
        if (state.fan_failsafe) {
            target = 1000;
        }
    }
    if (!target || state.fan_failsafe) {
        boost_until = 0;
    } else if (!state.fan_duty_permille) {
        boost_until = now + BOARD_FAN_BOOST_US;
    }
    state.fan_target_permille = target;
    state.fan_boost = target && now < boost_until;
    duty = state.fan_boost ? 1000 : target;
    if (state.available && duty != state.fan_duty_permille) {
        Xil_Out32(XPAR_BOARD_CTRL_AXI_0_BASEADDR + BOARD_FAN_HIGH_OFFSET,
                  (uint32_t)(((uint64_t)fan_period * duty + 500U) / 1000U));
    }
    state.fan_duty_permille = duty;
}

static void rgb_update(uint64_t now)
{
    BoardRgbState next;
    unsigned red = 0, green = 0, blue = 0, blink = 0;
    uint32_t color;
    if (!state.rgb_enabled) {
        next = BOARD_RGB_OFF;
    } else if (state.overtemperature) {
        next = BOARD_RGB_OVERTEMP;
        red = BOARD_RGB_LEVEL;
        blink = 4;
    } else if (fault) {
        next = BOARD_RGB_FAULT;
        red = BOARD_RGB_LEVEL;
    } else if (recovering) {
        next = BOARD_RGB_RECOVERING;
        red = BOARD_RGB_LEVEL;
        green = 10;
        blink = 1;
    } else if (initializing) {
        next = BOARD_RGB_INITIALIZING;
        red = green = BOARD_RGB_LEVEL;
    } else if (receiving && transmitting) {
        next = BOARD_RGB_DUPLEX;
        green = blue = BOARD_RGB_LEVEL;
    } else if (transmitting) {
        next = BOARD_RGB_TX;
        red = blue = BOARD_RGB_LEVEL;
    } else if (receiving) {
        next = BOARD_RGB_RX;
        blue = BOARD_RGB_LEVEL;
    } else {
        next = BOARD_RGB_IDLE;
        green = BOARD_RGB_LEVEL;
    }
    if (next != state.rgb_state) {
        rgb_since = now;
    }
    state.rgb_state = next;
    state.blink_hz = blink;
    if (blink && ((now - rgb_since) / (500000U / blink)) % 2U) {
        red = green = blue = 0;
    }
    state.red = red;
    state.green = green;
    state.blue = blue;
    color = (red << 16) | (green << 8) | blue;
    if (state.available && color != last_rgb) {
        Xil_Out32(XPAR_BOARD_CTRL_AXI_0_BASEADDR + BOARD_RGB_OFFSET, color);
        last_rgb = color;
    }
}

int board_monitor_init(void)
{
    uint32_t clock_hz;
    uint64_t now = board_now_us();
    memset(&state, 0, sizeof(state));
    initialized = xadc_ready = have_sample = fan_band = 0;
    fault = recovering = receiving = transmitting = 0;
    initializing = state.rgb_enabled = 1;
    service_at = now;
    sample_at = now + 100000U; /* 给连续转换留出首次采样时间。 */
    last_sample_at = boost_until = 0;
    rgb_since = now;
    last_rgb = UINT32_MAX;
    /* ELF 单独重启也必须清除上次留下的风扇输出。 */
    Xil_Out32(XPAR_BOARD_CTRL_AXI_0_BASEADDR + BOARD_FAN_HIGH_OFFSET, 0);
    clock_hz = Xil_In32(XPAR_BOARD_CTRL_AXI_0_BASEADDR + BOARD_CLOCK_HZ_OFFSET);
    state.available = clock_hz >= 50000000U && clock_hz <= 200000000U;
    if (state.available) {
        fan_period = clock_hz / BOARD_FAN_FREQUENCY_HZ;
        Xil_Out32(XPAR_BOARD_CTRL_AXI_0_BASEADDR + BOARD_FAN_PERIOD_OFFSET, fan_period);
    }
    xadc_ready = temperature_init() == 0;
    initialized = 1;
    rgb_update(now);
    return state.available && xadc_ready ? 0 : -1;
}

int board_monitor_available(void)
{
    return state.available;
}

void board_monitor_service(void)
{
    uint64_t now, age;
    if (!initialized) {
        return;
    }
    now = board_now_us();
    if (now < service_at) {
        return;
    }
    service_at = now + BOARD_SERVICE_US;
    /* 服务间隔过长时，先让旧温度失效；本次重新采到有效值才能解除。 */
    age = now - last_sample_at;
    if (!have_sample || age > BOARD_SAMPLE_STALE_US) {
        state.temperature_valid = 0;
        fan_update(now);
    }
    if (now >= sample_at) {
        temperature_sample(now);
        sample_at = now + BOARD_SAMPLE_US;
    }
    age = now - last_sample_at;
    state.temperature_age_ms =
        !have_sample || age / 1000U > UINT32_MAX ? UINT32_MAX : (uint32_t)(age / 1000U);
    fan_update(now);
    rgb_update(now);
}

void board_monitor_activity(unsigned init, unsigned failed, unsigned recovery, unsigned rx,
                            unsigned tx)
{
    initializing = init;
    fault = failed;
    recovering = recovery;
    receiving = rx;
    transmitting = tx;
    if (initialized) {
        rgb_update(board_now_us());
    }
}

int board_monitor_rgb_enable(unsigned enabled)
{
    if (enabled > 1 || !state.available) {
        return -1;
    }
    state.rgb_enabled = enabled;
    rgb_update(board_now_us());
    return 0;
}

int board_monitor_fan_enable(unsigned enabled)
{
    if (enabled > 1 || !state.available) {
        return -1;
    }
    board_monitor_service();
    state.fan_enabled = enabled;
    fan_update(board_now_us());
    return 0;
}

void board_monitor_status(BoardMonitorStatus *status)
{
    board_monitor_service();
    *status = state;
}

const char *board_monitor_rgb_name(BoardRgbState value)
{
    return value <= BOARD_RGB_OVERTEMP ? rgb_names[value] : "unknown";
}
