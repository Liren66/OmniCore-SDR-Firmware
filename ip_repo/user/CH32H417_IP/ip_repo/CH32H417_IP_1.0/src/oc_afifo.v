`timescale 1ns / 1ps
`default_nettype none

// Small asynchronous FIFO in distributed RAM, first-word-fall-through.
// rdata is valid whenever rempty=0; ren pops it. Gray pointers cross with two
// ASYNC_REG flops. Each side has an asynchronous active-low reset; assert the
// two resets together (both are derived from the same enable in this IP).
// Cross-domain paths are bounded by the project's datapath-only max delays.
module oc_afifo #(
    parameter integer W  = 128,
    parameter integer AW = 3          // depth = 2**AW, AW >= 2
)(
    input  wire         wclk,
    input  wire         wrstn,
    input  wire         wen,
    input  wire [W-1:0] wdata,
    output wire         wfull,
    input  wire         rclk,
    input  wire         rrstn,
    input  wire         ren,
    output wire [W-1:0] rdata,
    output wire         rempty
);
    (* ram_style = "distributed" *) reg [W-1:0] mem [0:(1<<AW)-1];
    reg [AW:0] wbin, wgray, rbin, rgray;
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg [AW:0] rg_w1, rg_w2;
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg [AW:0] wg_r1, wg_r2;

    wire wdo = wen && !wfull;
    wire [AW:0] wbin_n = wbin + {{AW{1'b0}}, wdo};
    always @(posedge wclk or negedge wrstn) begin
        if (!wrstn) begin
            wbin <= 0; wgray <= 0; rg_w1 <= 0; rg_w2 <= 0;
        end else begin
            wbin <= wbin_n;
            wgray <= (wbin_n >> 1) ^ wbin_n;
            rg_w1 <= rgray;
            rg_w2 <= rg_w1;
        end
    end
    always @(posedge wclk) if (wdo) mem[wbin[AW-1:0]] <= wdata;
    assign wfull = (wgray == {~rg_w2[AW:AW-1], rg_w2[AW-2:0]});

    wire rdo = ren && !rempty;
    wire [AW:0] rbin_n = rbin + {{AW{1'b0}}, rdo};
    always @(posedge rclk or negedge rrstn) begin
        if (!rrstn) begin
            rbin <= 0; rgray <= 0; wg_r1 <= 0; wg_r2 <= 0;
        end else begin
            rbin <= rbin_n;
            rgray <= (rbin_n >> 1) ^ rbin_n;
            wg_r1 <= wgray;
            wg_r2 <= wg_r1;
        end
    end
    assign rempty = (rgray == wg_r2);
    assign rdata = mem[rbin[AW-1:0]];
endmodule

`default_nettype wire
