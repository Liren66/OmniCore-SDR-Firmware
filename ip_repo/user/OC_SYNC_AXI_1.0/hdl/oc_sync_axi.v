`timescale 1ns / 1ps
`default_nettype none

// OmniCore multi-board sync, AXI4-Lite wrapper (see README.md for the map).
//
//  0x00 ID          RO 0x4F435359 ("OCSY")
//  0x04 VERSION     RO 0x00010000
//  0x08 CTRL        RW 0 ref_out_en, 1 trig_en, 2 trig_falling, 3 periodic_en,
//                      4 pps_sel_sync (timekeeper PPS = SYNC instead of PPS_EXT),
//                      5 loopback (SYNC capture uses the internal TRIG)
//  0x0C TRIG_WIDTH  RW 40 MHz cycles (0 -> 1), reset 80 (2 us)
//  0x10 TRIG_PERIOD RW 40 MHz cycles (< 1000 -> 1000), reset 40000000 (1 s)
//  0x14 CMD         WO 0 fire, 1 arm at next PPS_EXT, 2 disarm, 3 clear counters
//  0x18 STATUS      RO 0 cfg_busy, 1 ref40_present, 2 armed, 3 trig level,
//                      4 sync level, 5 pps level
//  0x1C REF40_HZ    RO measured clk_40m frequency, 16 Hz resolution, 1 s gate
//  0x20 SYNC_COUNT  RO rising edges on AD9361_SYNC
//  0x24 SYNC_INTERVAL RO clk_40m cycles between the last two SYNC edges
//  0x28 PPS_COUNT   RO rising edges on PPS_EXT
//  0x2C PPS_INTERVAL RO clk_40m cycles between the last two PPS edges
//  0x30 TRIG_COUNT  RO TRIG pulses emitted
//
// CTRL / TRIG_WIDTH / TRIG_PERIOD / CMD writes are carried to the 40 MHz
// domain by one hold-xfer word; STATUS.cfg_busy is set until it has been
// applied (it stays set while clk_40m is absent). Status counters return as
// a continuously refreshed snapshot, a few clk_40m cycles old.
module oc_sync_axi #(
    parameter integer CLK_FREQ_HZ = 100000000
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 S_AXI_CLK CLK", X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXI, ASSOCIATED_RESET s_axi_aresetn" *)
    input  wire        s_axi_aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 S_AXI_RST RST", X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire        s_axi_aresetn,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWADDR", X_INTERFACE_PARAMETER = "PROTOCOL AXI4LITE, DATA_WIDTH 32, ADDR_WIDTH 12, READ_WRITE_MODE READ_WRITE, HAS_BURST 0, HAS_LOCK 0, HAS_CACHE 0, HAS_QOS 0, HAS_REGION 0, SUPPORTS_NARROW_BURST 0, NUM_READ_OUTSTANDING 1, NUM_WRITE_OUTSTANDING 1" *)
    input  wire [11:0] s_axi_awaddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWPROT" *)
    input  wire [2:0]  s_axi_awprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWVALID" *)
    input  wire        s_axi_awvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWREADY" *)
    output wire        s_axi_awready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WDATA" *)
    input  wire [31:0] s_axi_wdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WSTRB" *)
    input  wire [3:0]  s_axi_wstrb,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WVALID" *)
    input  wire        s_axi_wvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WREADY" *)
    output wire        s_axi_wready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BRESP" *)
    output reg  [1:0]  s_axi_bresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BVALID" *)
    output reg         s_axi_bvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BREADY" *)
    input  wire        s_axi_bready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARADDR" *)
    input  wire [11:0] s_axi_araddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARPROT" *)
    input  wire [2:0]  s_axi_arprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARVALID" *)
    input  wire        s_axi_arvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARREADY" *)
    output wire        s_axi_arready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RDATA" *)
    output reg  [31:0] s_axi_rdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RRESP" *)
    output reg  [1:0]  s_axi_rresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RVALID" *)
    output reg         s_axi_rvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RREADY" *)
    input  wire        s_axi_rready,

    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk_40m CLK", X_INTERFACE_PARAMETER = "FREQ_HZ 40000000" *)
    input  wire        clk_40m,         // CLK_FB_FPGA
    input  wire        sync_in,         // AD9361_SYNC pin (shared with AD9361 SYNC_IN)
    input  wire        pps_ext_in,      // PPS_EXT pin
    output wire        pps_out,         // to the time base PPS input
    output wire        ref_out,         // REF_OUT, 10 MHz
    output wire        trig_out         // TRIG
);
    localparam [31:0] ID = 32'h4F435359;
    localparam [31:0] VERSION = 32'h00010000;

    // ------------------------------------------------------------------
    // Register file (AXI domain).
    // ------------------------------------------------------------------
    reg [5:0]  ctrl;
    reg [15:0] width;
    reg [31:0] period;
    reg [3:0]  cmd_pend;      // commands waiting for the next transfer
    reg        cfg_pend;      // configuration changed, not yet sent
    wire       xfer_busy;

    reg [11:0] write_addr;
    reg [31:0] write_data;
    reg [3:0]  write_strb;
    reg addr_pending, data_pending;
    integer lane;

    assign s_axi_awready = s_axi_aresetn && !addr_pending && !s_axi_bvalid;
    assign s_axi_wready  = s_axi_aresetn && !data_pending && !s_axi_bvalid;
    assign s_axi_arready = s_axi_aresetn && !s_axi_rvalid;

    wire send = (cfg_pend || cmd_pend != 4'd0) && !xfer_busy;

    always @(posedge s_axi_aclk) begin
        if (!s_axi_aresetn) begin
            ctrl <= 6'd0; width <= 16'd80; period <= 32'd40000000;
            cmd_pend <= 4'd0; cfg_pend <= 1'b1;
            write_addr <= 0; write_data <= 0; write_strb <= 0;
            addr_pending <= 0; data_pending <= 0;
            s_axi_bvalid <= 0; s_axi_bresp <= 2'b00;
        end else begin
            if (send) begin
                cfg_pend <= 1'b0;
                cmd_pend <= 4'd0;
            end
            if (s_axi_awready && s_axi_awvalid) begin
                write_addr <= s_axi_awaddr;
                addr_pending <= 1'b1;
            end
            if (s_axi_wready && s_axi_wvalid) begin
                write_data <= s_axi_wdata;
                write_strb <= s_axi_wstrb;
                data_pending <= 1'b1;
            end
            if (s_axi_bvalid && s_axi_bready)
                s_axi_bvalid <= 1'b0;
            if (addr_pending && data_pending && !s_axi_bvalid) begin
                addr_pending <= 1'b0;
                data_pending <= 1'b0;
                s_axi_bvalid <= 1'b1;
                s_axi_bresp <= 2'b00;
                case (write_addr)
                    12'h008: begin
                        if (write_strb[0]) ctrl <= write_data[5:0];
                        cfg_pend <= 1'b1;
                    end
                    12'h00C: begin
                        for (lane = 0; lane < 2; lane = lane + 1)
                            if (write_strb[lane]) width[lane*8 +: 8] <= write_data[lane*8 +: 8];
                        cfg_pend <= 1'b1;
                    end
                    12'h010: begin
                        for (lane = 0; lane < 4; lane = lane + 1)
                            if (write_strb[lane]) period[lane*8 +: 8] <= write_data[lane*8 +: 8];
                        cfg_pend <= 1'b1;
                    end
                    // A command written while one is waiting is merged into it.
                    12'h014: if (write_strb[0]) cmd_pend <= (send ? 4'd0 : cmd_pend) | write_data[3:0];
                    default: s_axi_bresp <= 2'b10;
                endcase
            end
        end
    end

    // ------------------------------------------------------------------
    // 40 MHz domain reset and transfers.
    // ------------------------------------------------------------------
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg [3:0] rst40;
    always @(posedge clk_40m or negedge s_axi_aresetn) begin
        if (!s_axi_aresetn) rst40 <= 4'd0;
        else rst40 <= {rst40[2:0], 1'b1};
    end
    wire rstn40 = rst40[3];

    wire        cfg_pulse;
    wire [57:0] cfg_word;
    ocs_hold_xfer #(.W(58)) u_cfg (
        .sclk(s_axi_aclk), .srstn(s_axi_aresetn), .start(send),
        .sdata({cmd_pend, ctrl, width, period}), .busy(xfer_busy),
        .dclk(clk_40m), .drstn(rstn40), .dpulse(cfg_pulse), .ddata(cfg_word));

    wire [3:0]  c_cmd    = cfg_word[57:54];
    wire [5:0]  c_ctrl   = cfg_word[53:48];
    wire [15:0] c_width  = cfg_word[47:32];
    wire [31:0] c_period = cfg_word[31:0];

    wire [31:0] sync_count, sync_interval, pps_count, pps_interval, trig_count;
    wire [3:0]  flags, fdiv;
    wire        trig_int;
    oc_sync_core u_core (
        .clk(clk_40m), .rstn(rstn40),
        .cfg_pulse(cfg_pulse), .ref_en(c_ctrl[0]), .trig_en(c_ctrl[1]),
        .trig_falling(c_ctrl[2]), .periodic_en(c_ctrl[3]), .loopback(c_ctrl[5]),
        .trig_width(c_width), .trig_period(c_period),
        .cmd_fire(cfg_pulse & c_cmd[0]), .cmd_arm_pps(cfg_pulse & c_cmd[1]),
        .cmd_disarm(cfg_pulse & c_cmd[2]), .cmd_clear(cfg_pulse & c_cmd[3]),
        .sync_pin(sync_in), .pps_pin(pps_ext_in), .trig_int(trig_int),
        .sync_count(sync_count), .sync_interval(sync_interval),
        .pps_count(pps_count), .pps_interval(pps_interval), .trig_count(trig_count),
        .flags(flags), .fdiv(fdiv), .ref_out(ref_out), .trig_out(trig_out));

    // Status snapshot back to the AXI domain, refreshed continuously.
    wire         snap_busy, snap_pulse;
    wire [163:0] snap_word;
    ocs_hold_xfer #(.W(164)) u_snap (
        .sclk(clk_40m), .srstn(rstn40), .start(1'b1),
        .sdata({flags, trig_count, pps_interval, pps_count, sync_interval, sync_count}),
        .busy(snap_busy), .dclk(s_axi_aclk), .drstn(s_axi_aresetn),
        .dpulse(snap_pulse), .ddata(snap_word));

    // ------------------------------------------------------------------
    // PPS selection for the time base (combinational, no added latency).
    // ------------------------------------------------------------------
    assign pps_out = ctrl[4] ? (ctrl[5] ? trig_int : sync_in) : pps_ext_in;

    // ------------------------------------------------------------------
    // clk_40m presence and frequency, measured with s_axi_aclk.
    // ------------------------------------------------------------------
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg [2:0] fd_s;
    reg [31:0] gate, edges, ref_hz;
    reg [7:0]  watchdog;
    always @(posedge s_axi_aclk) begin
        if (!s_axi_aresetn) begin
            fd_s <= 0; gate <= 0; edges <= 0; ref_hz <= 0; watchdog <= 0;
        end else begin
            fd_s <= {fd_s[1:0], fdiv[3]};
            if (fd_s[1] & ~fd_s[2]) begin
                edges <= edges + 1'b1;
                watchdog <= 8'd255;
            end else if (watchdog != 0) begin
                watchdog <= watchdog - 1'b1;
            end
            if (gate == CLK_FREQ_HZ - 1) begin
                gate <= 0;
                ref_hz <= {(edges + ((fd_s[1] & ~fd_s[2]) ? 1'b1 : 1'b0)), 4'b0000};
                edges <= 0;
            end else begin
                gate <= gate + 1'b1;
            end
        end
    end

    reg [163:0] snap;
    always @(posedge s_axi_aclk) begin
        if (!s_axi_aresetn) snap <= 0;
        else if (snap_pulse) snap <= snap_word;
    end
    wire present = watchdog != 0;
    wire [31:0] status = {26'd0, snap[163], snap[162], snap[161], present ? snap[160] : 1'b0,
                          present, xfer_busy | cfg_pend | (cmd_pend != 4'd0)};

    always @(posedge s_axi_aclk) begin
        if (!s_axi_aresetn) begin
            s_axi_rvalid <= 1'b0; s_axi_rdata <= 0; s_axi_rresp <= 2'b00;
        end else begin
            if (s_axi_rvalid && s_axi_rready)
                s_axi_rvalid <= 1'b0;
            if (s_axi_arready && s_axi_arvalid) begin
                s_axi_rvalid <= 1'b1;
                s_axi_rresp <= 2'b00;
                case (s_axi_araddr)
                    12'h000: s_axi_rdata <= ID;
                    12'h004: s_axi_rdata <= VERSION;
                    12'h008: s_axi_rdata <= {26'd0, ctrl};
                    12'h00C: s_axi_rdata <= {16'd0, width};
                    12'h010: s_axi_rdata <= period;
                    12'h014: s_axi_rdata <= 32'd0;
                    12'h018: s_axi_rdata <= status;
                    12'h01C: s_axi_rdata <= ref_hz;
                    12'h020: s_axi_rdata <= snap[31:0];
                    12'h024: s_axi_rdata <= snap[63:32];
                    12'h028: s_axi_rdata <= snap[95:64];
                    12'h02C: s_axi_rdata <= snap[127:96];
                    12'h030: s_axi_rdata <= snap[159:128];
                    default: begin
                        s_axi_rdata <= 32'd0;
                        s_axi_rresp <= 2'b10;
                    end
                endcase
            end
        end
    end
endmodule

`default_nettype wire
