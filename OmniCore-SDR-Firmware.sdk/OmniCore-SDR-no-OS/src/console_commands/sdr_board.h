#ifndef SDR_BOARD_H
#define SDR_BOARD_H

void sdr_board_initializing(unsigned active);
void sdr_board_service(unsigned recovering);
void sdr_board_status(void);
void sdr_board_rgb_status(void);
void sdr_board_fan_status(void);
void sdr_board_temperature_status(void);
int sdr_board_rgb_command(unsigned write, unsigned enabled);
int sdr_board_fan_command(unsigned write, unsigned enabled);

#endif
