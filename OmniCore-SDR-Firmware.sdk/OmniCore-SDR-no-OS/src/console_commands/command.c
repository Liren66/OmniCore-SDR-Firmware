/**************************************************************************//**
 *   @file   command.c
 *   @brief  Implementation of AD9361 Command Driver.
 *   @author DBogdan (dragos.bogdan@analog.com)
 *******************************************************************************
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
#include "command.h"
#include "sdr_control.h"
#include "sdr_commands.h"
#include "console.h"
#include "../ad9361_api.h"
#include "dac_core.h"
#include "CH32H417_IP.h"
#include "ch32h417_irq.h"
#include "xparameters.h"
#include <string.h>
#include <math.h>
#include <stdint.h>

static int report_api_error(int result);
static int validate_arguments(const char *name, double *param, char count);

/******************************************************************************/
/************************ Constants Definitions *******************************/
/******************************************************************************/
const command cmd_list[] = {
	{"help?", "Displays all available commands.", "help?", get_help, 0, 0, 0, 1},
	{"register?", "Gets the specified register value.", "register?0x037", get_register, 1, 0, 1023, 1},
	{"tx_lo_freq?", "Gets current TX LO frequency [MHz].", "tx_lo_freq?", get_tx_lo_freq, 0, 0, 0, 1},
	{"tx_lo_freq=", "Sets the TX LO frequency [MHz].", "tx_lo_freq=2400.125", set_tx_lo_freq, 1, 70, 6000, 0},
	{"tx_samp_freq?", "Gets current TX sampling frequency [Hz].", "tx_samp_freq?", get_tx_samp_freq, 0, 0, 0, 1},
	{"tx_samp_freq=", "Sets the TX sampling frequency [Hz].", "tx_samp_freq=30720000", set_tx_samp_freq, 1, 1, 61440000, 1},
	{"tx_rf_bandwidth?", "Gets current TX RF bandwidth [Hz].", "tx_rf_bandwidth?", get_tx_rf_bandwidth, 0, 0, 0, 1},
	{"tx_rf_bandwidth=", "Sets the TX RF bandwidth [Hz].", "tx_rf_bandwidth=18000000", set_tx_rf_bandwidth, 1, 1, 56000000, 1},
	{"tx1_attenuation?", "Gets current TX1 attenuation [mdB].", "tx1_attenuation?", get_tx1_attenuation, 0, 0, 0, 1},
	{"tx1_attenuation=", "Sets the TX1 attenuation [mdB].", "tx1_attenuation=10000", set_tx1_attenuation, 1, 0, 89750, 1},
	{"tx2_attenuation?", "Gets current TX2 attenuation [mdB].", "tx2_attenuation?", get_tx2_attenuation, 0, 0, 0, 1},
	{"tx2_attenuation=", "Sets the TX2 attenuation [mdB].", "tx2_attenuation=10000", set_tx2_attenuation, 1, 0, 89750, 1},
	{"tx_fir_en?", "Gets current TX FIR state.", "tx_fir_en?", get_tx_fir_en, 0, 0, 0, 1},
	{"tx_fir_en=", "Sets the TX FIR state.", "tx_fir_en=0", set_tx_fir_en, 1, 0, 1, 1},
	{"rx_lo_freq?", "Gets current RX LO frequency [MHz].", "rx_lo_freq?", get_rx_lo_freq, 0, 0, 0, 1},
	{"rx_lo_freq=", "Sets the RX LO frequency [MHz].", "rx_lo_freq=2400.125", set_rx_lo_freq, 1, 70, 6000, 0},
	{"rx_samp_freq?", "Gets current RX sampling frequency [Hz].", "rx_samp_freq?", get_rx_samp_freq, 0, 0, 0, 1},
	{"rx_samp_freq=", "Sets the RX sampling frequency [Hz].", "rx_samp_freq=30720000", set_rx_samp_freq, 1, 1, 61440000, 1},
	{"rx_rf_bandwidth?", "Gets current RX RF bandwidth [Hz].", "rx_rf_bandwidth?", get_rx_rf_bandwidth, 0, 0, 0, 1},
	{"rx_rf_bandwidth=", "Sets the RX RF bandwidth [Hz].", "rx_rf_bandwidth=18000000", set_rx_rf_bandwidth, 1, 1, 56000000, 1},
	{"rx1_gc_mode?", "Gets current RX1 GC mode.", "rx1_gc_mode?", get_rx1_gc_mode, 0, 0, 0, 1},
	{"rx1_gc_mode=", "Sets the RX1 GC mode.", "rx1_gc_mode=0", set_rx1_gc_mode, 1, 0, 3, 1},
	{"rx2_gc_mode?", "Gets current RX2 GC mode.", "rx2_gc_mode?", get_rx2_gc_mode, 0, 0, 0, 1},
	{"rx2_gc_mode=", "Sets the RX2 GC mode.", "rx2_gc_mode=0", set_rx2_gc_mode, 1, 0, 3, 1},
	{"rx1_rf_gain?", "Gets current RX1 RF gain.", "rx1_rf_gain?", get_rx1_rf_gain, 0, 0, 0, 1},
	{"rx1_rf_gain=", "Sets the RX1 RF gain.", "rx1_rf_gain=20", set_rx1_rf_gain, 1, -128, 127, 1},
	{"rx2_rf_gain?", "Gets current RX2 RF gain.", "rx2_rf_gain?", get_rx2_rf_gain, 0, 0, 0, 1},
	{"rx2_rf_gain=", "Sets the RX2 RF gain.", "rx2_rf_gain=20", set_rx2_rf_gain, 1, -128, 127, 1},
	{"rx_fir_en?", "Gets current RX FIR state.", "rx_fir_en?", get_rx_fir_en, 0, 0, 0, 1},
	{"rx_fir_en=", "Sets the RX FIR state.", "rx_fir_en=0", set_rx_fir_en, 1, 0, 1, 1},
	{"dds_tx1_tone1_freq?", "Gets current DDS TX1 Tone 1 frequency [Hz].", "dds_tx1_tone1_freq?", get_dds_tx1_tone1_freq, 0, 0, 0, 1},
	{"dds_tx1_tone1_freq=", "Sets the DDS TX1 Tone 1 frequency [Hz].", "dds_tx1_tone1_freq=1000000", set_dds_tx1_tone1_freq, 1, 0, 4294967295.0, 1},
	{"dds_tx1_tone2_freq?", "Gets current DDS TX1 Tone 2 frequency [Hz].", "dds_tx1_tone2_freq?", get_dds_tx1_tone2_freq, 0, 0, 0, 1},
	{"dds_tx1_tone2_freq=", "Sets the DDS TX1 Tone 2 frequency [Hz].", "dds_tx1_tone2_freq=1000000", set_dds_tx1_tone2_freq, 1, 0, 4294967295.0, 1},
	{"dds_tx1_tone1_phase?", "Gets current DDS TX1 Tone 1 phase [degrees].", "dds_tx1_tone1_phase?", get_dds_tx1_tone1_phase, 0, 0, 0, 1},
	{"dds_tx1_tone1_phase=", "Sets the DDS TX1 Tone 1 phase [degrees].", "dds_tx1_tone1_phase=90.125", set_dds_tx1_tone1_phase, 1, 0, 360, 0},
	{"dds_tx1_tone2_phase?", "Gets current DDS TX1 Tone 2 phase [degrees].", "dds_tx1_tone2_phase?", get_dds_tx1_tone2_phase, 0, 0, 0, 1},
	{"dds_tx1_tone2_phase=", "Sets the DDS TX1 Tone 2 phase [degrees].", "dds_tx1_tone2_phase=90.125", set_dds_tx1_tone2_phase, 1, 0, 360, 0},
	{"dds_tx1_tone1_scale?", "Gets current DDS TX1 Tone 1 scale.", "dds_tx1_tone1_scale?", get_dds_tx1_tone1_scale, 0, 0, 0, 1},
	{"dds_tx1_tone1_scale=", "Sets the DDS TX1 Tone 1 scale. Integer micro-units; 1000000 = 1.0.", "dds_tx1_tone1_scale=250000", set_dds_tx1_tone1_scale, 1, -1000000, 1000000, 1},
	{"dds_tx1_tone2_scale?", "Gets current DDS TX1 Tone 2 scale.", "dds_tx1_tone2_scale?", get_dds_tx1_tone2_scale, 0, 0, 0, 1},
	{"dds_tx1_tone2_scale=", "Sets the DDS TX1 Tone 2 scale. Integer micro-units; 1000000 = 1.0.", "dds_tx1_tone2_scale=250000", set_dds_tx1_tone2_scale, 1, -1000000, 1000000, 1},
	{"dds_tx2_tone1_freq?", "Gets current DDS TX2 Tone 1 frequency [Hz].", "dds_tx2_tone1_freq?", get_dds_tx2_tone1_freq, 0, 0, 0, 1},
	{"dds_tx2_tone1_freq=", "Sets the DDS TX2 Tone 1 frequency [Hz].", "dds_tx2_tone1_freq=1000000", set_dds_tx2_tone1_freq, 1, 0, 4294967295.0, 1},
	{"dds_tx2_tone2_freq?", "Gets current DDS TX2 Tone 2 frequency [Hz].", "dds_tx2_tone2_freq?", get_dds_tx2_tone2_freq, 0, 0, 0, 1},
	{"dds_tx2_tone2_freq=", "Sets the DDS TX2 Tone 2 frequency [Hz].", "dds_tx2_tone2_freq=1000000", set_dds_tx2_tone2_freq, 1, 0, 4294967295.0, 1},
	{"dds_tx2_tone1_phase?", "Gets current DDS TX2 Tone 1 phase [degrees].", "dds_tx2_tone1_phase?", get_dds_tx2_tone1_phase, 0, 0, 0, 1},
	{"dds_tx2_tone1_phase=", "Sets the DDS TX2 Tone 1 phase [degrees].", "dds_tx2_tone1_phase=90.125", set_dds_tx2_tone1_phase, 1, 0, 360, 0},
	{"dds_tx2_tone2_phase?", "Gets current DDS TX2 Tone 2 phase [degrees].", "dds_tx2_tone2_phase?", get_dds_tx2_tone2_phase, 0, 0, 0, 1},
	{"dds_tx2_tone2_phase=", "Sets the DDS TX2 Tone 2 phase [degrees].", "dds_tx2_tone2_phase=90.125", set_dds_tx2_tone2_phase, 1, 0, 360, 0},
	{"dds_tx2_tone1_scale?", "Gets current DDS TX2 Tone 1 scale.", "dds_tx2_tone1_scale?", get_dds_tx2_tone1_scale, 0, 0, 0, 1},
	{"dds_tx2_tone1_scale=", "Sets the DDS TX2 Tone 1 scale. Integer micro-units; 1000000 = 1.0.", "dds_tx2_tone1_scale=250000", set_dds_tx2_tone1_scale, 1, -1000000, 1000000, 1},
	{"dds_tx2_tone2_scale?", "Gets current DDS TX2 Tone 2 scale.", "dds_tx2_tone2_scale?", dds_tx2_tone2_scale, 0, 0, 0, 1},
	{"dds_tx2_tone2_scale=", "Sets the DDS TX2 Tone 2 scale. Integer micro-units; 1000000 = 1.0.", "dds_tx2_tone2_scale=250000", set_dds_tx2_tone2_scale, 1, -1000000, 1000000, 1},
};
const size_t cmd_no = sizeof(cmd_list) / sizeof(cmd_list[0]);

