/***************************************************************************//**
 *   @file   console.c
 *   @brief  Implementation of Console Driver.
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

/******************************************************************************/
/***************************** Include Files **********************************/
/******************************************************************************/
#include "stdarg.h"
#include "stdlib.h"
#include "stdio.h"
#include "config.h"
#include "console.h"
#include "board_monitor.h"
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>

#include "xparameters.h"
#include "xuartns550_l.h"

static size_t line_used;
static int line_invalid, skip_lf;

/* One execution context owns both sides of this fixed ring. It is serviced
 * during polled TX as well as normal RX; no ISR formats or dispatches commands. */
static unsigned char rx_ring[CONSOLE_RX_RING_CAPACITY];
static unsigned rx_head, rx_tail, rx_count;
static int rx_drop_line, rx_loss_pending;

static void uart_rx_lost(void)
{
    rx_head = rx_tail = rx_count = 0;
    line_used = 0;
    line_invalid = skip_lf = 0;
    rx_drop_line = 1;
    rx_loss_pending = 1;
}

static void uart_queue_byte(unsigned char ch)
{
    if (rx_drop_line) {
        if (ch == '\r' || ch == '\n') rx_drop_line = 0;
        return;
    }
    if (rx_count == CONSOLE_RX_RING_CAPACITY) {
        /* Queued commands and a possibly parsed prefix are uncertain. Drop
         * them together; resynchronize only at a complete line boundary. */
        uart_rx_lost();
        if (ch == '\r' || ch == '\n') rx_drop_line = 0;
        return;
    }
    rx_ring[rx_head] = ch;
    rx_head = (rx_head + 1U) % CONSOLE_RX_RING_CAPACITY;
    ++rx_count;
}

/* Return the last LSR snapshot so TX never reads/clears receive error bits
 * without examining them. The budget bounds time in a busy RX path. */
static u32 uart_capture_rx(void)
{
    const UINTPTR base = XPAR_AXI_UART16550_0_BASEADDR;
    unsigned budget = 32;
    u32 status;
    do {
        status = XUartNs550_ReadReg(base, XUN_LSR_OFFSET);
        if (status & XUN_LSR_ERROR_BREAK) {
            uart_rx_lost();
            /* Bytes already in the hardware FIFO may precede the lost byte.
             * Clearing only RX prevents using an earlier EOL as recovery. */
            XUartNs550_WriteReg(base, XUN_FCR_OFFSET,
                               XUN_FIFO_ENABLE | XUN_FIFO_RX_RESET);
        } else if (status & XUN_LSR_DATA_READY) {
            uart_queue_byte((unsigned char)XUartNs550_ReadReg(base, XUN_RBR_OFFSET));
        } else {
            break;
        }
    } while (--budget);
    return status;
}

static void uart_write_raw(unsigned char ch)
{
    while (!(uart_capture_rx() & XUN_LSR_TX_BUFFER_EMPTY)) {
        board_monitor_service();
    }
    XUartNs550_WriteReg(XPAR_AXI_UART16550_0_BASEADDR, XUN_THR_OFFSET, ch);
}

/***************************************************************************//**
 * @brief Initializes the PL UART16550 for polled console I/O, using 8 data
 *        bits, no parity and one stop bit. The bitstream must already be loaded.
 *
 * @param baudRate - Baud rate value.
 *                   Example: 9600 - 9600 bps.
 *
 * @return status  - Result of the initialization procedure.
 *                   Example: 0 - if initialization was successful;
 *                           -1 - if initialization was unsuccessful.
 *
*******************************************************************************/
#if (XPAR_AXI_UART16550_0_CLOCK_FREQ_HZ % (16UL * CONSOLE_BAUD_RATE)) != 0
#error CONSOLE_BAUD_RATE must divide the UART16550 clock / 16 exactly (CH32 side uses the same rate)
#endif
char uart_init(unsigned long baudRate)
{
	const UINTPTR base = XPAR_AXI_UART16550_0_BASEADDR;
	u64 divisor;

	if (baudRate == 0)
		return -1;

	divisor = ((u64)XPAR_AXI_UART16550_0_CLOCK_FREQ_HZ +
			   (u64)baudRate * 8U) / ((u64)baudRate * 16U);
	if (divisor == 0 || divisor > 0xFFFFU)
		return -1;

	/* Clear DLAB before accessing IER; this console does not use interrupts. */
	XUartNs550_SetLineControlReg(base, XUN_LCR_8_DATA_BITS);
	XUartNs550_WriteReg(base, XUN_IER_OFFSET, 0);
	XUartNs550_WriteReg(base, XUN_MCR_OFFSET, 0);
	XUartNs550_SetBaud(base, XPAR_AXI_UART16550_0_CLOCK_FREQ_HZ,
					  baudRate);

	/* Enable the FIFOs before resetting them, including on a debug restart. */
	XUartNs550_WriteReg(base, XUN_FCR_OFFSET, XUN_FIFO_ENABLE);
	XUartNs550_WriteReg(base, XUN_FCR_OFFSET,
					   XUN_FIFO_ENABLE | XUN_FIFO_RX_RESET | XUN_FIFO_TX_RESET);
	return 0;
}

