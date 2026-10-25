/***************************************************************************/ /**
 *   @file   board_config.c
 *   @brief  Board defaults derived from the ADI example.
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
#include "parameters.h"
#include "platform.h"

/* 鍗曠墖 AD9361锛�40 MHz 鍙傝�冦��2R2T LVDS銆�30.72 MSPS锛孯X/TX 榛樿鍐呴儴 LO銆�
 * 淇濈暀宸查獙璇佺殑榛樿鍙傛暟锛涘弬鑰冩簮銆丳PS銆乀X/USB 闂ㄦ帶鐢卞悇鑷ā鍧楀垵濮嬪寲銆�
 */
AD9361_InitParam default_init_param = {
    /* Device selection */
    .dev_sel = ID_AD9361,
    /* Identification number */
    .id_no = 0,
    /* Reference Clock */
    .reference_clk_rate = 40000000UL,
    /* Base Configuration */
    .two_rx_two_tx_mode_enable = 1,
    .one_rx_one_tx_mode_use_rx_num = 1,
    .one_rx_one_tx_mode_use_tx_num = 1,
    .frequency_division_duplex_mode_enable = 1,
    .frequency_division_duplex_independent_mode_enable = 0,
    .tdd_use_dual_synth_mode_enable = 0,
    .tdd_skip_vco_cal_enable = 0,
    .tx_fastlock_delay_ns = 0,
    .rx_fastlock_delay_ns = 0,
    .rx_fastlock_pincontrol_enable = 0,
    .tx_fastlock_pincontrol_enable = 0,
    .external_rx_lo_enable = 0,
    .external_tx_lo_enable = 0,
    .dc_offset_tracking_update_event_mask = 5,
    .dc_offset_attenuation_high_range = 6,
    .dc_offset_attenuation_low_range = 5,
    .dc_offset_count_high_range = 0x28,
    .dc_offset_count_low_range = 0x32,
    .split_gain_table_mode_enable = 0,
    .trx_synthesizer_target_fref_overwrite_hz = MAX_SYNTH_FREF,
    .qec_tracking_slow_mode_enable = 0,
    /* ENSM Control */
    .ensm_enable_pin_pulse_mode_enable = 0,
    .ensm_enable_txnrx_control_enable = 0,
    /* LO Control */
    .rx_synthesizer_frequency_hz = 1000000000UL,
    .tx_synthesizer_frequency_hz = 1000000000UL,
    .tx_lo_powerdown_managed_enable = 1,
    /* Rate & BW Control */
    .rx_path_clock_frequencies = {983040000, 245760000, 122880000, 61440000, 30720000, 30720000},
    .tx_path_clock_frequencies = {983040000, 122880000, 122880000, 61440000, 30720000, 30720000},
    .rf_rx_bandwidth_hz = 18000000,
    .rf_tx_bandwidth_hz = 18000000,
    /* RF Port Control */
    .rx_rf_port_input_select = 0,
    .tx_rf_port_input_select = 0,
    /* TX Attenuation Control */
    .tx_attenuation_mdB = 10000,
    .update_tx_gain_in_alert_enable = 0,
    /* Reference Clock Control */
    .xo_disable_use_ext_refclk_enable = 0,
    .dcxo_coarse_and_fine_tune = {8, 5920},
    .clk_output_mode_select = CLKOUT_DISABLE,
    /* Gain Control */
    .gc_rx1_mode = 2,
    .gc_rx2_mode = 2,
    .gc_adc_large_overload_thresh = 58,
    .gc_adc_ovr_sample_size = 4,
    .gc_adc_small_overload_thresh = 47,
    .gc_dec_pow_measurement_duration = 8192,
    .gc_dig_gain_enable = 0,
    .gc_lmt_overload_high_thresh = 800,
    .gc_lmt_overload_low_thresh = 704,
    .gc_low_power_thresh = 24,
    .gc_max_dig_gain = 15,
    /* Gain MGC Control */
    .mgc_dec_gain_step = 2,
    .mgc_inc_gain_step = 2,
    .mgc_rx1_ctrl_inp_enable = 0,
    .mgc_rx2_ctrl_inp_enable = 0,
    .mgc_split_table_ctrl_inp_gain_mode = 0,
    /* Gain AGC Control */
    .agc_adc_large_overload_exceed_counter = 10,
    .agc_adc_large_overload_inc_steps = 2,
    .agc_adc_lmt_small_overload_prevent_gain_inc_enable = 0,
    .agc_adc_small_overload_exceed_counter = 10,
    .agc_dig_gain_step_size = 4,
    .agc_dig_saturation_exceed_counter = 3,
    .agc_gain_update_interval_us = 1000,
    .agc_immed_gain_change_if_large_adc_overload_enable = 0,
    .agc_immed_gain_change_if_large_lmt_overload_enable = 0,
    .agc_inner_thresh_high = 10,
    .agc_inner_thresh_high_dec_steps = 1,
    .agc_inner_thresh_low = 12,
    .agc_inner_thresh_low_inc_steps = 1,
    .agc_lmt_overload_large_exceed_counter = 10,
    .agc_lmt_overload_large_inc_steps = 2,
    .agc_lmt_overload_small_exceed_counter = 10,
    .agc_outer_thresh_high = 5,
    .agc_outer_thresh_high_dec_steps = 2,
    .agc_outer_thresh_low = 18,
    .agc_outer_thresh_low_inc_steps = 2,
    .agc_attack_delay_extra_margin_us = 1,
    .agc_sync_for_gain_counter_enable = 0,
    /* Fast AGC */
    .fagc_dec_pow_measuremnt_duration = 64,
    .fagc_state_wait_time_ns = 260,
    /* Fast AGC - Low Power */
    .fagc_allow_agc_gain_increase = 0,
    .fagc_lp_thresh_increment_time = 5,
    .fagc_lp_thresh_increment_steps = 1,
    /* Fast AGC - Lock Level (Lock Level is set via slow AGC inner high threshold) */
    .fagc_lock_level_lmt_gain_increase_en = 1,
    .fagc_lock_level_gain_increase_upper_limit = 5,
    /* Fast AGC - Peak Detectors and Final Settling */
    .fagc_lpf_final_settling_steps = 1,
    .fagc_lmt_final_settling_steps = 1,
    .fagc_final_overrange_count = 3,
    /* Fast AGC - Final Power Test */
    .fagc_gain_increase_after_gain_lock_en = 0,
    /* Fast AGC - Unlocking the Gain */
    .fagc_gain_index_type_after_exit_rx_mode = 0,
    .fagc_use_last_lock_level_for_set_gain_en = 1,
    .fagc_rst_gla_stronger_sig_thresh_exceeded_en = 1,
    .fagc_optimized_gain_offset = 5,
    .fagc_rst_gla_stronger_sig_thresh_above_ll = 10,
    .fagc_rst_gla_engergy_lost_sig_thresh_exceeded_en = 1,
    .fagc_rst_gla_engergy_lost_goto_optim_gain_en = 1,
    .fagc_rst_gla_engergy_lost_sig_thresh_below_ll = 10,
    .fagc_energy_lost_stronger_sig_gain_lock_exit_cnt = 8,
    .fagc_rst_gla_large_adc_overload_en = 1,
    .fagc_rst_gla_large_lmt_overload_en = 1,
    .fagc_rst_gla_en_agc_pulled_high_en = 0,
    .fagc_rst_gla_if_en_agc_pulled_high_mode = 0,
    .fagc_power_measurement_duration_in_state5 = 64,
    /* RSSI Control */
    .rssi_delay = 1,
    .rssi_duration = 1000,
    .rssi_restart_mode = 3,
    .rssi_unit_is_rx_samples_enable = 0,
    .rssi_wait = 1,
    /* Aux ADC Control */
    .aux_adc_decimation = 256,
    .aux_adc_rate = 40000000UL,
    /* AuxDAC Control */
    .aux_dac_manual_mode_enable = 1,
    .aux_dac1_default_value_mV = 0,
    .aux_dac1_active_in_rx_enable = 0,
    .aux_dac1_active_in_tx_enable = 0,
    .aux_dac1_active_in_alert_enable = 0,
    .aux_dac1_rx_delay_us = 0,
    .aux_dac1_tx_delay_us = 0,
    .aux_dac2_default_value_mV = 0,
    .aux_dac2_active_in_rx_enable = 0,
    .aux_dac2_active_in_tx_enable = 0,
    .aux_dac2_active_in_alert_enable = 0,
    .aux_dac2_rx_delay_us = 0,
    .aux_dac2_tx_delay_us = 0,
    /* Temperature Sensor Control */
    .temp_sense_decimation = 256,
    .temp_sense_measurement_interval_ms = 1000,
    .temp_sense_offset_signed = 0xCE,
    .temp_sense_periodic_measurement_enable = 1,
    /* Control Out Setup */
    .ctrl_outs_enable_mask = 0xFF,
    .ctrl_outs_index = 0,
    /* External LNA Control */
    .elna_settling_delay_ns = 0,
    .elna_gain_mdB = 0,
    .elna_bypass_loss_mdB = 0,
    .elna_rx1_gpo0_control_enable = 0,
    .elna_rx2_gpo1_control_enable = 0,
    .elna_gaintable_all_index_enable = 0,
    /* Digital Interface Control */
    .digital_interface_tune_skip_mode = 0,
    .digital_interface_tune_fir_disable = 0,
    .pp_tx_swap_enable = 1,
    .pp_rx_swap_enable = 1,
    .tx_channel_swap_enable = 0,
    .rx_channel_swap_enable = 0,
    .rx_frame_pulse_mode_enable = 1,
    .two_t_two_r_timing_enable = 0,
    .invert_data_bus_enable = 0,
    .invert_data_clk_enable = 0,
    .fdd_alt_word_order_enable = 0,
    .invert_rx_frame_enable = 0,
    .fdd_rx_rate_2tx_enable = 0,
    .swap_ports_enable = 0,
    .single_data_rate_enable = 0,
    .lvds_mode_enable = 1,
    .half_duplex_mode_enable = 0,
    .single_port_mode_enable = 0,
    .full_port_enable = 0,
    .full_duplex_swap_bits_enable = 0,
    .delay_rx_data = 0,
    .rx_data_clock_delay = 0,
    .rx_data_delay = 4,
    .tx_fb_clock_delay = 7,
    .tx_data_delay = 0,
    .lvds_bias_mV = 150,
    .lvds_rx_onchip_termination_enable = 1,
    .rx1rx2_phase_inversion_en = 0,
    .lvds_invert1_control = 0xFF,
    .lvds_invert2_control = 0x0F,
    /* GPO Control */
    .gpo0_inactive_state_high_enable = 0,
    .gpo1_inactive_state_high_enable = 0,
    .gpo2_inactive_state_high_enable = 0,
    .gpo3_inactive_state_high_enable = 0,
    .gpo0_slave_rx_enable = 0,
    .gpo0_slave_tx_enable = 0,
    .gpo1_slave_rx_enable = 0,
    .gpo1_slave_tx_enable = 0,
    .gpo2_slave_rx_enable = 0,
    .gpo2_slave_tx_enable = 0,
    .gpo3_slave_rx_enable = 0,
    .gpo3_slave_tx_enable = 0,
    .gpo0_rx_delay_us = 0,
    .gpo0_tx_delay_us = 0,
    .gpo1_rx_delay_us = 0,
    .gpo1_tx_delay_us = 0,
    .gpo2_rx_delay_us = 0,
    .gpo2_tx_delay_us = 0,
    .gpo3_rx_delay_us = 0,
    .gpo3_tx_delay_us = 0,
    /* Tx Monitor Control */
    .low_high_gain_threshold_mdB = 37000,
    .low_gain_dB = 0,
    .high_gain_dB = 24,
    .tx_mon_track_en = 0,
    .one_shot_mode_en = 0,
    .tx_mon_delay = 511,
    .tx_mon_duration = 8192,
    .tx1_mon_front_end_gain = 2,
    .tx2_mon_front_end_gain = 2,
    .tx1_mon_lo_cm = 48,
    .tx2_mon_lo_cm = 48,
    /* GPIO definitions */
	.gpio_resetb = GPIO_RESET_PIN,
    /* MCS Sync */
    .gpio_sync = -1,
    .gpio_cal_sw1 = -1,
    .gpio_cal_sw2 = -1,
    /* External LO clocks */
    .ad9361_rfpll_ext_recalc_rate = NULL,
    .ad9361_rfpll_ext_round_rate = NULL,
    .ad9361_rfpll_ext_set_rate = NULL};

