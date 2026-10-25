//Copyright 1986-2018 Xilinx, Inc. All Rights Reserved.
//--------------------------------------------------------------------------------
//Tool Version: Vivado v.2018.3 (win64) Build 2405991 Thu Dec  6 23:38:27 MST 2018
//Date        : Sat Oct 24 21:12:43 2026
//Host        : HZZ_LAP running 64-bit major release  (build 9200)
//Command     : generate_target system_wrapper.bd
//Design      : system_wrapper
//Purpose     : IP block netlist
//--------------------------------------------------------------------------------
`timescale 1 ps / 1 ps

module system_wrapper
   (AD9361_RESET,
    CH32H417_AE_n_0,
    CH32H417_AF_n_0,
    CH32H417_CLK_0,
    CH32H417_CS_n_0,
    CH32H417_DATA_0,
    CH32H417_EOP_n_0,
    CH32H417_OE_n_0,
    CH32H417_RDNE_0,
    CH32H417_RD_n_0,
    CH32H417_SEL_0,
    CH32H417_WRNF_0,
    CH32H417_WR_n_0,
    UART_RX,
    UART_TX,
    ddr_addr,
    ddr_ba,
    ddr_cas_n,
    ddr_ck_n,
    ddr_ck_p,
    ddr_cke,
    ddr_cs_n,
    ddr_dm,
    ddr_dq,
    ddr_dqs_n,
    ddr_dqs_p,
    ddr_odt,
    ddr_ras_n,
    ddr_reset_n,
    ddr_we_n,
    enable,
    fan_pwm_0,
    fixed_io_ddr_vrn,
    fixed_io_ddr_vrp,
    fixed_io_mio,
    fixed_io_ps_clk,
    fixed_io_ps_porb,
    fixed_io_ps_srstb,
    gps_pps_0,
    rgb_data_0,
    rx_clk_in_n,
    rx_clk_in_p,
    rx_data_in_n,
    rx_data_in_p,
    rx_frame_in_n,
    rx_frame_in_p,
    spi0_clk_o,
    spi0_csn_0_o,
    spi0_sdi_i,
    spi0_sdo_o,
    tdd_sync_i,
    tx_clk_out_n,
    tx_clk_out_p,
    tx_data_out_n,
    tx_data_out_p,
    tx_frame_out_n,
    tx_frame_out_p,
    txnrx,
    up_enable,
    up_txnrx);
  output [0:0]AD9361_RESET;
  output CH32H417_AE_n_0;
  output CH32H417_AF_n_0;
  input CH32H417_CLK_0;
  input CH32H417_CS_n_0;
  inout [31:0]CH32H417_DATA_0;
  input CH32H417_EOP_n_0;
  input CH32H417_OE_n_0;
  output CH32H417_RDNE_0;
  input CH32H417_RD_n_0;
  input [1:0]CH32H417_SEL_0;
  output CH32H417_WRNF_0;
  input CH32H417_WR_n_0;
  input UART_RX;
  output UART_TX;
  inout [14:0]ddr_addr;
  inout [2:0]ddr_ba;
  inout ddr_cas_n;
  inout ddr_ck_n;
  inout ddr_ck_p;
  inout ddr_cke;
  inout ddr_cs_n;
  inout [3:0]ddr_dm;
  inout [31:0]ddr_dq;
  inout [3:0]ddr_dqs_n;
  inout [3:0]ddr_dqs_p;
  inout ddr_odt;
  inout ddr_ras_n;
  inout ddr_reset_n;
  inout ddr_we_n;
  output enable;
  output fan_pwm_0;
  inout fixed_io_ddr_vrn;
  inout fixed_io_ddr_vrp;
  inout [53:0]fixed_io_mio;
  inout fixed_io_ps_clk;
  inout fixed_io_ps_porb;
  inout fixed_io_ps_srstb;
  input gps_pps_0;
  output rgb_data_0;
  input rx_clk_in_n;
  input rx_clk_in_p;
  input [5:0]rx_data_in_n;
  input [5:0]rx_data_in_p;
  input rx_frame_in_n;
  input rx_frame_in_p;
  output spi0_clk_o;
  output spi0_csn_0_o;
  input spi0_sdi_i;
  output spi0_sdo_o;
  input tdd_sync_i;
  output tx_clk_out_n;
  output tx_clk_out_p;
  output [5:0]tx_data_out_n;
  output [5:0]tx_data_out_p;
  output tx_frame_out_n;
  output tx_frame_out_p;
  output txnrx;
  input up_enable;
  input up_txnrx;

  wire [0:0]AD9361_RESET;
  wire CH32H417_AE_n_0;
  wire CH32H417_AF_n_0;
  wire CH32H417_CLK_0;
  wire CH32H417_CS_n_0;
  wire [31:0]CH32H417_DATA_0;
  wire CH32H417_EOP_n_0;
  wire CH32H417_OE_n_0;
  wire CH32H417_RDNE_0;
  wire CH32H417_RD_n_0;
  wire [1:0]CH32H417_SEL_0;
  wire CH32H417_WRNF_0;
  wire CH32H417_WR_n_0;
  wire UART_RX;
  wire UART_TX;
  wire [14:0]ddr_addr;
  wire [2:0]ddr_ba;
  wire ddr_cas_n;
  wire ddr_ck_n;
  wire ddr_ck_p;
  wire ddr_cke;
  wire ddr_cs_n;
  wire [3:0]ddr_dm;
  wire [31:0]ddr_dq;
  wire [3:0]ddr_dqs_n;
  wire [3:0]ddr_dqs_p;
  wire ddr_odt;
  wire ddr_ras_n;
  wire ddr_reset_n;
  wire ddr_we_n;
  wire enable;
  wire fan_pwm_0;
  wire fixed_io_ddr_vrn;
  wire fixed_io_ddr_vrp;
  wire [53:0]fixed_io_mio;
  wire fixed_io_ps_clk;
  wire fixed_io_ps_porb;
  wire fixed_io_ps_srstb;
  wire gps_pps_0;
  wire rgb_data_0;
  wire rx_clk_in_n;
  wire rx_clk_in_p;
  wire [5:0]rx_data_in_n;
  wire [5:0]rx_data_in_p;
  wire rx_frame_in_n;
  wire rx_frame_in_p;
  wire spi0_clk_o;
  wire spi0_csn_0_o;
  wire spi0_sdi_i;
  wire spi0_sdo_o;
  wire tdd_sync_i;
  wire tx_clk_out_n;
  wire tx_clk_out_p;
  wire [5:0]tx_data_out_n;
  wire [5:0]tx_data_out_p;
  wire tx_frame_out_n;
  wire tx_frame_out_p;
  wire txnrx;
  wire up_enable;
  wire up_txnrx;

  system system_i
       (.AD9361_RESET(AD9361_RESET),
        .CH32H417_AE_n_0(CH32H417_AE_n_0),
        .CH32H417_AF_n_0(CH32H417_AF_n_0),
        .CH32H417_CLK_0(CH32H417_CLK_0),
        .CH32H417_CS_n_0(CH32H417_CS_n_0),
        .CH32H417_DATA_0(CH32H417_DATA_0),
        .CH32H417_EOP_n_0(CH32H417_EOP_n_0),
        .CH32H417_OE_n_0(CH32H417_OE_n_0),
        .CH32H417_RDNE_0(CH32H417_RDNE_0),
        .CH32H417_RD_n_0(CH32H417_RD_n_0),
        .CH32H417_SEL_0(CH32H417_SEL_0),
        .CH32H417_WRNF_0(CH32H417_WRNF_0),
        .CH32H417_WR_n_0(CH32H417_WR_n_0),
        .UART_RX(UART_RX),
        .UART_TX(UART_TX),
        .ddr_addr(ddr_addr),
        .ddr_ba(ddr_ba),
        .ddr_cas_n(ddr_cas_n),
        .ddr_ck_n(ddr_ck_n),
        .ddr_ck_p(ddr_ck_p),
        .ddr_cke(ddr_cke),
        .ddr_cs_n(ddr_cs_n),
        .ddr_dm(ddr_dm),
        .ddr_dq(ddr_dq),
        .ddr_dqs_n(ddr_dqs_n),
        .ddr_dqs_p(ddr_dqs_p),
        .ddr_odt(ddr_odt),
        .ddr_ras_n(ddr_ras_n),
        .ddr_reset_n(ddr_reset_n),
        .ddr_we_n(ddr_we_n),
        .enable(enable),
        .fan_pwm_0(fan_pwm_0),
        .fixed_io_ddr_vrn(fixed_io_ddr_vrn),
        .fixed_io_ddr_vrp(fixed_io_ddr_vrp),
        .fixed_io_mio(fixed_io_mio),
        .fixed_io_ps_clk(fixed_io_ps_clk),
        .fixed_io_ps_porb(fixed_io_ps_porb),
        .fixed_io_ps_srstb(fixed_io_ps_srstb),
        .gps_pps_0(gps_pps_0),
        .rgb_data_0(rgb_data_0),
        .rx_clk_in_n(rx_clk_in_n),
        .rx_clk_in_p(rx_clk_in_p),
        .rx_data_in_n(rx_data_in_n),
        .rx_data_in_p(rx_data_in_p),
        .rx_frame_in_n(rx_frame_in_n),
        .rx_frame_in_p(rx_frame_in_p),
        .spi0_clk_o(spi0_clk_o),
        .spi0_csn_0_o(spi0_csn_0_o),
        .spi0_sdi_i(spi0_sdi_i),
        .spi0_sdo_o(spi0_sdo_o),
        .tdd_sync_i(tdd_sync_i),
        .tx_clk_out_n(tx_clk_out_n),
        .tx_clk_out_p(tx_clk_out_p),
        .tx_data_out_n(tx_data_out_n),
        .tx_data_out_p(tx_data_out_p),
        .tx_frame_out_n(tx_frame_out_n),
        .tx_frame_out_p(tx_frame_out_p),
        .txnrx(txnrx),
        .up_enable(up_enable),
        .up_txnrx(up_txnrx));
endmodule
