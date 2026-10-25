#ifndef BOARD_MONITOR_H
#define BOARD_MONITOR_H

#include <stdint.h>

/* 板级状态只在主循环及其延时函数中访问，中断中不得调用。 */
typedef enum {
    BOARD_RGB_OFF,
    BOARD_RGB_INITIALIZING,
    BOARD_RGB_IDLE,
    BOARD_RGB_RX,
    BOARD_RGB_TX,
    BOARD_RGB_DUPLEX,
    BOARD_RGB_RECOVERING,
    BOARD_RGB_FAULT,
    BOARD_RGB_OVERTEMP
} BoardRgbState;

typedef struct {
    unsigned available, rgb_enabled, fan_enabled;
    BoardRgbState rgb_state;
    unsigned red, green, blue, blink_hz;
    unsigned temperature_valid, overtemperature;
    int32_t temperature_mc;
    uint32_t temperature_age_ms;
    unsigned fan_duty_permille, fan_target_permille, fan_boost, fan_failsafe;
} BoardMonitorStatus;

/* 冷启动恢复 RGB=开启、亮度约 15%、风扇温控=关闭。 */
int board_monitor_init(void);
int board_monitor_available(void);
void board_monitor_service(void);
void board_monitor_activity(unsigned initializing, unsigned fault, unsigned recovering,
                            unsigned receiving, unsigned transmitting);
int board_monitor_rgb_enable(unsigned enabled);
int board_monitor_fan_enable(unsigned enabled);
void board_monitor_status(BoardMonitorStatus *status);
const char *board_monitor_rgb_name(BoardRgbState state);

#endif
