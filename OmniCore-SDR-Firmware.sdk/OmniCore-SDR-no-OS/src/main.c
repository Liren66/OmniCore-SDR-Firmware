/***************************************************************************/ /**
 *   @file   main.c
 *   @brief  Implementation of Main Function.
 *   @author DBogdan (dragos.bogdan@analog.com)
********************************************************************************
 * Copyright 2013(c) Analog Devices, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *  - The use of this software may or may not infringe the patent rights
 *    of one or more patent holders.  This license does not release you
 *    from the requirement that you obtain separate licenses from these
 *    patent holders to use this software.
 *  - Use of the software either in source or binary form, must be run
 *    on or directly connected to an Analog Devices Inc. component.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ANALOG DEVICES BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, INTELLECTUAL PROPERTY RIGHTS, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#include "board_config.h"
#include "command.h"
#include "console.h"
#include "sdr_control.h"
#include "sdr_board.h"
#include "board_monitor.h"
#include "xil_cache.h"
#include <stdio.h>

/* 主循环只负责接收命令和服务状态机。
 * 原有 AD9361 命令见 command.c；扩展命令见 sdr_commands.c。
 */
int main(void)
{
    char received_cmd[CONSOLE_LINE_CAPACITY];

    Xil_ICacheEnable();
    Xil_DCacheEnable();
    if (console_init(CONSOLE_BAUD_RATE) != 0) {
        printf("Console UART initialization failed.\n");
        return -1;
    }
    if (board_monitor_init() < 0) {
        console_print(
            "Board RGB/fan or Zynq temperature initialization failed; inspect board status.\n");
    }
    board_peripherals_init();
    if (sdr_rf_boot() < 0) {
        console_print("RF initialization failed; use rf_reinit! to retry.\n");
    }
    if (command_init() != 0) {
        console_print("CH32 driver/interrupt initialization failed.\n");
    }

    sdr_board_initializing(0);

    console_print("SDR ready protocol=1.0 TX=off USB=off; use help?\n");
    for (;;) {
        int line_result;
        command_service();
        line_result = console_poll_command(received_cmd, sizeof(received_cmd));
        if (line_result == CONSOLE_PENDING) {
            continue;
        }
        if (line_result < 0) {
            console_print("Invalid/overlong line discarded (maximum 127 characters).\n");
            continue;
        }
        command_execute(received_cmd);
    }
}
