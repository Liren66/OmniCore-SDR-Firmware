# AD9361_MIX pin-binding revision, 2026-10-10
# Source: AD9361_MIX.pdf, pages 1 (PL_IO), 3 (power), 7 (AD9361).
# FPGA: XC7Z020-2CLG400I. Existing HDL port names are preserved.
# STATUS: PIN MAP VERIFIED; NOT A COMPLETE BITSTREAM-READY CONSTRAINT SET.
#
# Schematic supplies: VCCO_13=1.8V, VCCO_34=1.8V, VCCO_35=3.3V,
# AD9361 VDD_INTERFACE=1.8V. All active pin assignments below are Bank 34.
# Replace the corresponding OLD constraints; do not load both copies.
#
# Outstanding items:
# 1. HR-bank LVDS_25 outputs require VCCO=2.5V (AMD UG471 pp.91-92).
#    Bank 34 is 1.8V in the supplied schematic. TX IOSTANDARD assignments
#    are intentionally NOT activated. A hardware/interface decision is needed.
# 2. LVDS_25 inputs at 1.8V require DIFF_TERM=FALSE, valid input electrical
#    levels, and suitable external receiver termination. No external RX
#    100-ohm differential resistors are shown on schematic pages 1 and 7.
#    The RX settings below do not, by themselves, solve missing termination.
# 3. up_enable, up_txnrx and tdd_sync_i need top-level HDL / BD review.
#    Their old physical assignments are intentionally removed; see the end.
# 4. 2026-10-22: CH32H417 pins, clocks and internal CDC timing added below.
#    External CH32H417/AD9361 I/O delays omitted at the user's request.
#    Existing AD9361 electrical assignments are preserved as found, including
#    the later overrides below; the older electrical notes are NOT validation
#    of the present board. Timing assumptions and remaining limits are below.

# ----------------------------------------------------------------------
# SPI: direction is relative to FPGA, whereas DI/DO in the net names are
# relative to AD9361. FPGA SDI <- AD9361 DO; FPGA SDO -> AD9361 DI.
# ----------------------------------------------------------------------
set_property PACKAGE_PIN R14 [get_ports spi0_clk_o]
set_property PACKAGE_PIN P18 [get_ports spi0_csn_0_o]
set_property PACKAGE_PIN R19 [get_ports spi0_sdi_i]
set_property PACKAGE_PIN P15 [get_ports spi0_sdo_o]
set_property IOSTANDARD LVCMOS25 [get_ports spi0_clk_o]
set_property IOSTANDARD LVCMOS25 [get_ports spi0_csn_0_o]
set_property IOSTANDARD LVCMOS25 [get_ports spi0_sdi_i]
set_property IOSTANDARD LVCMOS25 [get_ports spi0_sdo_o]

# AD9361 control outputs from FPGA. RESET is active low at the device.
set_property PACKAGE_PIN N17 [get_ports {AD9361_RESET[0]}]
set_property PACKAGE_PIN R18 [get_ports enable]
set_property PACKAGE_PIN P14 [get_ports txnrx]
set_property IOSTANDARD LVCMOS25 [get_ports {AD9361_RESET[0]}]
set_property IOSTANDARD LVCMOS25 [get_ports enable]
set_property IOSTANDARD LVCMOS25 [get_ports txnrx]

# ----------------------------------------------------------------------
# RX: AD9361 -> FPGA. Explicitly constrain both P and N of every pair.
# ----------------------------------------------------------------------
set_property PACKAGE_PIN Y18 [get_ports {rx_data_in_p[0]}]
set_property PACKAGE_PIN Y19 [get_ports {rx_data_in_n[0]}]
set_property PACKAGE_PIN V17 [get_ports {rx_data_in_p[1]}]
set_property PACKAGE_PIN V18 [get_ports {rx_data_in_n[1]}]
set_property PACKAGE_PIN V20 [get_ports {rx_data_in_p[2]}]
set_property PACKAGE_PIN W20 [get_ports {rx_data_in_n[2]}]
set_property PACKAGE_PIN R16 [get_ports {rx_data_in_p[3]}]
set_property PACKAGE_PIN R17 [get_ports {rx_data_in_n[3]}]
set_property PACKAGE_PIN W18 [get_ports {rx_data_in_p[4]}]
set_property PACKAGE_PIN W19 [get_ports {rx_data_in_n[4]}]
set_property PACKAGE_PIN V16 [get_ports {rx_data_in_p[5]}]
set_property PACKAGE_PIN W16 [get_ports {rx_data_in_n[5]}]