/******************************************************************************/
/************************ Variables Definitions *******************************/
/******************************************************************************/
extern struct dds_state dds_st[];
extern struct ad9361_rf_phy *ad9361_phy;

/**************************************************************************//***
 * @brief Show the invalid parameter message.
 *
 * @return None.
*******************************************************************************/
void show_invalid_param_message(size_t cmd_no)
{
	console_print("Invalid parameter!\n");
	console_print("%s  - %s\n", (char*)cmd_list[cmd_no].name, (char*)cmd_list[cmd_no].description);
	console_print("Example: %s\n", (char*)cmd_list[cmd_no].example);
}

/**************************************************************************//***
 * @brief Displays all available commands.
 *
 * @return None.
*******************************************************************************/
void get_help(double* param, char param_no) // "help?" command
{
    if (!validate_arguments("help?", param, param_no)) return;
	size_t display_cmd;

	console_print("Available commands:\n");
    sdr_commands_help();
	for(display_cmd = 0; display_cmd < cmd_no; display_cmd++)
	{
		console_print("%s  - %s\n", (char*)cmd_list[display_cmd].name,
								  (char*)cmd_list[display_cmd].description);
	}
}

/**************************************************************************//***
 * @brief Displays all available commands.
 *
 * @return None.
*******************************************************************************/
void get_register(double* param, char param_no) // "register?" command
{
    if (!validate_arguments("register?", param, param_no)) return;
	uint16_t reg_addr;
	int32_t reg_val;
	struct spi_device spi;

	if(param_no >= 1)
	{
		spi.id_no = 0;
		reg_addr = param[0];
		reg_val = ad9361_spi_read(&spi, reg_addr);
        if (report_api_error(reg_val)) return;
		console_print("register[0x%x]=0x%x\n", (unsigned int)reg_addr, (unsigned int)reg_val);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current TX LO frequency [MHz].
 *
 * @return None.
*******************************************************************************/
void get_tx_lo_freq(double* param, char param_no) // "tx_lo_freq?" command
{
    if (!validate_arguments("tx_lo_freq?", param, param_no)) return;
	uint64_t lo_freq_hz;

	if (report_api_error(ad9361_get_tx_lo_freq(ad9361_phy, &lo_freq_hz))) return;
	console_print("tx_lo_freq=%.6f\n", (double)lo_freq_hz / 1000000.0);
}

/**************************************************************************//***
 * @brief Sets the TX LO frequency [MHz].
 *
 * @return None.
*******************************************************************************/
void set_tx_lo_freq(double* param, char param_no) // "tx_lo_freq=" command
{
    if (!validate_arguments("tx_lo_freq=", param, param_no)) return;
	uint64_t lo_freq_hz;

	if(param_no >= 1)
	{
		lo_freq_hz = (uint64_t)(param[0] * 1000000.0 + 0.5);
		if (report_api_error(ad9361_set_tx_lo_freq(ad9361_phy, lo_freq_hz))) return;
        if (report_api_error(ad9361_get_tx_lo_freq(ad9361_phy, &lo_freq_hz))) return;
		console_print("tx_lo_freq=%.6f\n", (double)lo_freq_hz / 1000000.0);
	}
}

/**************************************************************************//***
 * @brief Gets current sampling frequency [Hz].
 *
 * @return None.
*******************************************************************************/
void get_tx_samp_freq(double* param, char param_no) // "tx_samp_freq?" command
{
    if (!validate_arguments("tx_samp_freq?", param, param_no)) return;
	uint32_t sampling_freq_hz;

	if (report_api_error(ad9361_get_tx_sampling_freq(ad9361_phy, &sampling_freq_hz))) return;
	console_print("tx_samp_freq=%lu\n", (unsigned long)sampling_freq_hz);
}

/**************************************************************************//***
 * @brief Sets the sampling frequency [Hz].
 *
 * @return None.
*******************************************************************************/
void set_tx_samp_freq(double* param, char param_no) // "tx_samp_freq=" command
{
    if (!validate_arguments("tx_samp_freq=", param, param_no)) return;
	uint32_t sampling_freq_hz;

	if(param_no >= 1)
	{
		sampling_freq_hz = (uint32_t)param[0];
		if (report_api_error(ad9361_set_tx_sampling_freq(ad9361_phy, sampling_freq_hz))) return;
		if (report_api_error(ad9361_get_tx_sampling_freq(ad9361_phy, &sampling_freq_hz))) return;
		dds_update(ad9361_phy);
		console_print("tx_samp_freq=%lu\n", (unsigned long)sampling_freq_hz);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current TX RF bandwidth [Hz].
 *
 * @return None.
*******************************************************************************/
void get_tx_rf_bandwidth(double* param, char param_no) // "tx_rf_bandwidth?" command
{
    if (!validate_arguments("tx_rf_bandwidth?", param, param_no)) return;
	uint32_t bandwidth_hz;

	if (report_api_error(ad9361_get_tx_rf_bandwidth(ad9361_phy, &bandwidth_hz))) return;
	console_print("tx_rf_bandwidth=%lu\n", (unsigned long)bandwidth_hz);
}

/**************************************************************************//***
 * @brief Sets the TX RF bandwidth [Hz].
 *
 * @return None.
*******************************************************************************/
void set_tx_rf_bandwidth(double* param, char param_no) // "tx_rf_bandwidth=" command
{
    if (!validate_arguments("tx_rf_bandwidth=", param, param_no)) return;
	uint32_t bandwidth_hz;

	if(param_no >= 1)
	{
		bandwidth_hz = param[0];
		if (report_api_error(ad9361_set_tx_rf_bandwidth(ad9361_phy, bandwidth_hz))) return;
        if (report_api_error(ad9361_get_tx_rf_bandwidth(ad9361_phy, &bandwidth_hz))) return;
        console_print("tx_rf_bandwidth=%lu\n", (unsigned long)bandwidth_hz);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current TX1 attenuation [mdB].
 *
 * @return None.
*******************************************************************************/
void get_tx1_attenuation(double* param, char param_no) // "tx1_attenuation?" command
{
    if (!validate_arguments("tx1_attenuation?", param, param_no)) return;
	uint32_t attenuation_mdb;

	if (report_api_error(sdr_get_attenuation(0, &attenuation_mdb))) return;
	console_print("tx1_attenuation=%lu\n", (unsigned long)attenuation_mdb);
}

/**************************************************************************//***
 * @brief Sets the TX1 attenuation [mdB].
 *
 * @return None.
*******************************************************************************/
void set_tx1_attenuation(double* param, char param_no) // "tx1_attenuation=" command
{
    if (!validate_arguments("tx1_attenuation=", param, param_no)) return;
	uint32_t attenuation_mdb;

	if(param_no >= 1)
	{
		attenuation_mdb = param[0];
		if (report_api_error(sdr_set_attenuation(0, attenuation_mdb))) return;
        if (report_api_error(sdr_get_attenuation(0, &attenuation_mdb))) return;
		console_print("tx1_attenuation=%lu\n", (unsigned long)attenuation_mdb);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current TX2 attenuation [mdB].
 *
 * @return None.
*******************************************************************************/
void get_tx2_attenuation(double* param, char param_no) // "tx1_attenuation?" command
{
    if (!validate_arguments("tx2_attenuation?", param, param_no)) return;
	uint32_t attenuation_mdb;

	if (report_api_error(sdr_get_attenuation(1, &attenuation_mdb))) return;
	console_print("tx2_attenuation=%lu\n", (unsigned long)attenuation_mdb);
}

/**************************************************************************//***
 * @brief Sets the TX2 attenuation [mdB].
 *
 * @return None.
*******************************************************************************/
void set_tx2_attenuation(double* param, char param_no) // "tx1_attenuation=" command
{
    if (!validate_arguments("tx2_attenuation=", param, param_no)) return;
	uint32_t attenuation_mdb;

	if(param_no >= 1)
	{
		attenuation_mdb = param[0];
		if (report_api_error(sdr_set_attenuation(1, attenuation_mdb))) return;
        if (report_api_error(sdr_get_attenuation(1, &attenuation_mdb))) return;
		console_print("tx2_attenuation=%lu\n", (unsigned long)attenuation_mdb);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current TX FIR state.
 *
 * @return None.
*******************************************************************************/
void get_tx_fir_en(double* param, char param_no) // "tx_fir_en?" command
{
    if (!validate_arguments("tx_fir_en?", param, param_no)) return;
	uint8_t en_dis;

	if (report_api_error(ad9361_get_tx_fir_en_dis(ad9361_phy, &en_dis))) return;
	console_print("tx_fir_en=%d\n", en_dis);
}

/**************************************************************************//***
 * @brief Sets the TX FIR state.
 *
 * @return None.
*******************************************************************************/
void set_tx_fir_en(double* param, char param_no) // "tx_fir_en=" command
{
    if (!validate_arguments("tx_fir_en=", param, param_no)) return;
	uint8_t en_dis;

	if(param_no >= 1)
	{
		en_dis = param[0];
		if (report_api_error(ad9361_set_tx_fir_en_dis(ad9361_phy, en_dis))) return;
		console_print("tx_fir_en=%d\n", en_dis);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current RX LO frequency [MHz].
 *
 * @return None.
*******************************************************************************/
void get_rx_lo_freq(double* param, char param_no) // "rx_lo_freq?" command
{
    if (!validate_arguments("rx_lo_freq?", param, param_no)) return;
	uint64_t lo_freq_hz;

	if (report_api_error(ad9361_get_rx_lo_freq(ad9361_phy, &lo_freq_hz))) return;
	console_print("rx_lo_freq=%.6f\n", (double)lo_freq_hz / 1000000.0);
}

/**************************************************************************//***
 * @brief Sets the RX LO frequency [MHz].
 *
 * @return None.
*******************************************************************************/
void set_rx_lo_freq(double* param, char param_no) // "rx_lo_freq=" command
{
    if (!validate_arguments("rx_lo_freq=", param, param_no)) return;
	uint64_t lo_freq_hz;

	if(param_no >= 1)
	{
		lo_freq_hz = (uint64_t)(param[0] * 1000000.0 + 0.5);
		if (report_api_error(ad9361_set_rx_lo_freq(ad9361_phy, lo_freq_hz))) return;
        if (report_api_error(ad9361_get_rx_lo_freq(ad9361_phy, &lo_freq_hz))) return;
		console_print("rx_lo_freq=%.6f\n", (double)lo_freq_hz / 1000000.0);
	}
}

/**************************************************************************//***
 * @brief Gets current RX sampling frequency [Hz].
 *
 * @return None.
*******************************************************************************/
void get_rx_samp_freq(double* param, char param_no) // "rx_samp_freq?" command
{
    if (!validate_arguments("rx_samp_freq?", param, param_no)) return;
	uint32_t sampling_freq_hz;

	if (report_api_error(ad9361_get_rx_sampling_freq(ad9361_phy, &sampling_freq_hz))) return;
	console_print("rx_samp_freq=%lu\n", (unsigned long)sampling_freq_hz);
}

/**************************************************************************//***
 * @brief Sets the RX sampling frequency [Hz].
 *
 * @return None.
*******************************************************************************/
void set_rx_samp_freq(double* param, char param_no) // "rx_samp_freq=" command
{
    if (!validate_arguments("rx_samp_freq=", param, param_no)) return;
	uint32_t sampling_freq_hz;

	if(param_no >= 1)
	{
		sampling_freq_hz = (uint32_t)param[0];
		if (report_api_error(ad9361_set_rx_sampling_freq(ad9361_phy, sampling_freq_hz))) return;
		if (report_api_error(ad9361_get_rx_sampling_freq(ad9361_phy, &sampling_freq_hz))) return;
		dds_update(ad9361_phy);
		console_print("rx_samp_freq=%lu\n", (unsigned long)sampling_freq_hz);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current RX RF bandwidth [Hz].
 *
 * @return None.
*******************************************************************************/
void get_rx_rf_bandwidth(double* param, char param_no) // "rx_rf_bandwidth?" command
{
    if (!validate_arguments("rx_rf_bandwidth?", param, param_no)) return;
	uint32_t bandwidth_hz;

	if (report_api_error(ad9361_get_rx_rf_bandwidth(ad9361_phy, &bandwidth_hz))) return;
	console_print("rx_rf_bandwidth=%lu\n", (unsigned long)bandwidth_hz);
}

/**************************************************************************//***
 * @brief Sets the RX RF bandwidth [Hz].
 *
 * @return None.
*******************************************************************************/
void set_rx_rf_bandwidth(double* param, char param_no) // "rx_rf_bandwidth=" command
{
    if (!validate_arguments("rx_rf_bandwidth=", param, param_no)) return;
	uint32_t bandwidth_hz;

	if(param_no >= 1)
	{
		bandwidth_hz = param[0];
		if (report_api_error(ad9361_set_rx_rf_bandwidth(ad9361_phy, bandwidth_hz))) return;
        if (report_api_error(ad9361_get_rx_rf_bandwidth(ad9361_phy, &bandwidth_hz))) return;
		console_print("rx_rf_bandwidth=%lu\n", (unsigned long)bandwidth_hz);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current RX1 GC mode.
 *
 * @return None.
*******************************************************************************/
void get_rx1_gc_mode(double* param, char param_no) // "rx1_gc_mode?" command
{
    if (!validate_arguments("rx1_gc_mode?", param, param_no)) return;
	uint8_t gc_mode;

	if (report_api_error(ad9361_get_rx_gain_control_mode(ad9361_phy, 0, &gc_mode))) return;
	console_print("rx1_gc_mode=%d\n", gc_mode);
}

/**************************************************************************//***
 * @brief Sets the RX1 GC mode.
 *
 * @return None.
*******************************************************************************/
void set_rx1_gc_mode(double* param, char param_no) // "rx1_gc_mode=" command
{
    if (!validate_arguments("rx1_gc_mode=", param, param_no)) return;
	uint8_t gc_mode;

	if(param_no >= 1)
	{
		gc_mode = param[0];
		if (report_api_error(ad9361_set_rx_gain_control_mode(ad9361_phy, 0, gc_mode))) return;
		console_print("rx1_gc_mode=%d\n", gc_mode);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current RX2 GC mode.
 *
 * @return None.
*******************************************************************************/
void get_rx2_gc_mode(double* param, char param_no) // "rx2_gc_mode?" command
{
    if (!validate_arguments("rx2_gc_mode?", param, param_no)) return;
	uint8_t gc_mode;

	if (report_api_error(ad9361_get_rx_gain_control_mode(ad9361_phy, 1, &gc_mode))) return;
	console_print("rx2_gc_mode=%d\n", gc_mode);
}

/**************************************************************************//***
 * @brief Sets the RX2 GC mode.
 *
 * @return None.
*******************************************************************************/
void set_rx2_gc_mode(double* param, char param_no) // "rx2_gc_mode=" command
{
    if (!validate_arguments("rx2_gc_mode=", param, param_no)) return;
	uint8_t gc_mode;

	if(param_no >= 1)
	{
		gc_mode = param[0];
		if (report_api_error(ad9361_set_rx_gain_control_mode(ad9361_phy, 1, gc_mode))) return;
		console_print("rx2_gc_mode=%d\n", gc_mode);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current RX1 RF gain.
 *
 * @return None.
*******************************************************************************/
void get_rx1_rf_gain(double* param, char param_no) // "rx1_rf_gain?" command
{
    if (!validate_arguments("rx1_rf_gain?", param, param_no)) return;
	int32_t gain_db;

	if (report_api_error(ad9361_get_rx_rf_gain (ad9361_phy, 0, &gain_db))) return;
	console_print("rx1_rf_gain=%ld\n", (long)gain_db);
}

/**************************************************************************//***
 * @brief Sets the RX1 RF gain.
 *
 * @return None.
*******************************************************************************/
void set_rx1_rf_gain(double* param, char param_no) // "rx1_rf_gain=" command
{
    if (!validate_arguments("rx1_rf_gain=", param, param_no)) return;
	int32_t gain_db;

	if(param_no >= 1)
	{
		gain_db = param[0];
		if (report_api_error(ad9361_set_rx_rf_gain (ad9361_phy, 0, gain_db))) return;
		console_print("rx1_rf_gain=%ld\n", (long)gain_db);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current RX2 RF gain.
 *
 * @return None.
*******************************************************************************/
void get_rx2_rf_gain(double* param, char param_no) // "rx2_rf_gain?" command
{
    if (!validate_arguments("rx2_rf_gain?", param, param_no)) return;
	int32_t gain_db;

	if (report_api_error(ad9361_get_rx_rf_gain (ad9361_phy, 1, &gain_db))) return;
	console_print("rx2_rf_gain=%ld\n", (long)gain_db);
}

/**************************************************************************//***
 * @brief Sets the RX2 RF gain.
 *
 * @return None.
*******************************************************************************/
void set_rx2_rf_gain(double* param, char param_no) // "rx2_rf_gain=" command
{
    if (!validate_arguments("rx2_rf_gain=", param, param_no)) return;
	int32_t gain_db;

	if(param_no >= 1)
	{
		gain_db = param[0];
		if (report_api_error(ad9361_set_rx_rf_gain (ad9361_phy, 1, gain_db))) return;
		console_print("rx2_rf_gain=%ld\n", (long)gain_db);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current RX FIR state.
 *
 * @return None.
*******************************************************************************/
void get_rx_fir_en(double* param, char param_no) // "rx_fir_en?" command
{
    if (!validate_arguments("rx_fir_en?", param, param_no)) return;
	uint8_t en_dis;

	if (report_api_error(ad9361_get_rx_fir_en_dis(ad9361_phy, &en_dis))) return;
	console_print("rx_fir_en=%d\n", en_dis);
}

/**************************************************************************//***
 * @brief Sets the RX FIR state.
 *
 * @return None.
*******************************************************************************/
void set_rx_fir_en(double* param, char param_no) // "rx_fir_en=" command
{
    if (!validate_arguments("rx_fir_en=", param, param_no)) return;
	uint8_t en_dis;

	if(param_no >= 1)
	{
		en_dis = param[0];
		if (report_api_error(ad9361_set_rx_fir_en_dis(ad9361_phy, en_dis))) return;
		console_print("rx_fir_en=%d\n", en_dis);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current DDS TX1 Tone 1 frequency [Hz].
 *
 * @return None.
*******************************************************************************/
void get_dds_tx1_tone1_freq(double* param, char param_no)	// dds_tx1_tone1_freq?
{
    if (!validate_arguments("dds_tx1_tone1_freq?", param, param_no)) return;
	uint32_t freq = dds_st[ad9361_phy->id_no].cached_freq[DDS_CHAN_TX1_I_F1];

	console_print("dds_tx1_tone1_freq=%lu\n", (unsigned long)freq);
}

/**************************************************************************//***
 * @brief Sets the DDS TX1 Tone 1 frequency [Hz].
 *
 * @return None.
*******************************************************************************/
void set_dds_tx1_tone1_freq(double* param, char param_no)	// dds_tx1_tone1_freq=
{
    if (!validate_arguments("dds_tx1_tone1_freq=", param, param_no)) return;
	uint32_t freq = (uint32_t)param[0];

	if(param_no >= 1)
	{
		dds_set_frequency(ad9361_phy, DDS_CHAN_TX1_I_F1, freq);
		dds_set_frequency(ad9361_phy, DDS_CHAN_TX1_Q_F1, freq);
		console_print("dds_tx1_tone1_freq=%lu\n", (unsigned long)freq);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current DDS TX1 Tone 2 frequency [Hz].
 *
 * @return None.
*******************************************************************************/
void get_dds_tx1_tone2_freq(double* param, char param_no)	// dds_tx1_tone2_freq?
{
    if (!validate_arguments("dds_tx1_tone2_freq?", param, param_no)) return;
	uint32_t freq = dds_st[ad9361_phy->id_no].cached_freq[DDS_CHAN_TX1_I_F2];

	console_print("dds_tx1_tone2_freq=%lu\n", (unsigned long)freq);
}

/**************************************************************************//***
 * @brief Sets the DDS TX1 Tone 2 frequency [Hz].
 *
 * @return None.
*******************************************************************************/
void set_dds_tx1_tone2_freq(double* param, char param_no)	// dds_tx1_tone2_freq=
{
    if (!validate_arguments("dds_tx1_tone2_freq=", param, param_no)) return;
	uint32_t freq = (uint32_t)param[0];

	if(param_no >= 1)
	{
		dds_set_frequency(ad9361_phy, DDS_CHAN_TX1_I_F2, freq);
		dds_set_frequency(ad9361_phy, DDS_CHAN_TX1_Q_F2, freq);
		console_print("dds_tx1_tone2_freq=%lu\n", (unsigned long)freq);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current DDS TX1 Tone 1 phase [degrees].
 *
 * @return None.
*******************************************************************************/
void get_dds_tx1_tone1_phase(double* param, char param_no)	// dds_tx1_tone1_phase?
{
    if (!validate_arguments("dds_tx1_tone1_phase?", param, param_no)) return;
	uint32_t phase = dds_st[ad9361_phy->id_no].cached_phase[DDS_CHAN_TX1_I_F1];

	console_print("dds_tx1_tone1_phase=%.3f\n", (double)phase / 1000.0);
}

/**************************************************************************//***
 * @brief Sets the DDS TX1 Tone 1 phase [degrees].
 *
 * @return None.
*******************************************************************************/
void set_dds_tx1_tone1_phase(double* param, char param_no)	// dds_tx1_tone1_phase=
{
    if (!validate_arguments("dds_tx1_tone1_phase=", param, param_no)) return;
	uint32_t phase = (uint32_t)(param[0] * 1000.0 + 0.5) % 360000U;

	if(param_no >= 1)
	{
		dds_set_phase(ad9361_phy, DDS_CHAN_TX1_I_F1, phase);
		dds_set_phase(ad9361_phy, DDS_CHAN_TX1_Q_F1, (phase + 270000U) % 360000U);
		phase = dds_st[ad9361_phy->id_no].cached_phase[DDS_CHAN_TX1_I_F1];
        console_print("dds_tx1_tone1_phase=%.3f\n", (double)phase / 1000.0);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current DDS TX1 Tone 2 phase [degrees].
 *
 * @return None.
*******************************************************************************/
void get_dds_tx1_tone2_phase(double* param, char param_no)	// dds_tx1_tone2_phase?
{
    if (!validate_arguments("dds_tx1_tone2_phase?", param, param_no)) return;
	uint32_t phase = dds_st[ad9361_phy->id_no].cached_phase[DDS_CHAN_TX1_I_F2];

	console_print("dds_tx1_tone2_phase=%.3f\n", (double)phase / 1000.0);
}

/**************************************************************************//***
 * @brief Sets the DDS TX1 Tone 2 phase [degrees].
 *
 * @return None.
*******************************************************************************/
void set_dds_tx1_tone2_phase(double* param, char param_no)	// dds_tx1_tone2_phase=
{
    if (!validate_arguments("dds_tx1_tone2_phase=", param, param_no)) return;
	uint32_t phase = (uint32_t)(param[0] * 1000.0 + 0.5) % 360000U;

	if(param_no >= 1)
	{
		dds_set_phase(ad9361_phy, DDS_CHAN_TX1_I_F2, phase);
		dds_set_phase(ad9361_phy, DDS_CHAN_TX1_Q_F2, (phase + 270000U) % 360000U);
		phase = dds_st[ad9361_phy->id_no].cached_phase[DDS_CHAN_TX1_I_F2];
        console_print("dds_tx1_tone2_phase=%.3f\n", (double)phase / 1000.0);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current DDS TX1 Tone 1 scale.
 *
 * @return None.
*******************************************************************************/
void get_dds_tx1_tone1_scale(double* param, char param_no)	// dds_tx1_tone1_scale?
{
    if (!validate_arguments("dds_tx1_tone1_scale?", param, param_no)) return;
	int32_t scale = dds_st[ad9361_phy->id_no].cached_scale[DDS_CHAN_TX1_I_F1];

	console_print("dds_tx1_tone1_scale=%ld\n", (long)scale);
}

/**************************************************************************//***
 * @brief Sets the DDS TX1 Tone 1 scale.
 *
 * @return None.
*******************************************************************************/
void set_dds_tx1_tone1_scale(double* param, char param_no)	// dds_tx1_tone1_scale=
{
    if (!validate_arguments("dds_tx1_tone1_scale=", param, param_no)) return;
	int32_t scale = (int32_t)param[0];

	if(param_no >= 1)
	{
		dds_set_scale(ad9361_phy, DDS_CHAN_TX1_I_F1, scale);
		dds_set_scale(ad9361_phy, DDS_CHAN_TX1_Q_F1, scale);
		scale = dds_st[ad9361_phy->id_no].cached_scale[DDS_CHAN_TX1_I_F1];
		console_print("dds_tx1_tone1_scale=%ld\n", (long)scale);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current DDS TX1 Tone 2 scale.
 *
 * @return None.
*******************************************************************************/
void get_dds_tx1_tone2_scale(double* param, char param_no)	// dds_tx1_tone2_scale?
{
    if (!validate_arguments("dds_tx1_tone2_scale?", param, param_no)) return;
	int32_t scale = dds_st[ad9361_phy->id_no].cached_scale[DDS_CHAN_TX1_I_F2];

	console_print("dds_tx1_tone2_scale=%ld\n", (long)scale);
}

/**************************************************************************//***
 * @brief Sets the DDS TX1 Tone 2 scale.
 *
 * @return None.
*******************************************************************************/
void set_dds_tx1_tone2_scale(double* param, char param_no)	// dds_tx1_tone2_scale=
{
    if (!validate_arguments("dds_tx1_tone2_scale=", param, param_no)) return;
	int32_t scale = (int32_t)param[0];

	if(param_no >= 1)
	{
		dds_set_scale(ad9361_phy, DDS_CHAN_TX1_I_F2, scale);
		dds_set_scale(ad9361_phy, DDS_CHAN_TX1_Q_F2, scale);
		scale = dds_st[ad9361_phy->id_no].cached_scale[DDS_CHAN_TX1_I_F2];
		console_print("dds_tx1_tone2_scale=%ld\n", (long)scale);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current DDS TX2 Tone 1 frequency [Hz].
 *
 * @return None.
*******************************************************************************/
void get_dds_tx2_tone1_freq(double* param, char param_no)	// dds_tx2_tone1_freq?
{
    if (!validate_arguments("dds_tx2_tone1_freq?", param, param_no)) return;
	uint32_t freq = dds_st[ad9361_phy->id_no].cached_freq[DDS_CHAN_TX2_I_F1];

	console_print("dds_tx2_tone1_freq=%lu\n", (unsigned long)freq);
}

/**************************************************************************//***
 * @brief Sets the DDS TX2 Tone 1 frequency [Hz].
 *
 * @return None.
*******************************************************************************/
void set_dds_tx2_tone1_freq(double* param, char param_no)	// dds_tx2_tone1_freq=
{
    if (!validate_arguments("dds_tx2_tone1_freq=", param, param_no)) return;
	uint32_t freq = (uint32_t)param[0];

	if(param_no >= 1)
	{
		dds_set_frequency(ad9361_phy, DDS_CHAN_TX2_I_F1, freq);
		dds_set_frequency(ad9361_phy, DDS_CHAN_TX2_Q_F1, freq);
		console_print("dds_tx2_tone1_freq=%lu\n", (unsigned long)freq);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current DDS TX2 Tone 2 frequency [Hz].
 *
 * @return None.
*******************************************************************************/
void get_dds_tx2_tone2_freq(double* param, char param_no)	// dds_tx2_tone2_freq?
{
    if (!validate_arguments("dds_tx2_tone2_freq?", param, param_no)) return;
	uint32_t freq = dds_st[ad9361_phy->id_no].cached_freq[DDS_CHAN_TX2_I_F2];

	console_print("dds_tx2_tone2_freq=%lu\n", (unsigned long)freq);
}

/**************************************************************************//***
 * @brief Sets the DDS TX2 Tone 2 frequency [Hz].
 *
 * @return None.
*******************************************************************************/
void set_dds_tx2_tone2_freq(double* param, char param_no)	// dds_tx2_tone2_freq=
{
    if (!validate_arguments("dds_tx2_tone2_freq=", param, param_no)) return;
	uint32_t freq = (uint32_t)param[0];

	if(param_no >= 1)
	{
		dds_set_frequency(ad9361_phy, DDS_CHAN_TX2_I_F2, freq);
		dds_set_frequency(ad9361_phy, DDS_CHAN_TX2_Q_F2, freq);
		console_print("dds_tx2_tone2_freq=%lu\n", (unsigned long)freq);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current DDS TX2 Tone 1 phase [degrees].
 *
 * @return None.
*******************************************************************************/
void get_dds_tx2_tone1_phase(double* param, char param_no)	// dds_tx2_tone1_phase?
{
    if (!validate_arguments("dds_tx2_tone1_phase?", param, param_no)) return;
	uint32_t phase = dds_st[ad9361_phy->id_no].cached_phase[DDS_CHAN_TX2_I_F1];

	console_print("dds_tx2_tone1_phase=%.3f\n", (double)phase / 1000.0);
}

/**************************************************************************//***
 * @brief Sets the DDS TX2 Tone 1 phase [degrees].
 *
 * @return None.
*******************************************************************************/
void set_dds_tx2_tone1_phase(double* param, char param_no)	// dds_tx2_tone1_phase=
{
    if (!validate_arguments("dds_tx2_tone1_phase=", param, param_no)) return;
	uint32_t phase = (uint32_t)(param[0] * 1000.0 + 0.5) % 360000U;

	if(param_no >= 1)
	{
		dds_set_phase(ad9361_phy, DDS_CHAN_TX2_I_F1, phase);
		dds_set_phase(ad9361_phy, DDS_CHAN_TX2_Q_F1, (phase + 270000U) % 360000U);
		phase = dds_st[ad9361_phy->id_no].cached_phase[DDS_CHAN_TX2_I_F1];
        console_print("dds_tx2_tone1_phase=%.3f\n", (double)phase / 1000.0);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current DDS TX2 Tone 2 phase [degrees].
 *
 * @return None.
*******************************************************************************/
void get_dds_tx2_tone2_phase(double* param, char param_no)	// dds_tx2_tone2_phase?
{
    if (!validate_arguments("dds_tx2_tone2_phase?", param, param_no)) return;
	uint32_t phase = dds_st[ad9361_phy->id_no].cached_phase[DDS_CHAN_TX2_I_F2];

	console_print("dds_tx2_tone2_phase=%.3f\n", (double)phase / 1000.0);
}

/**************************************************************************//***
 * @brief Sets the DDS TX2 Tone 2 phase [degrees].
 *
 * @return None.
*******************************************************************************/
void set_dds_tx2_tone2_phase(double* param, char param_no)	// dds_tx2_tone2_phase=
{
    if (!validate_arguments("dds_tx2_tone2_phase=", param, param_no)) return;
	uint32_t phase = (uint32_t)(param[0] * 1000.0 + 0.5) % 360000U;

	if(param_no >= 1)
	{
		dds_set_phase(ad9361_phy, DDS_CHAN_TX2_I_F2, phase);
		dds_set_phase(ad9361_phy, DDS_CHAN_TX2_Q_F2, (phase + 270000U) % 360000U);
		phase = dds_st[ad9361_phy->id_no].cached_phase[DDS_CHAN_TX2_I_F2];
        console_print("dds_tx2_tone2_phase=%.3f\n", (double)phase / 1000.0);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current DDS TX2 Tone 1 scale.
 *
 * @return None.
*******************************************************************************/
void get_dds_tx2_tone1_scale(double* param, char param_no)	// dds_tx2_tone1_scale?
{
    if (!validate_arguments("dds_tx2_tone1_scale?", param, param_no)) return;
	int32_t scale = dds_st[ad9361_phy->id_no].cached_scale[DDS_CHAN_TX2_I_F1];

	console_print("dds_tx2_tone1_scale=%ld\n", (long)scale);
}

/**************************************************************************//***
 * @brief Sets the DDS TX2 Tone 1 scale.
 *
 * @return None.
*******************************************************************************/
void set_dds_tx2_tone1_scale(double* param, char param_no)	// dds_tx2_tone1_scale=
{
    if (!validate_arguments("dds_tx2_tone1_scale=", param, param_no)) return;
	int32_t scale = (int32_t)param[0];

	if(param_no >= 1)
	{
		dds_set_scale(ad9361_phy, DDS_CHAN_TX2_I_F1, scale);
		dds_set_scale(ad9361_phy, DDS_CHAN_TX2_Q_F1, scale);
		scale = dds_st[ad9361_phy->id_no].cached_scale[DDS_CHAN_TX2_I_F1];
		console_print("dds_tx2_tone1_scale=%ld\n", (long)scale);
	}
	else
		show_invalid_param_message(1);
}

/**************************************************************************//***
 * @brief Gets current DDS TX2 Tone 2 scale.
 *
 * @return None.
*******************************************************************************/
void dds_tx2_tone2_scale(double* param, char param_no)	// dds_tx2_tone2_scale?
{
    if (!validate_arguments("dds_tx2_tone2_scale?", param, param_no)) return;
	int32_t scale = dds_st[ad9361_phy->id_no].cached_scale[DDS_CHAN_TX2_I_F2];

	console_print("dds_tx2_tone2_scale=%ld\n", (long)scale);
}

/**************************************************************************//***
 * @brief Sets the DDS TX2 Tone 2 scale.
 *
 * @return None.
*******************************************************************************/
void set_dds_tx2_tone2_scale(double* param, char param_no)	// dds_tx2_tone2_scale=
{
    if (!validate_arguments("dds_tx2_tone2_scale=", param, param_no)) return;
	int32_t scale = (int32_t)param[0];

	if(param_no >= 1)
	{
		dds_set_scale(ad9361_phy, DDS_CHAN_TX2_I_F2, scale);
		dds_set_scale(ad9361_phy, DDS_CHAN_TX2_Q_F2, scale);
		scale = dds_st[ad9361_phy->id_no].cached_scale[DDS_CHAN_TX2_I_F2];
		console_print("dds_tx2_tone2_scale=%ld\n", (long)scale);
	}
	else
		show_invalid_param_message(1);
}

int command_init(void) { return sdr_control_init(); }
void command_service(void) { sdr_control_service(); }

static int report_api_error(int result)
{
    if (result < 0) {
        console_print("Error: AD9361 operation failed (%d).\n", result);
        return 1;
    }
    return 0;
}

static int validate_arguments(const char *name, double *param, char count)
{
    size_t index, p;
    const command *entry = NULL;
    for (index = 0; index < cmd_no; ++index) {
        if (strcmp(cmd_list[index].name, name) == 0) {
            entry = &cmd_list[index];
            break;
        }
    }
    if (entry == NULL) return 0;
    if (count != entry->parameter_count || (count && param == NULL)) {
        show_invalid_param_message(index);
        return 0;
    }
    for (p = 0; p < (size_t)count; ++p) {
        if (!isfinite(param[p]) || param[p] < entry->minimum ||
            param[p] > entry->maximum ||
            (entry->integer_only && param[p] != (double)(int64_t)param[p])) {
            console_print("Invalid parameter: %s requires %s in [%g, %g].\n",
                          name, entry->integer_only ? "integers" : "numbers",
                          entry->minimum, entry->maximum);
            return 0;
        }
    }
    if ((strcmp(name, "ch32_tx=") == 0 && param[3] && !param[0]) ||
        (strcmp(name, "ch32_config=") == 0 &&
         ((u32)param[0] & CH32H417_IP_TX_RUN) &&
         !((u32)param[0] & CH32H417_IP_TX_EN))) {
        console_print("Invalid parameter: TX_RUN requires TX_EN.\n");
        return 0;
    }
    if (!sdr_guard(name)) return 0;
    if (strstr(name, "_attenuation=") && ((uint32_t)param[0] % 250U)) {
        console_print("Invalid attenuation: step is 250 mdB.\n"); return 0;
    }
    /* Never allow the DDS phase increment to wrap silently. */
    if (strncmp(name, "dds_", 4) == 0 && strstr(name, "_freq=") != NULL) {
        uint32_t sample_rate;
        if (report_api_error(ad9361_get_tx_sampling_freq(ad9361_phy, &sample_rate)))
            return 0;
        if (sample_rate == 0 || param[0] > (double)sample_rate / 2.0) {
            console_print("Invalid DDS frequency: use 0..%u Hz.\n", (unsigned int)(sample_rate / 2U));
            return 0;
        }
    }
    return 1;
}

static int command_execute_impl(const char *line)
{
    size_t index;
    double param[CONSOLE_MAX_PARAMS] = {0};
    char count = 0;
    int type;
    if (line == NULL) return -1;
    while (*line == ' ' || *line == '\t') ++line;
    if (*line == '\0') return 0;
    type = console_check_commands(line, "host_protocol?", param, &count,
                                  CONSOLE_MAX_PARAMS);
    if (type != UNKNOWN_CMD) {
        if (type == INVALID_CMD || count != 0) {
            console_print("Invalid host_protocol query.\n");
            return -1;
        }
        console_print("host_protocol=1 envelope=@id completion=RSP events=EVT\n");
        return 0;
    }
    type = sdr_commands_execute(line);
    if (type) return type < 0 ? -1 : 0;
    for (index = 0; index < cmd_no; ++index) {
        type = console_check_commands(line, cmd_list[index].name,
                                      param, &count, CONSOLE_MAX_PARAMS);
        if (type == UNKNOWN_CMD) continue;
        if (type == INVALID_CMD) {
            console_print("Invalid command parameters or number format.\n");
            return -1;
        }
        if (!validate_arguments(cmd_list[index].name, param, count)) return -1;
        /* Callbacks also validate when invoked directly from other C code. */
        cmd_list[index].function(param, count);
        return 0;
    }
    console_print("Invalid command! Use help?\n");
    return -1;
}

/* Optional, backward-compatible host transaction envelope. The completion
 * record is emitted after the synchronous command returns; asynchronous EVT
 * records remain distinguishable and do not mark command completion. */
int command_execute(const char *line)
{
    const char *body;
    uint32_t id = 0;
    int result;
    size_t length = 0;
    if (line == NULL) return -1;
    while (length < CONSOLE_LINE_CAPACITY && line[length]) ++length;
    if (length == CONSOLE_LINE_CAPACITY) {
        console_print("Invalid command line length.\n");
        return -1;
    }
    while (*line == ' ' || *line == '\t') ++line;
    if (*line != '@') return command_execute_impl(line);
    ++line;
    if (*line < '0' || *line > '9') goto invalid_envelope;
    do {
        const uint32_t digit = (uint32_t)(*line - '0');
        if (id > (UINT32_MAX - digit) / 10U) goto invalid_envelope;
        id = id * 10U + digit;
        ++line;
    } while (*line >= '0' && *line <= '9');
    if (id == 0 || (*line != ' ' && *line != '\t')) goto invalid_envelope;
    body = line;
    while (*body == ' ' || *body == '\t') ++body;
    if (*body == '\0' || *body == '\r' || *body == '\n' || *body == '@')
        goto invalid_envelope;
    /* A single envelope contains exactly one console command. Permit only a
     * trailing CR/LF pair for direct callers; never dispatch embedded lines. */
    line = body;
    while (*line && *line != '\r' && *line != '\n') ++line;
    if (*line == '\r') ++line;
    if (*line == '\n') ++line;
    if (*line) goto invalid_envelope;
    console_print("RSP id=%lu begin\n", (unsigned long)id);
    result = command_execute_impl(body);
    console_print("RSP id=%lu end result=%d\n", (unsigned long)id, result);
    return result;
invalid_envelope:
    console_print("Invalid host envelope: use @<nonzero uint32 id> <command>.\n");
    return -1;
}