/***************************************************************************//**
 * @brief Writes one character to UART.
 *
 * @param data - Character to write.
 *
 * @return None.
*******************************************************************************/
void uart_write_char(char data)
{
	/* Preserve the newline conversion previously supplied by BSP stdio. */
	if (data == '\n')
		uart_write_raw('\r');
	uart_write_raw((unsigned char)data);
}

/***************************************************************************//**
 * @brief Reads one character from UART.
 *
 * @return received_char   - Read character.
 *
 * Note: Blocking function - Waits until get a valid data.
*******************************************************************************/
void uart_read_char(char * data)
{
    while (rx_count == 0U) uart_capture_rx();
    *data = (char)rx_ring[rx_tail];
    rx_tail = (rx_tail + 1U) % CONSOLE_RX_RING_CAPACITY;
    --rx_count;
}

/***************************************************************************//**
 * @brief Writes one character string to UART.
 *
 * @param data - Character to write.
 *
 * @return None.
*******************************************************************************/
void uart_write_string(const char* string)
{
	while(*string)
	{
		uart_write_char(*string++);
	}
}

/* Use libc formatting: the old formatter overran its integer buffer and
 * overflowed a 32-bit temporary while printing large fractional values. */
void console_print(const char *format, ...)
{
    char local[256];
    char *output = local;
    int length;
    va_list args;
    va_start(args, format);
    length = vsnprintf(local, sizeof(local), format, args);
    va_end(args);
    if (length < 0) return;
    if ((size_t)length >= sizeof(local)) {
        output = malloc((size_t)length + 1U);
        if (output == NULL) {
            uart_write_string("Console output allocation failed.\n");
            return;
        }
        va_start(args, format);
        vsnprintf(output, (size_t)length + 1U, format, args);
        va_end(args);
    }
    uart_write_string(output);
    if (output != local) free(output);
}

char console_init(unsigned long baud_rate)
{
    line_used = 0;
    line_invalid = skip_lf = 0;
    rx_head = rx_tail = rx_count = 0;
    rx_drop_line = rx_loss_pending = 0;
    return uart_init(baud_rate);
}

static int uart_try_read(void)
{
    int received;
    uart_capture_rx();
    if (rx_loss_pending || rx_count == 0U) return EOF;
    received = rx_ring[rx_tail];
    rx_tail = (rx_tail + 1U) % CONSOLE_RX_RING_CAPACITY;
    --rx_count;
    return received;
}

int console_poll_command(char *command, size_t capacity)
{
    unsigned budget = 32;
    char ch;
    if (command == NULL || capacity == 0U) return -1;
    /* Keep the same buffer/capacity until a complete line is returned. */
    while (budget--) {
        int received = uart_try_read();
        if (rx_loss_pending) {
            rx_loss_pending = 0;
            command[0] = '\0';
            console_print("Error: UART receive overflow/error; queued and partial commands discarded.\n");
            return -1;
        }
        if (received == EOF) return CONSOLE_PENDING;
        ch = (char)received;
        if (skip_lf) {
            skip_lf = 0;
            if (ch == '\n') continue;
        }
        if (ch == '\r' || ch == '\n') {
            skip_lf = ch == '\r';
            int result = line_invalid ? -1 : (int)line_used;
            command[line_invalid ? 0U : line_used] = '\0';
            line_used = 0;
            line_invalid = 0;
            return result;
        }
        /* Once rejected, drain to EOL; never execute a truncated prefix. */
        if (line_invalid) continue;
        if (ch == '\b' || ch == 127) {
            if (line_used != 0U) --line_used;
        } else if (((unsigned char)ch < 32U && ch != '\t') ||
                   (unsigned char)ch > 126U) {
            line_invalid = 1;
        } else if (line_used + 1U >= capacity) {
            line_invalid = 1;
        } else {
            command[line_used++] = ch;
        }
    }
    return CONSOLE_PENDING;
}

