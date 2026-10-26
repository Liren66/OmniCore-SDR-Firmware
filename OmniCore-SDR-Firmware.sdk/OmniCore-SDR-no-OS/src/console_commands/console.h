/***************************************************************************//**
 *   @file   console.h
 *   @brief  Header file of Console Driver.
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
#ifndef __CONSOLE_H__
#define __CONSOLE_H__
#include <stddef.h>

/******************************************************************************/
/******************** Macros and Constants Definitions ************************/
/******************************************************************************/
#define UNKNOWN_CMD	-1
#define DO_CMD	   	0
#define READ_CMD	1
#define WRITE_CMD	2
#define INVALID_CMD -2
#define CONSOLE_LINE_CAPACITY 128U
#define CONSOLE_MAX_PARAMS 5U
#define CONSOLE_PENDING -2
#define CONSOLE_RX_RING_CAPACITY 2048U
/* PL UART16550 <-> CH32 USART1 link. Must equal UART1_BRIDGE_BAUD in the CH32
 * project. Both sides use a 100 MHz clock with 16x oversampling, so exact rates
 * are 6.25 MBd / N; 6.25 MBd (N=1) is the ceiling of both UARTs. */
#define CONSOLE_BAUD_RATE 3125000UL

/******************************************************************************/
/************************ Functions Declarations ******************************/
/******************************************************************************/
/* Initializes the serial console. */
char console_init(unsigned long baud_rate);

/* Prints formatted data to console. */
void console_print(const char* str, ...)
    __attribute__((format(printf, 1, 2)));

/* Reads one command from console. */
/* Returns length, or -1 after discarding an overlong/invalid entire line. */
int console_get_command(char* command, size_t capacity);
/* Nonblocking, at most 32 received bytes/call. Keep buffer/capacity stable
 * between calls. Returns CONSOLE_PENDING, -1 (bad line), or line length. */
int console_poll_command(char* command, size_t capacity);
/* Main-loop context only. Moves received bytes from the 16-byte UART FIFO
 * into the line ring without dispatching. Call inside any loop that may run
 * longer than ~50 us outside a host command (3.125 MBd fills the FIFO in
 * 51 us; an overrun discards the next whole line). */
void console_poll_rx(void);
/* After the GIC is initialised: receive through the UART16550 interrupt
 * (XScuGic *). Returns 0 on success; on failure the console stays polled. */
int console_enable_rx_irq(void *gic);
/* 1 when console RX runs from the UART interrupt (self-test passed). */
int console_rx_irq_active(void);

/* Compares two commands and returns the type of the command. */
int console_check_commands(const char* received_cmd,
						   const char* expected_cmd,
						   double*	   param,
						   char*	   param_no,
                           size_t capacity);

#endif /*__CONSOLE_H__*/