set_property PACKAGE_PIN N20 [get_ports rx_clk_in_p]
set_property PACKAGE_PIN P20 [get_ports rx_clk_in_n]
set_property PACKAGE_PIN U18 [get_ports rx_frame_in_p]
set_property PACKAGE_PIN U19 [get_ports rx_frame_in_n]

set_property IOSTANDARD LVDS_25 [get_ports {{rx_data_in_p[*]} {rx_data_in_n[*]} rx_clk_in_p rx_clk_in_n rx_frame_in_p rx_frame_in_n}]
set_property DIFF_TERM true [get_ports {{rx_data_in_p[*]} {rx_data_in_n[*]} rx_clk_in_p rx_clk_in_n rx_frame_in_p rx_frame_in_n}]

# ----------------------------------------------------------------------
# TX: FPGA -> AD9361. Pin assignments are valid independently of the
# unresolved bank supply / output-standard issue above.
# ----------------------------------------------------------------------
set_property PACKAGE_PIN W14 [get_ports {tx_data_out_p[0]}]
set_property PACKAGE_PIN Y14 [get_ports {tx_data_out_n[0]}]
set_property PACKAGE_PIN T12 [get_ports {tx_data_out_p[1]}]
set_property PACKAGE_PIN U12 [get_ports {tx_data_out_n[1]}]
set_property PACKAGE_PIN U14 [get_ports {tx_data_out_p[2]}]
set_property PACKAGE_PIN U15 [get_ports {tx_data_out_n[2]}]
set_property PACKAGE_PIN T16 [get_ports {tx_data_out_p[3]}]
set_property PACKAGE_PIN U17 [get_ports {tx_data_out_n[3]}]
set_property PACKAGE_PIN V12 [get_ports {tx_data_out_p[4]}]
set_property PACKAGE_PIN W13 [get_ports {tx_data_out_n[4]}]
set_property PACKAGE_PIN V15 [get_ports {tx_data_out_p[5]}]
set_property PACKAGE_PIN W15 [get_ports {tx_data_out_n[5]}]

set_property PACKAGE_PIN N18 [get_ports tx_clk_out_p]
set_property PACKAGE_PIN P19 [get_ports tx_clk_out_n]
set_property PACKAGE_PIN Y16 [get_ports tx_frame_out_p]
set_property PACKAGE_PIN Y17 [get_ports tx_frame_out_n]

# NOT VALID WITH THE SCHEMATIC'S 1.8V BANK 34 SUPPLY:
# set_property IOSTANDARD LVDS_25 [get_ports {tx_data_out_p[*] tx_data_out_n[*] tx_clk_out_p tx_clk_out_n tx_frame_out_p tx_frame_out_n}]
# Do not change the above to LVDS: that output standard is for HP banks.
# Do not use LVCMOS18 on these pairs while retaining the LVDS HDL/interface.
# A compliant 2.5V LVDS redesign must also match SPI/control voltage levels,
# review AD9361 VDD_INTERFACE and the AD9361 signals connected to Bank 13,
# and update the single-ended standards and RX termination accordingly.
# Do not raise the shared board 1V8 rail to 2.5V.

# ----------------------------------------------------------------------
# AD9361: 61.44 MSPS per channel, 2R2T LVDS => DATA_CLK = 245.76 MHz.
# l_clk feeds BOTH CH32H417_IP adc_clk and dac_clk directly in this BD.
# They are not independent 61.44 MHz clocks: sample-valid qualifies the data.
# Keep all derived AD9361 clocks related; do not create clocks on adc/dac pins.
# ----------------------------------------------------------------------
create_clock -period 4.069010 -name rx_clk -waveform {0.000000 2.034505} [get_ports rx_clk_in_p]