AD9361_RXFIRConfig rx_fir_config = {
    .rx = 3,
    .rx_gain = 0,
    .rx_dec = 1,
    .rx_coef = {-4,   -6,    -37,   35,   186,   86,     -284,  -315,  107,   219,   -4,     271,
                558,  -307,  -1182, -356, 658,   157,    207,   1648,  790,   -2525, -2553,  748,
                865,  -476,  3737,  6560, -3583, -14731, -5278, 14819, 14819, -5278, -14731, -3583,
                6560, 3737,  -476,  865,  748,   -2553,  -2525, 790,   1648,  207,   157,    658,
                -356, -1182, -307,  558,  271,   -4,     219,   107,   -315,  -284,  86,     186,
                35,   -37,   -6,    -4,   0,     0,      0,     0,     0,     0,     0,      0,
                0,    0,     0,     0,    0,     0,      0,     0,     0,     0,     0,      0,
                0,    0,     0,     0,    0,     0,      0,     0,     0,     0,     0,      0,
                0,    0,     0,     0,    0,     0,      0,     0,     0,     0,     0,      0,
                0,    0,     0,     0,    0,     0,      0,     0,     0,     0,     0,      0,
                0,    0,     0,     0,    0,     0,      0,     0},
    .rx_coef_size = 64,
    .rx_path_clks = {0, 0, 0, 0, 0, 0},
    .rx_bandwidth = 0};

