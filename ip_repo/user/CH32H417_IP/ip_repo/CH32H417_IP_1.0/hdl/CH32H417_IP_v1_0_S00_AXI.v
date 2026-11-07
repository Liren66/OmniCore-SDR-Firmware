`timescale 1 ns / 1 ps

// CH32H417_IP 1.2 AXI4-Lite register file (32 x 32-bit registers).
//
// Offset  Name              Access  Description
// 0x00    CTRL0             RW      stream config (see CH32H417_Fsm.v); bit8 reserved, bit9 RX_TIMED
// 0x04    CTRL1             RW      bit0 rising edge clears latched errors
// 0x08    STATUS0           RO      bit31 framed (always 1), bit30 time capable, bit29 IQ8 capable
// 0x0C    STATUS1           RO      bit31 RX_TIMED applied, FIFO group counts
// 0x10    TIME_SET_LO       RW      value for the next time command
// 0x14    TIME_SET_HI       RW
// 0x18    TIME_CTRL         RW      write triggers: [1:0] 1 now, 2 next PPS, 3 cancel+invalidate,
//                                   0 = source only; [5:4] PPS source 0 none,1 ext,2 internal.
//                                   Read: [5:0] last written, bit31 command busy.
// 0x1C    TIME_STATUS       RO      0 armed, 1 time valid, 3:2 PPS source, 4 ref clock present,
//                                   5 radio clock stalled, 31 command busy
// 0x20    TIME_NOW_LO       RO      reading LO latches HI into 0x24 (coherent 64-bit read)
// 0x24    TIME_NOW_HI       RO
// 0x28    TIME_PPS_LO       RO      time of the first sample at/after the last PPS edge
// 0x2C    TIME_PPS_HI       RO
// 0x30    RX_CMD_TIME_LO    RW
// 0x34    RX_CMD_TIME_HI    RW
// 0x38    RX_CMD_NSAMP      RW      0 = continuous
// 0x3C    RX_CMD            RW      write triggers: bit0 now, bit1 stop. Read bit31 busy,
//                                   [15:0] framer status bits
// 0x40    EVT_STATUS        RO/W    read bit0 event available; write bit0 pops one event
// 0x44    EVT_INFO          RO      [31:24] code, [23] lost-before, [15:0] block seq
// 0x48    EVT_TIME_LO       RO
// 0x4C    EVT_TIME_HI       RO
// 0x50    PPS_COUNT         RO      selected PPS edges counted
// 0x54    VERSION           RO      0x0103_0001 (IP 1.3: framed only, register map 1)
// 0x58    FRAME_INFO        RO      [15:0] header bytes 16, [31:16] payload bytes 16368
	module CH32H417_IP_v1_0_S00_AXI #
	(
		parameter integer C_S_AXI_DATA_WIDTH	= 32,
		parameter integer C_S_AXI_ADDR_WIDTH	= 7,
		parameter integer REF_HZ = 40000000
	)
	(
        output wire			Interrupt_flag,
        input  wire         CH32H417_CLK,
        input  wire         CH32H417_CS_n,
        input  wire         CH32H417_WR_n,
        input  wire         CH32H417_RD_n,
        input  wire         CH32H417_OE_n,
        input  wire         CH32H417_EOP_n,
        input  wire [1:0]   CH32H417_SEL,
        inout  wire [31:0]  CH32H417_DATA,
        output wire         CH32H417_RDNE,
        output wire         CH32H417_WRNF,

        input  wire         adc_clk,
		input  wire [15:0]  adc_data_i0,
		input  wire [15:0]  adc_data_q0,
		input  wire [15:0]  adc_data_i1,
		input  wire [15:0]  adc_data_q1,
		input  wire         adc_valid_0,
		input  wire         adc_valid_1,
		input  wire         adc_enable_0,
		input  wire         adc_enable_1,

        input  wire         dac_clk,
        input  wire [15:0]  dac_data_i0,
        input  wire [15:0]  dac_data_q0,
        input  wire [15:0]  dac_data_i1,
        input  wire [15:0]  dac_data_q1,
        input  wire         dac_valid_0,
        input  wire         dac_valid_1,
        input  wire         dac_enable_0,
        input  wire         dac_enable_1,
        output wire [15:0]  dac_data_i0_out,
        output wire [15:0]  dac_data_q0_out,
        output wire [15:0]  dac_data_i1_out,
        output wire [15:0]  dac_data_q1_out,

        input  wire         pps_ext,
        input  wire         ref_clk_in,

		input wire  S_AXI_ACLK,
		input wire  S_AXI_ARESETN,
		input wire [C_S_AXI_ADDR_WIDTH-1 : 0] S_AXI_AWADDR,
		input wire [2 : 0] S_AXI_AWPROT,
		input wire  S_AXI_AWVALID,
		output wire  S_AXI_AWREADY,
		input wire [C_S_AXI_DATA_WIDTH-1 : 0] S_AXI_WDATA,
		input wire [(C_S_AXI_DATA_WIDTH/8)-1 : 0] S_AXI_WSTRB,
		input wire  S_AXI_WVALID,
		output wire  S_AXI_WREADY,
		output wire [1 : 0] S_AXI_BRESP,
		output wire  S_AXI_BVALID,
		input wire  S_AXI_BREADY,
		input wire [C_S_AXI_ADDR_WIDTH-1 : 0] S_AXI_ARADDR,
		input wire [2 : 0] S_AXI_ARPROT,
		input wire  S_AXI_ARVALID,
		output wire  S_AXI_ARREADY,
		output wire [C_S_AXI_DATA_WIDTH-1 : 0] S_AXI_RDATA,
		output wire [1 : 0] S_AXI_RRESP,
		output wire  S_AXI_RVALID,
		input wire  S_AXI_RREADY
	);
	localparam integer ADDR_LSB = 2;
	localparam integer OPT_MEM_ADDR_BITS = 4;   // 32 registers

	reg [C_S_AXI_ADDR_WIDTH-1 : 0] axi_awaddr;
	reg  	axi_awready;
	reg  	axi_wready;
	reg [1 : 0] 	axi_bresp;
	reg  	axi_bvalid;
	reg [C_S_AXI_ADDR_WIDTH-1 : 0] axi_araddr;
	reg  	axi_arready;
	reg [31 : 0] 	axi_rdata;
	reg [1 : 0] 	axi_rresp;
	reg  	axi_rvalid;
	reg aw_en;

	assign S_AXI_AWREADY = axi_awready;
	assign S_AXI_WREADY  = axi_wready;
	assign S_AXI_BRESP   = axi_bresp;
	assign S_AXI_BVALID  = axi_bvalid;
	assign S_AXI_ARREADY = axi_arready;
	assign S_AXI_RDATA   = axi_rdata;
	assign S_AXI_RRESP   = axi_rresp;
	assign S_AXI_RVALID  = axi_rvalid;

	always @(posedge S_AXI_ACLK) begin
	  if (!S_AXI_ARESETN) begin
	    axi_awready <= 1'b0; aw_en <= 1'b1; axi_awaddr <= 0; axi_wready <= 1'b0;
	  end else begin
	    if (~axi_awready && S_AXI_AWVALID && S_AXI_WVALID && aw_en) begin
	      axi_awready <= 1'b1; aw_en <= 1'b0; axi_awaddr <= S_AXI_AWADDR;
	    end else if (S_AXI_BREADY && axi_bvalid) begin
	      aw_en <= 1'b1; axi_awready <= 1'b0;
	    end else begin
	      axi_awready <= 1'b0;
	    end
	    axi_wready <= (~axi_wready && S_AXI_WVALID && S_AXI_AWVALID && aw_en);
	  end
	end
	wire slv_reg_wren = axi_wready && S_AXI_WVALID && axi_awready && S_AXI_AWVALID;
	wire [OPT_MEM_ADDR_BITS:0] waddr = axi_awaddr[ADDR_LSB+OPT_MEM_ADDR_BITS:ADDR_LSB];
	wire [31:0] wdata_full = S_AXI_WDATA;

	always @(posedge S_AXI_ACLK) begin
	  if (!S_AXI_ARESETN) begin
	    axi_bvalid <= 0; axi_bresp <= 2'b0;
	  end else if (axi_awready && S_AXI_AWVALID && ~axi_bvalid && axi_wready && S_AXI_WVALID) begin
	    axi_bvalid <= 1'b1; axi_bresp <= 2'b0;
	  end else if (S_AXI_BREADY && axi_bvalid) begin
	    axi_bvalid <= 1'b0;
	  end
	end
	always @(posedge S_AXI_ACLK) begin
	  if (!S_AXI_ARESETN) begin
	    axi_arready <= 1'b0; axi_araddr <= 0;
	  end else if (~axi_arready && S_AXI_ARVALID) begin
	    axi_arready <= 1'b1; axi_araddr <= S_AXI_ARADDR;
	  end else begin
	    axi_arready <= 1'b0;
	  end
	end
	always @(posedge S_AXI_ACLK) begin
	  if (!S_AXI_ARESETN) begin
	    axi_rvalid <= 0; axi_rresp <= 0;
	  end else if (axi_arready && S_AXI_ARVALID && ~axi_rvalid) begin
	    axi_rvalid <= 1'b1; axi_rresp <= 2'b0;
	  end else if (axi_rvalid && S_AXI_RREADY) begin
	    axi_rvalid <= 1'b0;
	  end
	end

	// ------------------------------------------------------------------
	// Writable registers. Byte strobes are honoured for plain registers;
	// trigger registers act on any write.
	// ------------------------------------------------------------------
	reg [31:0] slv_reg0, slv_reg1;
	reg [31:0] r_tset_lo, r_tset_hi, r_tctrl, r_rxt_lo, r_rxt_hi, r_rxn, r_rxcmd;
	function [31:0] merge;
		input [31:0] old;
		input [31:0] nw;
		input [3:0] strb;
		integer b;
		begin
			merge = old;
			for (b = 0; b < 4; b = b + 1) if (strb[b]) merge[b*8 +: 8] = nw[b*8 +: 8];
		end
	endfunction
	always @(posedge S_AXI_ACLK) begin
	  if (!S_AXI_ARESETN) begin
	    slv_reg0 <= 0; slv_reg1 <= 0; r_tset_lo <= 0; r_tset_hi <= 0; r_tctrl <= 0;
	    r_rxt_lo <= 0; r_rxt_hi <= 0; r_rxn <= 0; r_rxcmd <= 0;
	  end else if (slv_reg_wren) begin
	    case (waddr)
	      5'd0:  slv_reg0  <= merge(slv_reg0,  wdata_full, S_AXI_WSTRB);
	      5'd1:  slv_reg1  <= merge(slv_reg1,  wdata_full, S_AXI_WSTRB);
	      5'd4:  r_tset_lo <= merge(r_tset_lo, wdata_full, S_AXI_WSTRB);
	      5'd5:  r_tset_hi <= merge(r_tset_hi, wdata_full, S_AXI_WSTRB);
	      5'd6:  r_tctrl   <= wdata_full;
	      5'd12: r_rxt_lo  <= merge(r_rxt_lo,  wdata_full, S_AXI_WSTRB);
	      5'd13: r_rxt_hi  <= merge(r_rxt_hi,  wdata_full, S_AXI_WSTRB);
	      5'd14: r_rxn     <= merge(r_rxn,     wdata_full, S_AXI_WSTRB);
	      5'd15: r_rxcmd   <= wdata_full;
	      default: ;
	    endcase
	  end
	end
	wire time_cmd_start = slv_reg_wren && (waddr == 5'd6);
	wire rx_cmd_start   = slv_reg_wren && (waddr == 5'd15);
	wire evt_pop        = slv_reg_wren && (waddr == 5'd16) && wdata_full[0];

	// ------------------------------------------------------------------
	// Time base.
	// ------------------------------------------------------------------
	wire [63:0] time_now;
	wire time_valid;
	wire time_busy;
	wire [63:0] snap_now, snap_pps;
	wire [31:0] snap_pps_count;
	wire [7:0] snap_flags;
	oc_timekeeper #(.REF_HZ(REF_HZ)) u_timekeeper (
		.ctrl_clk(S_AXI_ACLK), .ctrl_resetn(S_AXI_ARESETN),
		.cmd_start(time_cmd_start), .cmd_code(wdata_full[1:0]), .cmd_pps_sel(wdata_full[5:4]),
		.cmd_value({r_tset_hi, r_tset_lo}), .cmd_busy(time_busy),
		.snap_now(snap_now), .snap_pps(snap_pps), .snap_pps_count(snap_pps_count),
		.snap_flags(snap_flags),
		.clk(adc_clk), .strobe(adc_valid_0 | adc_valid_1), .pps_ext(pps_ext),
		.ref_clk(ref_clk_in), .time_now(time_now), .time_valid(time_valid)
	);

	// ------------------------------------------------------------------
	// Streaming core.
	// ------------------------------------------------------------------
	wire [31:0] status0, status1;
	wire rx_cmd_busy;
	wire evt_empty;
	wire [95:0] evt_word;
	wire [15:0] frame_status;
    CH32H417_Fsm CH32H417_Fsm_inst(
		.ctrl_clk(S_AXI_ACLK),
		.ctrl_resetn(S_AXI_ARESETN),
        .Interrupt_flag(Interrupt_flag),
        .CH32H417_CLK(CH32H417_CLK),
        .CH32H417_CS_n(CH32H417_CS_n),
        .CH32H417_WR_n(CH32H417_WR_n),
        .CH32H417_RD_n(CH32H417_RD_n),
        .CH32H417_OE_n(CH32H417_OE_n),
        .CH32H417_EOP_n(CH32H417_EOP_n),
        .CH32H417_SEL(CH32H417_SEL),
        .CH32H417_DATA(CH32H417_DATA),
        .CH32H417_RDNE(CH32H417_RDNE),
        .CH32H417_WRNF(CH32H417_WRNF),
        .adc_clk(adc_clk),
		.adc_data_i0(adc_data_i0),
		.adc_data_q0(adc_data_q0),
		.adc_data_i1(adc_data_i1),
		.adc_data_q1(adc_data_q1),
		.adc_valid_0(adc_valid_0),
		.adc_valid_1(adc_valid_1),
		.adc_enable_0(adc_enable_0),
		.adc_enable_1(adc_enable_1),
        .dac_clk(dac_clk),
        .dac_data_i0(dac_data_i0),
        .dac_data_q0(dac_data_q0),
        .dac_data_i1(dac_data_i1),
        .dac_data_q1(dac_data_q1),
        .dac_valid_0(dac_valid_0),
        .dac_valid_1(dac_valid_1),
        .dac_enable_0(dac_enable_0),
        .dac_enable_1(dac_enable_1),
        .dac_data_i0_out(dac_data_i0_out),
        .dac_data_q0_out(dac_data_q0_out),
        .dac_data_i1_out(dac_data_i1_out),
        .dac_data_q1_out(dac_data_q1_out),
		.CH32H417_CTRL_REG0(slv_reg0),
		.CH32H417_CTRL_REG1(slv_reg1),
		.CH32H417_STATUS_REG0(status0),
		.CH32H417_STATUS_REG1(status1),
		.time_now(time_now),
		.time_valid(time_valid),
		.rx_cmd_start(rx_cmd_start),
		.rx_cmd_word({wdata_full[1], wdata_full[0], r_rxn, r_rxt_hi, r_rxt_lo}),
		.rx_cmd_busy(rx_cmd_busy),
		.evt_pop(evt_pop),
		.evt_empty(evt_empty),
		.evt_word(evt_word),
		.frame_status(frame_status)
    );

	// ------------------------------------------------------------------
	// Read mux. Reading a LO word latches its HI word for a coherent pair.
	// ------------------------------------------------------------------
	wire slv_reg_rden = axi_arready & S_AXI_ARVALID & ~axi_rvalid;
	wire [OPT_MEM_ADDR_BITS:0] raddr = axi_araddr[ADDR_LSB+OPT_MEM_ADDR_BITS:ADDR_LSB];
	reg [31:0] now_hi_sh, pps_hi_sh;
	reg [31:0] reg_data_out;
	always @(*) begin
	  case (raddr)
	    5'd0:  reg_data_out = slv_reg0;
	    5'd1:  reg_data_out = slv_reg1;
	    5'd2:  reg_data_out = status0;
	    5'd3:  reg_data_out = status1;
	    5'd4:  reg_data_out = r_tset_lo;
	    5'd5:  reg_data_out = r_tset_hi;
	    5'd6:  reg_data_out = {time_busy, 25'd0, r_tctrl[5:0]};
	    5'd7:  reg_data_out = {time_busy, 23'd0, snap_flags};
	    5'd8:  reg_data_out = snap_now[31:0];
	    5'd9:  reg_data_out = now_hi_sh;
	    5'd10: reg_data_out = snap_pps[31:0];
	    5'd11: reg_data_out = pps_hi_sh;
	    5'd12: reg_data_out = r_rxt_lo;
	    5'd13: reg_data_out = r_rxt_hi;
	    5'd14: reg_data_out = r_rxn;
	    5'd15: reg_data_out = {rx_cmd_busy, 15'd0, frame_status};
	    5'd16: reg_data_out = {31'd0, !evt_empty};
	    5'd17: reg_data_out = {evt_word[95:88], evt_word[87], 7'd0, evt_word[79:64]};
	    5'd18: reg_data_out = evt_word[31:0];
	    5'd19: reg_data_out = evt_word[63:32];
	    5'd20: reg_data_out = snap_pps_count;
	    5'd21: reg_data_out = 32'h0103_0001;
	    5'd22: reg_data_out = {16'd16368, 16'd16};
	    default: reg_data_out = 32'd0;
	  endcase
	end
	always @(posedge S_AXI_ACLK) begin
	  if (!S_AXI_ARESETN) begin
	    axi_rdata <= 0; now_hi_sh <= 0; pps_hi_sh <= 0;
	  end else if (slv_reg_rden) begin
	    axi_rdata <= reg_data_out;
	    if (raddr == 5'd8)  now_hi_sh <= snap_now[63:32];
	    if (raddr == 5'd10) pps_hi_sh <= snap_pps[63:32];
	  end
	end
	endmodule