# ----------------------------------------------------------------------
# UNRESOLVED ORIGINAL TOP-LEVEL PORTS - no physical binding is assumed.
# up_enable : old G14 is now UHSIF_SEL0 (Bank 35, USB interface).
# up_txnrx  : old E17 is now UHSIF_AE#  (Bank 35, USB interface).
# tdd_sync_i: old Y12 is now AD9361_CTRL_IN1 (AD9361 input, Bank 13).
# If these remain external ports, assign an intended external source using
# the top-level HDL/BD and actual wiring. If unused, handle inside the HDL/BD;
# deleting only the XDC assignments leaves unconstrained top-level ports.
#
# Potential external timing sources (NOT selected automatically):
#   TRIG    -> Y6 (Bank 13, 1.8V), see page 10.
#   PPS_EXT -> Y7 (Bank 13, 1.8V), see page 10.
# AD9361_SYNC -> U20 drives the chip SYNC_IN input (H5), not a default
# replacement for FPGA tdd_sync_i. A matching FPGA output port is required.
# AD9361_EN_AGC -> P16 is available if a matching HDL output is introduced.
# ----------------------------------------------------------------------

set_property IOSTANDARD LVDS_25 [get_ports tx_clk_out_p]
set_property IOSTANDARD LVCMOS25 [get_ports up_enable]
set_property IOSTANDARD LVCMOS25 [get_ports up_txnrx]
set_property IOSTANDARD LVCMOS25 [get_ports tdd_sync_i]
set_property IOSTANDARD LVDS_25 [get_ports {tx_data_out_p[5]}]
set_property IOSTANDARD LVDS_25 [get_ports {tx_data_out_p[4]}]
set_property IOSTANDARD LVDS_25 [get_ports {tx_data_out_p[3]}]
set_property IOSTANDARD LVDS_25 [get_ports {tx_data_out_p[2]}]
set_property IOSTANDARD LVDS_25 [get_ports {tx_data_out_p[1]}]
set_property IOSTANDARD LVDS_25 [get_ports {tx_data_out_p[0]}]
set_property IOSTANDARD LVDS_25 [get_ports tx_frame_out_p]
set_property PACKAGE_PIN W11 [get_ports tdd_sync_i]
set_property PACKAGE_PIN Y11 [get_ports up_enable]
set_property PACKAGE_PIN V10 [get_ports up_txnrx]

# ----------------------------------------------------------------------
# PL UART PROTS
# ----------------------------------------------------------------------
set_property PACKAGE_PIN G19 [get_ports UART_RX]
set_property PACKAGE_PIN H18 [get_ports UART_TX]
set_property IOSTANDARD LVCMOS33 [get_ports UART_RX]
set_property IOSTANDARD LVCMOS33 [get_ports UART_TX]

