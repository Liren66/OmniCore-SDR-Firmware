`timescale 1ns / 1ps
// One WS2812C-2020. Refresh continuously; snapshot RGB only at frame start.
// All timing is a clock enable/counter. No derived clocks, FIFO, or multiplier.
module board_ctrl_ws2812 #(
    parameter integer CLK_FREQ_HZ = 100000000
) (
    input wire clk,
    input wire resetn,
    input wire [23:0] rgb,  // RRGGBB in software; GRB on the wire, MSB first
    output reg data_out
);
    // 64-bit constant arithmetic avoids elaboration overflow, even at 200 MHz.
    localparam integer BIT_TICKS = (64'd1250 * CLK_FREQ_HZ + 500000000) / 1000000000;
    localparam integer ZERO_HIGH = (64'd300 * CLK_FREQ_HZ + 500000000) / 1000000000;
    localparam integer ONE_HIGH  = (64'd650 * CLK_FREQ_HZ + 500000000) / 1000000000;
    localparam integer RESET_TICKS = (64'd300 * CLK_FREQ_HZ + 999999) / 1000000;
    localparam integer COUNT_WIDTH = $clog2(RESET_TICKS + 1);
    reg [COUNT_WIDTH-1:0] ticks;
    reg [23:0] shift_data;
    reg [4:0] bit_index;
    reg sending;
    wire [COUNT_WIDTH-1:0] high_ticks = shift_data[23] ? ONE_HIGH : ZERO_HIGH;

    always @(posedge clk) begin
        if (!resetn) begin
            ticks <= 0;
            shift_data <= 0;
            bit_index <= 0;
            sending <= 1'b0;
            data_out <= 1'b0;
        end else if (!sending) begin
            data_out <= 1'b0;
            if (ticks == RESET_TICKS - 1) begin
                // First frame also waits a full reset/latch interval.
                shift_data <= {rgb[15:8], rgb[23:16], rgb[7:0]};
                bit_index <= 0;
                ticks <= 0;
                sending <= 1'b1;
                data_out <= 1'b1;
            end else begin
                ticks <= ticks + 1'b1;
            end
        end else if (ticks == BIT_TICKS - 1) begin
            ticks <= 0;
            if (bit_index == 23) begin
                sending <= 1'b0;
                data_out <= 1'b0;
            end else begin
                bit_index <= bit_index + 1'b1;
                shift_data <= {shift_data[22:0], 1'b0};
                data_out <= 1'b1;
            end
        end else begin
            ticks <= ticks + 1'b1;
            if (ticks == high_ticks - 1)
                data_out <= 1'b0;
        end
    end
endmodule
