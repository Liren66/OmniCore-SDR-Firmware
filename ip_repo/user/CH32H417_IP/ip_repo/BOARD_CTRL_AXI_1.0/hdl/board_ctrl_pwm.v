`timescale 1ns / 1ps
module board_ctrl_pwm #(
    parameter [31:0] DEFAULT_PERIOD = 4000
) (
    input wire clk,
    input wire resetn,
    input wire [31:0] period_request,
    input wire [31:0] high_request,
    output reg pwm_out
);
    reg [31:0] count, period_active, high_active;
    wire [31:0] next_period = (period_request == 0) ? 32'd1 : period_request;

    always @(posedge clk) begin
        if (!resetn) begin
            count <= 0;
            period_active <= DEFAULT_PERIOD;
            high_active <= 0;
            pwm_out <= 1'b0;
        end else if (count == period_active - 1'b1) begin
            count <= 0;
            period_active <= next_period;
            high_active <= high_request;
            pwm_out <= (high_request != 0);
        end else begin
            count <= count + 1'b1;
            pwm_out <= (count + 1'b1 < high_active);
        end
    end
endmodule