# ----------------------------------------------------------------------
# CH32H417 UHSIF: reference pin map translated to system_wrapper port names.
# 32 data + 2 select + 10 scalar signals = 44 physical pins, all LVCMOS33.
# Reference rst_n/D18 has NO matching top-level port; intentionally omitted.
# Do not copy the reference dbg_hub commands: their net names/frequency belong
# to a different design and are not constraints for this input clock.
# ----------------------------------------------------------------------
set_property PACKAGE_PIN M20 [get_ports {CH32H417_DATA_0[31]}]
set_property PACKAGE_PIN L20 [get_ports {CH32H417_DATA_0[30]}]
set_property PACKAGE_PIN J20 [get_ports {CH32H417_DATA_0[29]}]
set_property PACKAGE_PIN H20 [get_ports {CH32H417_DATA_0[28]}]
set_property PACKAGE_PIN G20 [get_ports {CH32H417_DATA_0[27]}]
set_property PACKAGE_PIN F20 [get_ports {CH32H417_DATA_0[26]}]
set_property PACKAGE_PIN D20 [get_ports {CH32H417_DATA_0[25]}]
set_property PACKAGE_PIN C20 [get_ports {CH32H417_DATA_0[24]}]
set_property PACKAGE_PIN B20 [get_ports {CH32H417_DATA_0[23]}]
set_property PACKAGE_PIN A20 [get_ports {CH32H417_DATA_0[22]}]
set_property PACKAGE_PIN B19 [get_ports {CH32H417_DATA_0[21]}]
set_property PACKAGE_PIN M19 [get_ports {CH32H417_DATA_0[20]}]
set_property PACKAGE_PIN M18 [get_ports {CH32H417_DATA_0[19]}]
set_property PACKAGE_PIN L19 [get_ports {CH32H417_DATA_0[18]}]
set_property PACKAGE_PIN M17 [get_ports {CH32H417_DATA_0[17]}]
set_property PACKAGE_PIN K19 [get_ports {CH32H417_DATA_0[16]}]
set_property PACKAGE_PIN K18 [get_ports {CH32H417_DATA_0[15]}]
set_property PACKAGE_PIN J19 [get_ports {CH32H417_DATA_0[14]}]
set_property PACKAGE_PIN J18 [get_ports {CH32H417_DATA_0[13]}]
set_property PACKAGE_PIN G18 [get_ports {CH32H417_DATA_0[12]}]
set_property PACKAGE_PIN F19 [get_ports {CH32H417_DATA_0[11]}]
set_property PACKAGE_PIN M15 [get_ports {CH32H417_DATA_0[10]}]
set_property PACKAGE_PIN L17 [get_ports {CH32H417_DATA_0[9]}]
set_property PACKAGE_PIN L16 [get_ports {CH32H417_DATA_0[8]}]
set_property PACKAGE_PIN L15 [get_ports {CH32H417_DATA_0[7]}]
set_property PACKAGE_PIN K17 [get_ports {CH32H417_DATA_0[6]}]
set_property PACKAGE_PIN N16 [get_ports {CH32H417_DATA_0[5]}]
set_property PACKAGE_PIN N15 [get_ports {CH32H417_DATA_0[4]}]
set_property PACKAGE_PIN M14 [get_ports {CH32H417_DATA_0[3]}]
set_property PACKAGE_PIN L14 [get_ports {CH32H417_DATA_0[2]}]
set_property PACKAGE_PIN K16 [get_ports {CH32H417_DATA_0[1]}]
set_property PACKAGE_PIN K14 [get_ports {CH32H417_DATA_0[0]}]
set_property PACKAGE_PIN G15 [get_ports {CH32H417_SEL_0[1]}]
set_property PACKAGE_PIN G14 [get_ports {CH32H417_SEL_0[0]}]
set_property PACKAGE_PIN E17 [get_ports CH32H417_AE_n_0]
set_property PACKAGE_PIN F16 [get_ports CH32H417_AF_n_0]
set_property PACKAGE_PIN J16 [get_ports CH32H417_CS_n_0]
set_property PACKAGE_PIN E18 [get_ports CH32H417_EOP_n_0]
set_property PACKAGE_PIN J14 [get_ports CH32H417_OE_n_0]
set_property PACKAGE_PIN H15 [get_ports CH32H417_RD_n_0]
set_property PACKAGE_PIN G17 [get_ports CH32H417_RDNE_0]
set_property PACKAGE_PIN H16 [get_ports CH32H417_CLK_0]
set_property PACKAGE_PIN J15 [get_ports CH32H417_WR_n_0]
set_property PACKAGE_PIN F17 [get_ports CH32H417_WRNF_0]
set_property IOSTANDARD LVCMOS33 [get_ports {CH32H417_DATA_0[*] CH32H417_SEL_0[*] CH32H417_AE_n_0 CH32H417_AF_n_0 CH32H417_CS_n_0 CH32H417_EOP_n_0 CH32H417_OE_n_0 CH32H417_RD_n_0 CH32H417_RDNE_0 CH32H417_CLK_0 CH32H417_WR_n_0 CH32H417_WRNF_0}]

set_property PACKAGE_PIN Y7 [get_ports gps_pps_0]
set_property IOSTANDARD LVCMOS25 [get_ports gps_pps_0]

# WS2812 RGB£∫E19
set_property PACKAGE_PIN E19 [get_ports {rgb_data_0}]
set_property IOSTANDARD LVCMOS33 [get_ports {rgb_data_0}]
set_property SLEW SLOW [get_ports {rgb_data_0}]
set_property DRIVE 8 [get_ports {rgb_data_0}]