int console_get_command(char *command, size_t capacity)
{
    int result;
    do { result = console_poll_command(command, capacity); }
    while (result == CONSOLE_PENDING);
    return result;
}

static const char *skip_blanks(const char *text)
{
    while (*text == ' ' || *text == '\t') ++text;
    return text;
}

static int end_of_line(const char *text)
{
    text = skip_blanks(text);
    if (*text == '\r') ++text;
    if (*text == '\n') ++text;
    return *text == '\0';
}

static int parse_number(const char *token, double *value)
{
    const char *digits = token;
    char *end;
    int sign = 1;
    double number = 0.0;
    if (*digits == '+' || *digits == '-') {
        if (*digits == '-') sign = -1;
        ++digits;
    }
    if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
        digits += 2;
        if (*digits == '\0') return 0;
        while (*digits) {
            unsigned int digit;
            if (*digits >= '0' && *digits <= '9') digit = *digits - '0';
            else if (*digits >= 'a' && *digits <= 'f') digit = *digits - 'a' + 10;
            else if (*digits >= 'A' && *digits <= 'F') digit = *digits - 'A' + 10;
            else return 0;
            /* Integers transported as double must remain exactly representable. */
            if (number > (9007199254740991.0 - digit) / 16.0) return 0;
            number = number * 16.0 + digit;
            ++digits;
        }
        *value = sign * number;
        return 1;
    }
    if (!isdigit((unsigned char)*digits) && *digits != '.') return 0;
    errno = 0;
    number = strtod(token, &end);
    if (end == token || *end != '\0' || errno == ERANGE ||
        !isfinite(number) || fabs(number) > 9007199254740991.0) return 0;
    *value = number;
    return 1;
}

int console_check_commands(const char *received, const char *expected,
                           double *param, char *param_no, size_t capacity)
{
    size_t name_length, length = 0, count = 0;
    const char *cursor;
    int type;
    char token[CONSOLE_LINE_CAPACITY];
    if (received == NULL || expected == NULL || param_no == NULL ||
        param == NULL || capacity > CONSOLE_MAX_PARAMS) return INVALID_CMD;
    *param_no = 0;
    while (length < CONSOLE_LINE_CAPACITY && received[length]) ++length;
    if (length == CONSOLE_LINE_CAPACITY) return INVALID_CMD;
    cursor = skip_blanks(received);
    name_length = strlen(expected);
    if (name_length == 0U) return UNKNOWN_CMD;
    if (strncmp(cursor, expected, name_length) != 0) return UNKNOWN_CMD;
    switch (expected[name_length - 1U]) {
    case '!': type = DO_CMD; break;
    case '?': type = READ_CMD; break;
    case '=': type = WRITE_CMD; break;
    default: return UNKNOWN_CMD;
    }
    cursor = skip_blanks(cursor + name_length);
    if (type == DO_CMD) return end_of_line(cursor) ? type : INVALID_CMD;
    while (!end_of_line(cursor)) {
        size_t token_length = 0;
        if (*cursor == '\r' || *cursor == '\n' || count >= capacity)
            return INVALID_CMD;
        while (*cursor && !isspace((unsigned char)*cursor)) {
            if (token_length + 1U >= sizeof(token)) return INVALID_CMD;
            token[token_length++] = *cursor++;
        }
        if (token_length == 0U) return INVALID_CMD;
        token[token_length] = '\0';
        if (!parse_number(token, &param[count])) return INVALID_CMD;
        ++count;
        cursor = skip_blanks(cursor);
    }
    *param_no = (char)count;
    return type;
}
