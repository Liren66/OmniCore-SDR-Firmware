`timescale 1ns / 1ps
`default_nettype none

// OmniCore sample-clock time base (similar to the USRP B2x0 vita_time).
//
// time_now is a 64-bit count of AD9361 sample instants: it advances by one on
// every interface sample strobe (adc_valid), so one tick = one sample and the
// tick rate equals the RX/TX sample rate. A sample presented together with a
// strobe carries the value of time_now in that same cycle.
//
// Loading a new time (radio clock domain):
//   code 1 NOW      : the next cycle loads cmd_value.
//   code 2 NEXT_PPS : arms; the first selected PPS rising edge loads cmd_value.
//   code 3 CANCEL   : disarms and clears time_valid (e.g. after a rate change).
//   code 0          : only updates the PPS source selection.
// A load in the same cycle as a strobe wins: that strobe's sample keeps the
// old time and the following sample carries cmd_value.
// last_pps is the time of the first sample at/after each selected PPS edge
// (cmd_value when the edge performed a NEXT_PPS load).
//
// PPS sources: 0 none, 1 external PPS pin, 2 internal 1 PPS divided from the
// AD9361 CLK_OUT reference (REF_HZ, normally the 40 MHz VCTCXO, disciplined by
// the ADF4002 when a 10 MHz reference is used). External PPS crosses three
// radio-clock flops; the constant latency is not compensated.
module oc_timekeeper #(
    parameter integer REF_HZ = 40000000
)(
    input  wire        ctrl_clk,
    input  wire        ctrl_resetn,
    input  wire        cmd_start,          // ctrl_clk pulse
    input  wire [1:0]  cmd_code,
    input  wire [1:0]  cmd_pps_sel,
    input  wire [63:0] cmd_value,
    output wire        cmd_busy,
    output reg  [63:0] snap_now,
    output reg  [63:0] snap_pps,
    output reg  [31:0] snap_pps_count,
    output wire [7:0]  snap_flags,         // 0 armed,1 valid,3:2 sel,4 ref,5 stalled

    input  wire        clk,                // radio clock (adc_clk == dac_clk)
    input  wire        strobe,
    input  wire        pps_ext,
    input  wire        ref_clk,            // AD9361 CLK_OUT, may be stopped
    output reg  [63:0] time_now,
    output reg         time_valid
);
    // ------------------------------------------------------------------
    // Radio-domain reset: asynchronous assert, 32 running clocks to release.
    // ------------------------------------------------------------------
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg [31:0] rel;
    always @(posedge clk or negedge ctrl_resetn) begin
        if (!ctrl_resetn) rel <= 0;
        else rel <= {rel[30:0], 1'b1};
    end
    wire rstn = rel[31];

    // ------------------------------------------------------------------
    // Internal PPS from the reference clock (own clock domain).
    // The high level lasts 100 ms so every radio clock rate can sample it.
    // ------------------------------------------------------------------
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg [1:0] ref_rst_sync;
    always @(posedge ref_clk or negedge ctrl_resetn) begin
        if (!ctrl_resetn) ref_rst_sync <= 0;
        else ref_rst_sync <= {ref_rst_sync[0], 1'b1};
    end
    reg [31:0] ref_div;
    reg ref_pps_level;
    always @(posedge ref_clk or negedge ref_rst_sync[1]) begin
        if (!ref_rst_sync[1]) begin
            ref_div <= 0;
            ref_pps_level <= 0;
        end else begin
            ref_div <= (ref_div == REF_HZ - 1) ? 32'd0 : ref_div + 1'b1;
            ref_pps_level <= (ref_div < REF_HZ / 10);
        end
    end

    // ------------------------------------------------------------------
    // Commands from the AXI side.
    // ------------------------------------------------------------------
    wire cmd_pulse;
    wire [67:0] cmd_word;
    oc_hold_xfer #(.W(68)) u_cmd (
        .sclk(ctrl_clk), .srstn(ctrl_resetn), .start(cmd_start),
        .sdata({cmd_pps_sel, cmd_code, cmd_value}), .busy(cmd_busy),
        .dclk(clk), .drstn(rstn), .dpulse(cmd_pulse), .ddata(cmd_word)
    );
    wire [1:0]  c_sel  = cmd_word[67:66];
    wire [1:0]  c_code = cmd_word[65:64];
    wire [63:0] c_val  = cmd_word[63:0];

    // ------------------------------------------------------------------
    // PPS synchronizers and edge detection in the radio domain.
    // ------------------------------------------------------------------
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg [2:0] ext_s;
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg [2:0] int_s;
    always @(posedge clk) begin
        ext_s <= {ext_s[1:0], pps_ext};
        int_s <= {int_s[1:0], ref_pps_level};
    end
    wire ext_edge = ext_s[1] & ~ext_s[2];
    wire int_edge = int_s[1] & ~int_s[2];

    reg [1:0] pps_sel;
    reg armed;
    reg [63:0] armed_value;
    reg [63:0] last_pps;
    reg [31:0] pps_count;
    // Registered edge: one fixed extra radio clock of PPS latency.
    reg pps_edge;
    always @(posedge clk) pps_edge <= (pps_sel == 2'd1) ? ext_edge :
                                      (pps_sel == 2'd2) ? int_edge : 1'b0;

    always @(posedge clk or negedge rstn) begin
        if (!rstn) begin
            time_now <= 0;
            time_valid <= 0;
            pps_sel <= 0;
            armed <= 0;
            armed_value <= 0;
            last_pps <= 0;
            pps_count <= 0;
        end else begin
            if (cmd_pulse) pps_sel <= c_sel;
            if (cmd_pulse && c_code == 2'd1) begin
                time_now <= c_val;
                time_valid <= 1'b1;
                armed <= 1'b0;
            end else if (armed && pps_edge) begin
                time_now <= armed_value;
                time_valid <= 1'b1;
                armed <= 1'b0;
            end else if (strobe) begin
                time_now <= time_now + 1'b1;
            end
            if (cmd_pulse && c_code == 2'd2) begin
                armed <= 1'b1;
                armed_value <= c_val;
            end
            if (cmd_pulse && c_code == 2'd3) begin
                armed <= 1'b0;
                time_valid <= 1'b0;
            end
            if (pps_edge) begin
                last_pps <= armed ? armed_value : time_now;
                pps_count <= pps_count + 1'b1;
            end
        end
    end

    // ------------------------------------------------------------------
    // Snapshot: the AXI side requests continuously; the radio side copies a
    // coherent set into held registers and acknowledges.
    // ------------------------------------------------------------------
    reg snap_req, snap_ack;
    reg [63:0] h_now, h_pps;
    reg [31:0] h_cnt;
    reg [3:0]  h_flags;
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg sr1, sr2;
    always @(posedge clk or negedge rstn) begin
        if (!rstn) begin
            sr1 <= 0; sr2 <= 0; snap_ack <= 0;
            h_now <= 0; h_pps <= 0; h_cnt <= 0; h_flags <= 0;
        end else begin
            sr1 <= snap_req;
            sr2 <= sr1;
            if (sr2 != snap_ack) begin
                h_now <= time_now;
                h_pps <= last_pps;
                h_cnt <= pps_count;
                h_flags <= {pps_sel, time_valid, armed};
                snap_ack <= sr2;
            end
        end
    end
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg sa1, sa2;
    reg snap_seen;
    reg [3:0] c_flags;
    reg [15:0] stall_timer;
    always @(posedge ctrl_clk or negedge ctrl_resetn) begin
        if (!ctrl_resetn) begin
            sa1 <= 0; sa2 <= 0; snap_seen <= 0; snap_req <= 0;
            snap_now <= 0; snap_pps <= 0; snap_pps_count <= 0; c_flags <= 0;
            stall_timer <= 0;
        end else begin
            sa1 <= snap_ack;
            sa2 <= sa1;
            if (sa2 != snap_seen) begin
                snap_now <= h_now;
                snap_pps <= h_pps;
                snap_pps_count <= h_cnt;
                c_flags <= h_flags;
                snap_seen <= sa2;
                stall_timer <= 0;
            end else begin
                if (sa2 == snap_req) snap_req <= !snap_req;
                if (stall_timer != 16'hFFFF) stall_timer <= stall_timer + 1'b1;
            end
        end
    end

    // Reference presence: a slow toggle from ref_clk observed in ctrl_clk.
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg [2:0] ref_t;
    reg [11:0] ref_timer;
    always @(posedge ctrl_clk or negedge ctrl_resetn) begin
        if (!ctrl_resetn) begin
            ref_t <= 0;
            ref_timer <= 12'hFFF;
        end else begin
            ref_t <= {ref_t[1:0], ref_div[4]};
            if (ref_t[2] != ref_t[1]) ref_timer <= 0;
            else if (ref_timer != 12'hFFF) ref_timer <= ref_timer + 1'b1;
        end
    end
    wire ref_present = (ref_timer < 12'd1024);
    wire stalled = (stall_timer == 16'hFFFF);
    assign snap_flags = {2'b0, stalled, ref_present, c_flags};
endmodule

`default_nettype wire