# ∑Á…» PWM£∫D19
set_property PACKAGE_PIN D19 [get_ports {fan_pwm_0}]
set_property IOSTANDARD LVCMOS33 [get_ports {fan_pwm_0}]
set_property SLEW SLOW [get_ports {fan_pwm_0}]
set_property DRIVE 8 [get_ports {fan_pwm_0}]
# ----------------------------------------------------------------------
# Clock constraints. External CH32H417/AD9361 data-interface I/O delays are
# intentionally out of scope per user instruction (2026-10-22).
# CH32H417 uses SDR; AD9361 uses 2R2T LVDS DDR at 61.44 MSPS/channel.
# Keep the actual clock rates; DDR does not halve the 245.76 MHz DATA_CLK.
# ----------------------------------------------------------------------
create_clock -name ch32h417_clk -period 7.500 -waveform {0.000 3.750} [get_ports CH32H417_CLK_0]

# No false paths are applied to these ports; unconstrained I/O is reported.

# Forwarded AD9361 FB_CLK, DDR_CLK_EDGE_SEL=0: ODDR D1=0, D2=1.
# This clock records the physical relationship only; external I/O timing is
# not claimed closed without a separate board/interface validation.
set ad9361_tx_oddr_c [get_pins -hierarchical -filter {NAME =~ */i_dev_if/i_tx_clk/*i_tx_data_oddr/C}]
create_generated_clock -name ad9361_fb_clk -source $ad9361_tx_oddr_c -divide_by 1 -invert [get_ports tx_clk_out_p]

# ----------------------------------------------------------------------
# Independent clock domains: CH32H417, AD9361, PS FCLK.
# PS clocks (100/200 MHz) are created by the processing_system7 IP XDC.
# Preserve their constraints and all AD9361 generated-clock relationships.
# Use bounded datapath-only CDC constraints instead of blanket clock groups:
# clock groups/false paths would override FIFO Gray-pointer max-delay checks.
# Bound each crossing by the faster domain period. Bus skew is per payload,
# not across unrelated resets, status bits or divided-clock domains.
# Existing IP endpoint-specific exceptions remain in their IP XDC files.
# ----------------------------------------------------------------------
set ch32_domain [get_clocks -include_generated_clocks ch32h417_clk]
set ad9361_domain [get_clocks -include_generated_clocks rx_clk]
set ps_domain [get_clocks -include_generated_clocks {clk_fpga_0 clk_fpga_1}]
set_max_delay -datapath_only 4.069010 -from $ch32_domain -to $ad9361_domain
set_max_delay -datapath_only 4.069010 -from $ad9361_domain -to $ch32_domain
set_max_delay -datapath_only 5.000 -from $ch32_domain -to $ps_domain
set_max_delay -datapath_only 5.000 -from $ps_domain -to $ch32_domain
set_max_delay -datapath_only 4.069010 -from $ad9361_domain -to $ps_domain
set_max_delay -datapath_only 4.069010 -from $ps_domain -to $ad9361_domain

# Only compare bits of the SAME held-data handshake payload.
# FIFO Generator/XPM already supplies per-instance Gray-pointer bus skew.
# Independent single-bit synchronizers and asynchronous reset trees are not
# members of a common data bus and must not be compared to each other.
set ch32_cfg_held [get_cells -hierarchical -filter {NAME =~ */CH32H417_Fsm_inst/cfg_held_reg* && IS_SEQUENTIAL}]
set_bus_skew 5.000 -from $ch32_cfg_held -to [get_cells -hierarchical -filter {NAME =~ */CH32H417_Fsm_inst/cfg_u_reg* && IS_SEQUENTIAL}]
set_bus_skew 4.069010 -from $ch32_cfg_held -to [get_cells -hierarchical -filter {NAME =~ */CH32H417_Fsm_inst/cfg_a_reg* && IS_SEQUENTIAL}]
set_bus_skew 4.069010 -from $ch32_cfg_held -to [get_cells -hierarchical -filter {NAME =~ */CH32H417_Fsm_inst/cfg_d_reg* && IS_SEQUENTIAL}]
set_bus_skew 5.000 -from [get_cells -hierarchical -filter {NAME =~ */CH32H417_Fsm_inst/status_held_reg* && IS_SEQUENTIAL}] -to [get_cells -hierarchical -filter {NAME =~ */CH32H417_Fsm_inst/status_snapshot_reg* && IS_SEQUENTIAL}]

# Scope: internal FPGA timing. External streaming, SPI and control I/O paths
# have no board-level input/output delay constraints in this iteration.
# They are not hidden with false paths. PS DDR/MIO remain owned by the PS IP.
# Build reports and reproducible scripts are under timing_work_20261022/.
