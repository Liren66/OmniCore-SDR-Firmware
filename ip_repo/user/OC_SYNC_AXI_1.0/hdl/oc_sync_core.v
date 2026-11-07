`timescale 1ns / 1ps
`default_nettype none

// OmniCore multi-board synchronisation core, 40 MHz reference domain.
//
// clk_40m is the board's own 40 MHz VCTCXO (CLK_FB_FPGA, the same clock that
// feeds AD9361 XTALN). Everything that must be phase-related to that clock
// lives here:
//   REF_OUT : clk_40m / 4 = 10 MHz for the next board's ADF4002 (daisy chain).
//   TRIG    : sync pulse for every board's AD9361_SYNC (1:N split). Fired by
//             command, periodically, or at the next PPS_EXT rising edge.
//   SYNC/PPS: rising-edge counters and the interval between the last two
//             edges in clk_40m cycles. With a common reference the interval
//             is exactly the generator period, so it doubles as lock check.
// Both outputs go through ODDR so the launch edge is an IOB clock edge;
// trig_falling moves the TRIG transitions half a 40 MHz cycle later.
module oc_sync_core (
    input  wire        clk,             // clk_40m
    input  wire        rstn,            // synchronous to clk

    // Configuration (quasi-static, applied with cfg_pulse) and commands.
    input  wire        cfg_pulse,
    input  wire        ref_en,
    input  wire        trig_en,
    input  wire        trig_falling,
    input  wire        periodic_en,
    input  wire        loopback,
    input  wire [15:0] trig_width,
    input  wire [31:0] trig_period,
    input  wire        cmd_fire,
    input  wire        cmd_arm_pps,
    input  wire        cmd_disarm,
    input  wire        cmd_clear,

    input  wire        sync_pin,        // AD9361_SYNC (asynchronous)
    input  wire        pps_pin,         // PPS_EXT (asynchronous)
    output wire        trig_int,        // TRIG level in this domain (loopback / PPS mux)

    output reg  [31:0] sync_count,
    output reg  [31:0] sync_interval,
    output reg  [31:0] pps_count,
    output reg  [31:0] pps_interval,
    output reg  [31:0] trig_count,
    output wire [3:0]  flags,           // 0 armed, 1 trig active, 2 sync level, 3 pps level
    output wire [3:0]  fdiv,            // free-running divider, bit 3 for frequency count

    output wire        ref_out,
    output wire        trig_out
);
    localparam [31:0] MIN_PERIOD = 32'd1000;

    // ------------------------------------------------------------------
    // Applied configuration.
    // ------------------------------------------------------------------
    reg        r_ref_en, r_trig_en, r_falling, r_periodic, r_loop;
    reg [15:0] r_width;
    reg [31:0] r_period;
    always @(posedge clk) begin
        if (!rstn) begin
            r_ref_en <= 1'b0; r_trig_en <= 1'b0; r_falling <= 1'b0;
            r_periodic <= 1'b0; r_loop <= 1'b0;
            r_width <= 16'd80; r_period <= 32'd40000000;
        end else if (cfg_pulse) begin
            r_ref_en <= ref_en; r_trig_en <= trig_en; r_falling <= trig_falling;
            r_periodic <= periodic_en; r_loop <= loopback;
            r_width <= (trig_width == 16'd0) ? 16'd1 : trig_width;
            r_period <= (trig_period < MIN_PERIOD) ? MIN_PERIOD : trig_period;
        end
    end

    // ------------------------------------------------------------------
    // 10 MHz reference: bit 1 of a free-running counter, 50 % duty.
    // ------------------------------------------------------------------
    reg [3:0] div;
    always @(posedge clk) begin
        if (!rstn) div <= 4'd0;
        else div <= div + 1'b1;
    end
    assign fdiv = div;
    reg ref_q;
    always @(posedge clk) ref_q <= rstn && r_ref_en && div[1];

    // ------------------------------------------------------------------
    // Input synchronisers and rising edges.
    // ------------------------------------------------------------------
    reg trig_q;
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg [2:0] sync_s;
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg [2:0] pps_s;
    always @(posedge clk) begin
        sync_s <= {sync_s[1:0], r_loop ? trig_q : sync_pin};
        pps_s  <= {pps_s[1:0], pps_pin};
    end
    wire sync_edge = sync_s[1] & ~sync_s[2];
    wire pps_edge  = pps_s[1] & ~pps_s[2];

    // ------------------------------------------------------------------
    // TRIG generator.
    // ------------------------------------------------------------------
    reg        armed;
    reg [31:0] per_cnt;
    reg [15:0] width_cnt;
    // A command arriving with a new configuration uses that configuration.
    wire en_now = cfg_pulse ? trig_en : r_trig_en;
    wire per_fire = r_periodic && !cfg_pulse && (per_cnt == 32'd0);
    // A request while a pulse is still high is dropped (no new edge): keep
    // TRIG_WIDTH below TRIG_PERIOD. An armed PPS request then stays armed.
    wire fire = en_now && !trig_q && (cmd_fire || per_fire || (armed && pps_edge));
    always @(posedge clk) begin
        if (!rstn) begin
            armed <= 1'b0; per_cnt <= 32'd0; width_cnt <= 16'd0;
            trig_q <= 1'b0; trig_count <= 32'd0;
        end else begin
            if (armed && pps_edge && fire) armed <= 1'b0;
            if (cmd_arm_pps && en_now) armed <= 1'b1;
            if (cmd_disarm || !en_now) armed <= 1'b0;

            if (!r_periodic || cfg_pulse) per_cnt <= 32'd0;
            else per_cnt <= (per_cnt >= r_period - 1'b1) ? 32'd0 : per_cnt + 1'b1;

            if (!en_now) begin
                width_cnt <= 16'd0;
                trig_q <= 1'b0;
            end else if (fire) begin
                width_cnt <= (cfg_pulse ? ((trig_width == 16'd0) ? 16'd1 : trig_width)
                                        : r_width) - 1'b1;
                trig_q <= 1'b1;
                trig_count <= trig_count + 1'b1;
            end else if (width_cnt != 16'd0) begin
                width_cnt <= width_cnt - 1'b1;
            end else begin
                trig_q <= 1'b0;
            end
            if (cmd_clear) trig_count <= 32'd0;
        end
    end
    assign trig_int = trig_q;
    reg trig_d1;
    always @(posedge clk) trig_d1 <= trig_q;

    // ------------------------------------------------------------------
    // Edge counters and intervals (clk_40m cycles between rising edges).
    // ------------------------------------------------------------------
    reg [31:0] sync_age, pps_age;
    reg sync_seen, pps_seen;
    always @(posedge clk) begin
        if (!rstn || cmd_clear) begin
            sync_count <= 0; sync_interval <= 0; sync_age <= 0; sync_seen <= 0;
            pps_count <= 0; pps_interval <= 0; pps_age <= 0; pps_seen <= 0;
        end else begin
            if (sync_edge) begin
                sync_count <= sync_count + 1'b1;
                if (sync_seen) sync_interval <= sync_age + 1'b1;
                sync_seen <= 1'b1;
                sync_age <= 0;
            end else if (sync_age != 32'hFFFFFFFF) begin
                sync_age <= sync_age + 1'b1;
            end
            if (pps_edge) begin
                pps_count <= pps_count + 1'b1;
                if (pps_seen) pps_interval <= pps_age + 1'b1;
                pps_seen <= 1'b1;
                pps_age <= 0;
            end else if (pps_age != 32'hFFFFFFFF) begin
                pps_age <= pps_age + 1'b1;
            end
        end
    end
    assign flags = {pps_s[1], sync_s[1], trig_q, armed};

    // ------------------------------------------------------------------
    // Output registers in the IOBs. SAME_EDGE ODDR: Q = D1 while clk is
    // high, D2 while low. Falling launch: D1 = previous, D2 = new value.
    // ------------------------------------------------------------------
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE"), .INIT(1'b0), .SRTYPE("SYNC")) u_ref_oddr (
        .Q(ref_out), .C(clk), .CE(1'b1), .D1(ref_q), .D2(ref_q), .R(1'b0), .S(1'b0));
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE"), .INIT(1'b0), .SRTYPE("SYNC")) u_trig_oddr (
        .Q(trig_out), .C(clk), .CE(1'b1),
        .D1(r_falling ? trig_d1 : trig_q), .D2(trig_q), .R(1'b0), .S(1'b0));
endmodule

`default_nettype wire
