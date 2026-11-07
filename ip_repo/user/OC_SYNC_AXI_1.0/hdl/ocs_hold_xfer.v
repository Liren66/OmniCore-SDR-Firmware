`timescale 1ns / 1ps
`default_nettype none

// Moves one held W-bit word from the source to the destination clock domain
// with a toggle request/acknowledge. start is ignored while busy. The held
// word is stable from the request until the acknowledge returns, so the
// destination may capture all bits together. dpulse is one destination cycle.
// Reset both sides from the same source; a one-sided reset can replay a word.
module ocs_hold_xfer #(
    parameter integer W = 32
)(
    input  wire         sclk,
    input  wire         srstn,
    input  wire         start,
    input  wire [W-1:0] sdata,
    output wire         busy,
    input  wire         dclk,
    input  wire         drstn,
    output reg          dpulse,
    output reg  [W-1:0] ddata
);
    reg req;
    reg [W-1:0] held;
    reg ack;
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg ack_s1, ack_s2;
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg req_d1, req_d2;

    assign busy = (req != ack_s2);
    always @(posedge sclk or negedge srstn) begin
        if (!srstn) begin
            req <= 0; held <= 0; ack_s1 <= 0; ack_s2 <= 0;
        end else begin
            ack_s1 <= ack;
            ack_s2 <= ack_s1;
            if (start && !busy) begin
                held <= sdata;
                req <= !req;
            end
        end
    end
    always @(posedge dclk or negedge drstn) begin
        if (!drstn) begin
            req_d1 <= 0; req_d2 <= 0; ack <= 0; dpulse <= 0; ddata <= 0;
        end else begin
            req_d1 <= req;
            req_d2 <= req_d1;
            dpulse <= 0;
            if (req_d2 != ack) begin
                ddata <= held;
                ack <= req_d2;
                dpulse <= 1;
            end
        end
    end
endmodule

`default_nettype wire