AD9361_TXFIRConfig tx_fir_config = {
    .tx = 3,
    .tx_gain = -6,
    .tx_int = 1,
    .tx_coef = {-4,   -6,    -37,   35,   186,   86,     -284,  -315,  107,   219,   -4,     271,
                558,  -307,  -1182, -356, 658,   157,    207,   1648,  790,   -2525, -2553,  748,
                865,  -476,  3737,  6560, -3583, -14731, -5278, 14819, 14819, -5278, -14731, -3583,
                6560, 3737,  -476,  865,  748,   -2553,  -2525, 790,   1648,  207,   157,    658,
                -356, -1182, -307,  558,  271,   -4,     219,   107,   -315,  -284,  86,     186,
                35,   -37,   -6,    -4,   0,     0,      0,     0,     0,     0,     0,      0,
                0,    0,     0,     0,    0,     0,      0,     0,     0,     0,     0,      0,
                0,    0,     0,     0,    0,     0,      0,     0,     0,     0,     0,      0,
                0,    0,     0,     0,    0,     0,      0,     0,     0,     0,     0,      0,
                0,    0,     0,     0,    0,     0,      0,     0,     0,     0,     0,      0,
                0,    0,     0,     0,    0,     0,      0,     0},
    .tx_coef_size = 64,
    .tx_path_clks = {0, 0, 0, 0, 0, 0},
    .tx_bandwidth = 0};
struct ad9361_rf_phy *ad9361_phy;

void board_peripherals_init(void)
{
    /* AD9361 RESETB: PS EMIO GPIO[0], XGpioPs pin 54. */
    default_init_param.gpio_resetb = GPIO_RESET_PIN;
    default_init_param.gpio_sync = -1;
    default_init_param.gpio_cal_sw1 = -1;
    default_init_param.gpio_cal_sw2 = -1;

    gpio_init(GPIO_DEVICE_ID);

    /* 先设置输出值，再使能输出，让 AD9361 保持复位。 */
    gpio_set_value(GPIO_RESET_PIN, 0);
    gpio_direction(GPIO_RESET_PIN, 1);

    spi_init(SPI_DEVICE_ID, 1, 0);
}
