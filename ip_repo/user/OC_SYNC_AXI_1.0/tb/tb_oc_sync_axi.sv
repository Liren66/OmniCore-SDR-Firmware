`timescale 1ns / 1ps
// Self-checking testbench for oc_sync_axi. Last line: TB RESULT: PASS|FAIL.
module tb;
    localparam integer GATE = 1000000;   // CLK_FREQ_HZ parameter: 10 ms gate at 100 MHz
    reg aclk = 0, clk40 = 0, aresetn = 0;
    always #5 aclk = ~aclk;               // 100 MHz
    always #12.5 clk40 = ~clk40;          // 40 MHz

    reg  [11:0] awaddr = 0, araddr = 0;
    reg  [31:0] wdata = 0;
    reg  awvalid = 0, wvalid = 0, bready = 1, arvalid = 0, rready = 1;
    wire awready, wready, bvalid, arready, rvalid;
    wire [1:0] bresp, rresp;
    wire [31:0] rdata;
    reg  pps = 0, sync_force = 0, cable = 0;
    wire ref_out, trig_out, pps_out;
    // Optional external TRIG -> SYNC cable with 7 ns delay.
    wire sync_pin;
    assign #7 sync_pin = cable ? trig_out : sync_force;

    oc_sync_axi #(.CLK_FREQ_HZ(GATE)) dut (
        .s_axi_aclk(aclk), .s_axi_aresetn(aresetn),
        .s_axi_awaddr(awaddr), .s_axi_awprot(3'd0), .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(4'hF), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arprot(3'd0), .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp), .s_axi_rvalid(rvalid), .s_axi_rready(rready),
        .clk_40m(clk40), .sync_in(sync_pin), .pps_ext_in(pps), .pps_out(pps_out),
        .ref_out(ref_out), .trig_out(trig_out));

    integer errors = 0;
    task check(input bit cond, input [8*96-1:0] what);
        if (!cond) begin errors = errors + 1; $display("FAIL: %0s", what); end
    endtask

    // BFM drives and samples on the falling edge so the handshake seen by the
    // DUT at the next rising edge is never a simulator race.
    task wr(input [11:0] a, input [31:0] d);
        reg aw_hs, w_hs;
        begin
            @(negedge aclk); awaddr = a; wdata = d; awvalid = 1; wvalid = 1;
            while (awvalid || wvalid) begin
                aw_hs = awvalid && awready; w_hs = wvalid && wready;
                @(negedge aclk);
                if (aw_hs) awvalid = 0;
                if (w_hs) wvalid = 0;
            end
            while (!bvalid) @(negedge aclk);
            @(negedge aclk);
        end
    endtask
    task rd(input [11:0] a, output [31:0] d);
        begin
            @(negedge aclk); araddr = a; arvalid = 1;
            while (!arready) @(negedge aclk);
            @(negedge aclk); arvalid = 0;
            while (!rvalid) @(negedge aclk);
            d = rdata;
            @(negedge aclk);
        end
    endtask
    task wait_idle;
        reg [31:0] s; integer n;
        begin
            n = 0; rd(12'h018, s);
            while (s[0] && n < 100) begin rd(12'h018, s); n = n + 1; end
            if (s[0]) $display("    busy: s=%h xfer_busy=%b cfg_pend=%b cmd_pend=%h req=%b ack=%b t=%0t", s, dut.xfer_busy, dut.cfg_pend, dut.cmd_pend, dut.u_cfg.req, dut.u_cfg.ack, $time);
            check(!s[0], "cfg_busy never cleared");
            repeat (20) @(posedge aclk);   // let the status snapshot refresh
        end
    endtask

    // Measurements on the outputs.
    integer ref_edges = 0; real ref_last = 0, ref_period = 0;
    always @(posedge ref_out) begin
        if (ref_edges) ref_period = $realtime - ref_last;
        ref_last = $realtime; ref_edges = ref_edges + 1;
    end
    real trig_rise = 0, trig_fall = 0; integer trig_rises = 0;
    always @(posedge trig_out) begin trig_rise = $realtime; trig_rises = trig_rises + 1; end
    always @(negedge trig_out) trig_fall = $realtime;

    // 40 MHz phase of a time (0 = at a rising clk40 edge, 12.5 = falling).
    // clk40 starts low, so its rising edges are at 12.5 + 25 k ns.
    function real phase40(input real t);
        phase40 = (t - 12.5) - 25.0 * $floor((t - 12.5) / 25.0);
    endfunction

    reg [31:0] v, c0, w;
    real t_pps;
    integer i;
    initial begin
        repeat (10) @(posedge aclk);
        aresetn = 1;
        repeat (20) @(posedge aclk);

        $display("=== A: ID, version, reset state");
        rd(12'h000, v); check(v == 32'h4F435359, "ID");
        rd(12'h004, v); check(v == 32'h00010000, "VERSION");
        rd(12'h00C, v); check(v == 80, "reset TRIG_WIDTH");
        rd(12'h010, v); check(v == 40000000, "reset TRIG_PERIOD");
        rd(12'h040, v); check(rresp == 2'b10, "unmapped read SLVERR");
        wait_idle;
        repeat (400) @(posedge clk40);
        check(ref_edges == 0 && trig_out == 0, "outputs low while disabled");
        rd(12'h018, v); check(v[1], "ref40_present");

        $display("=== B: REF_OUT = 10 MHz");
        wr(12'h008, 32'h1); wait_idle;
        repeat (200) @(posedge clk40);
        check(ref_edges > 40, "REF_OUT toggles");
        check(ref_period > 99.9 && ref_period < 100.1, "REF_OUT period 100 ns");

        $display("=== C: REF40_HZ measurement");
        #(GATE * 10 * 2.2);
        rd(12'h01C, v);
        // Gate = GATE aclk cycles -> expected 40e6 * GATE / 100e6, 16 Hz steps.
        check(v >= 400000 - 32 && v <= 400000 + 32, "REF40_HZ");
        $display("    REF40_HZ=%0d (expect 400000 for a %0d-cycle gate)", v, GATE);

        $display("=== D: one-shot TRIG, rising launch, width");
        wr(12'h00C, 40);                    // 1 us
        wr(12'h008, 32'h3);                 // ref + trig
        wr(12'h014, 32'h1); wait_idle;
        #2000;
        rd(12'h030, v); check(v == 1, "TRIG_COUNT 1");
        check(trig_rises == 1, "one TRIG pulse");
        check((trig_fall - trig_rise) > 999.9 && (trig_fall - trig_rise) < 1000.1, "TRIG width 1 us");
        check(phase40(trig_rise) < 2.0, "rising launch at a clk40 rising edge");

        $display("=== E: falling launch");
        wr(12'h008, 32'h7);
        wr(12'h014, 32'h1); wait_idle; #2000;
        check(trig_rises == 2, "second TRIG pulse");
        check(phase40(trig_rise) > 12.0 && phase40(trig_rise) < 14.0, "falling launch at a clk40 falling edge");

        $display("=== F: fire merged with enable in one write burst");
        wr(12'h008, 32'h1);                 // trig off
        wait_idle;
        wr(12'h008, 32'h3); wr(12'h014, 32'h1); wait_idle; #2000;
        check(trig_rises == 3, "fire right after enable");
        c0 = trig_rises;
        wr(12'h014, 32'h1); wr(12'h014, 32'h1); wait_idle; #2000;
        check(trig_rises == c0 + 1, "fire during a pulse adds no edge");
        rd(12'h030, v); check(v == 4, "dropped fire not counted");

        $display("=== G: periodic TRIG over the cable, SYNC interval");
        cable = 1;
        wr(12'h014, 32'h8);                 // clear counters
        wr(12'h010, 4000);                  // 100 us
        wr(12'h008, 32'h0B);                // ref + trig + periodic
        wait_idle;
        #(100000 * 5 + 2000);
        rd(12'h020, v); check(v >= 5 && v <= 7, "SYNC_COUNT ~6");
        rd(12'h024, w); check(w == 4000, "SYNC_INTERVAL == period");
        rd(12'h030, c0); check(c0 == v, "TRIG_COUNT == SYNC_COUNT");
        $display("    sync_count=%0d interval=%0d trig_count=%0d", v, w, c0);

        $display("=== H: periodic minimum 1000 cycles");
        wr(12'h010, 5); wait_idle;
        wr(12'h014, 32'h8); wait_idle;
        #(25 * 1000 * 4);
        rd(12'h024, w); check(w == 1000, "period clamped to 1000");

        $display("=== I: internal loopback without cable");
        cable = 0; sync_force = 0;
        wr(12'h008, 32'h03 | 32'h20);       // ref + trig + loopback, not periodic
        wait_idle; #2000;                   // let the last periodic pulse end
        wr(12'h014, 32'h8); wait_idle;
        wr(12'h014, 32'h1); wait_idle; #3000;
        wr(12'h014, 32'h1); wait_idle; #3000;
        rd(12'h020, v); check(v == 2, "loopback SYNC_COUNT 2");
        rd(12'h030, w);
        $display("    loopback sync_count=%0d trig_count=%0d rises=%0d", v, w, trig_rises);

        $display("=== J: PPS interval and arm-at-PPS");
        wr(12'h008, 32'h03);
        wr(12'h014, 32'h8); wait_idle;
        for (i = 0; i < 3; i = i + 1) begin
            #(25 * 40000 - 1013); @(negedge clk40); #3 pps = 1; #1000 pps = 0;
        end
        rd(12'h028, v); check(v == 3, "PPS_COUNT 3");
        rd(12'h02C, w); check(w == 40000, "PPS_INTERVAL 40000");
        $display("    pps_count=%0d pps_interval=%0d", v, w);
        c0 = trig_rises;
        wr(12'h014, 32'h2); wait_idle;
        rd(12'h018, v); check(v[2], "armed");
        #20000; check(trig_rises == c0, "armed TRIG waits for PPS");
        @(negedge clk40); #3 pps = 1; t_pps = $realtime; #1000 pps = 0;
        check(trig_rises == c0 + 1, "TRIG at PPS");
        check(trig_rise - t_pps > 50 && trig_rise - t_pps < 130, "PPS->TRIG latency 3-5 cycles");
        $display("    PPS->TRIG %0.1f ns", trig_rise - t_pps);
        rd(12'h018, v); check(!v[2], "disarmed after firing");
        wr(12'h014, 32'h2); wait_idle;
        wr(12'h014, 32'h4); wait_idle;
        rd(12'h018, v); check(!v[2], "disarm");

        $display("=== K: PPS selection for the time base");
        cable = 0; sync_force = 1; pps = 0;
        wr(12'h008, 32'h03); #10; check(pps_out == 0, "sel PPS_EXT (low)");
        pps = 1; #10; check(pps_out == 1, "sel PPS_EXT (high)"); pps = 0;
        wr(12'h008, 32'h13); #10; check(pps_out == 1, "sel SYNC pin");
        sync_force = 0; #10; check(pps_out == 0, "sel SYNC pin low");
        wr(12'h008, 32'h33); wait_idle;
        wr(12'h014, 32'h1); #200; check(pps_out == 1, "sel SYNC loopback = TRIG");

        $display("=== L: disable drives outputs low");
        wr(12'h008, 32'h0); wait_idle; #500;
        check(trig_out == 0 && ref_out == 0, "outputs low after disable");

        if (errors == 0) $display("TB RESULT: PASS");
        else $display("TB RESULT: FAIL (%0d errors)", errors);
        $finish;
    end
endmodule
