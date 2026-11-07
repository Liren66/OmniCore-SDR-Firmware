`timescale 1ns / 1ps
// Self-checking testbench for CH32H417_IP 1.2 (time base + framed RX/TX).
// ADC data encode a sample counter s: I0=s[11:0], Q0=s[23:12], I1=I0^A5A,
// Q1=Q0^5A5. Because time_now also advances once per sample, every received
// sample must satisfy s == time - offset with one constant offset; this checks
// header timestamps sample-exactly, including after overflow gaps.
module tb;
    localparam real T_AXI = 10.0, T_USB = 7.5, T_L = 4.0, T_REF = 25.0;
    reg axi_clk = 0, usb_clk = 0, l_clk = 0, ref_clk = 0;
    always #(T_AXI/2) axi_clk = ~axi_clk;
    always #(T_USB/2) usb_clk = ~usb_clk;
    always #(T_L/2)   l_clk   = ~l_clk;
    always #(T_REF/2) ref_clk = ~ref_clk;
    reg resetn = 0;
    integer errors = 0;
    task automatic fail(input string msg);
        errors++;
        $display("[%0t] ERROR: %s", $time, msg);
    endtask

    // ---------------- ADC / DAC interface model (2R2T, one sample per 4 clocks)
    reg [1:0] ph = 0;
    reg [23:0] sc = 0;
    reg adc_v = 0, dac_v = 0;
    reg [15:0] ai0 = 0, aq0 = 0, ai1 = 0, aq1 = 0;
    always @(posedge l_clk) begin
        ph <= ph + 1'b1;
        adc_v <= (ph == 2'd0);
        dac_v <= (ph == 2'd2);
        if (ph == 2'd0) begin
            ai0 <= {4'h0, sc[11:0]};
            aq0 <= {4'h0, sc[23:12]};
            ai1 <= {4'h0, sc[11:0] ^ 12'hA5A};
            aq1 <= {4'h0, sc[23:12] ^ 12'h5A5};
            sc <= sc + 1'b1;
        end
    end

    // ---------------- CH32 UHSIF bus model
    reg cs_n = 1, wr_n = 1, rd_n = 1, oe_n = 1, eop_n = 1;
    reg [1:0] sel = 2'b11;
    reg [31:0] dout_tb = 0;
    reg drive = 0;
    wire [31:0] bus = drive ? dout_tb : 32'bz;
    wire rdne, wrnf, irq;
    reg pps_ext = 0;

    // ---------------- AXI master
    reg [6:0] awaddr = 0, araddr = 0;
    reg awvalid = 0, wvalid = 0, arvalid = 0;
    reg [31:0] wdata = 0;
    wire awready, wready, bvalid, arready, rvalid;
    wire [1:0] bresp, rresp;
    wire [31:0] rdata;
    wire [15:0] do_i0, do_q0, do_i1, do_q1;

    CH32H417_IP_v1_0 #(.C_S00_AXI_ADDR_WIDTH(7), .REF_HZ(1000)) dut (
        .Interrupt_flag(irq),
        .CH32H417_CLK(usb_clk), .CH32H417_CS_n(cs_n), .CH32H417_WR_n(wr_n),
        .CH32H417_RD_n(rd_n), .CH32H417_OE_n(oe_n), .CH32H417_EOP_n(eop_n),
        .CH32H417_SEL(sel), .CH32H417_DATA(bus), .CH32H417_RDNE(rdne), .CH32H417_WRNF(wrnf),
        .CH32H417_AF_n(), .CH32H417_AE_n(),
        .adc_clk(l_clk), .adc_data_i0(ai0), .adc_data_q0(aq0), .adc_data_i1(ai1), .adc_data_q1(aq1),
        .adc_valid_i0(adc_v), .adc_valid_q0(adc_v), .adc_valid_i1(adc_v), .adc_valid_q1(adc_v),
        .adc_enable_i0(1'b1), .adc_enable_q0(1'b1), .adc_enable_i1(1'b1), .adc_enable_q1(1'b1),
        .dac_clk(l_clk), .dac_data_i0(16'h0), .dac_data_q0(16'h0), .dac_data_i1(16'h0), .dac_data_q1(16'h0),
        .dac_valid_i0(dac_v), .dac_valid_q0(dac_v), .dac_valid_i1(dac_v), .dac_valid_q1(dac_v),
        .dac_enable_i0(1'b1), .dac_enable_q0(1'b1), .dac_enable_i1(1'b1), .dac_enable_q1(1'b1),
        .dac_data_i0_out(do_i0), .dac_data_q0_out(do_q0), .dac_data_i1_out(do_i1), .dac_data_q1_out(do_q1),
        .pps_ext(pps_ext), .ref_clk_in(ref_clk),
        .s00_axi_aclk(axi_clk), .s00_axi_aresetn(resetn),
        .s00_axi_awaddr(awaddr), .s00_axi_awprot(3'b0), .s00_axi_awvalid(awvalid), .s00_axi_awready(awready),
        .s00_axi_wdata(wdata), .s00_axi_wstrb(4'hF), .s00_axi_wvalid(wvalid), .s00_axi_wready(wready),
        .s00_axi_bresp(bresp), .s00_axi_bvalid(bvalid), .s00_axi_bready(1'b1),
        .s00_axi_araddr(araddr), .s00_axi_arprot(3'b0), .s00_axi_arvalid(arvalid), .s00_axi_arready(arready),
        .s00_axi_rdata(rdata), .s00_axi_rresp(rresp), .s00_axi_rvalid(rvalid), .s00_axi_rready(1'b1)
    );
    wire [63:0] dut_time = dut.CH32H417_IP_v1_0_S00_AXI_inst.u_timekeeper.time_now;

    task automatic axi_write(input [6:0] a, input [31:0] d);
        @(posedge axi_clk); awaddr <= a; wdata <= d; awvalid <= 1; wvalid <= 1;
        @(negedge axi_clk); while (!awready) @(negedge axi_clk);
        @(posedge axi_clk); awvalid <= 0; wvalid <= 0;
        @(negedge axi_clk); while (!bvalid) @(negedge axi_clk);
        @(posedge axi_clk);
    endtask
    task automatic axi_read(input [6:0] a, output [31:0] d);
        @(posedge axi_clk); araddr <= a; arvalid <= 1;
        @(negedge axi_clk); while (!arready) @(negedge axi_clk);
        @(posedge axi_clk); arvalid <= 0;
        @(negedge axi_clk); while (!rvalid) @(negedge axi_clk);
        d = rdata;
        @(posedge axi_clk);
    endtask
    function automatic [63:0] u64(input [31:0] lo, input [31:0] hi); return {hi, lo}; endfunction
    task automatic read_time(output [63:0] t);
        reg [31:0] lo, hi;
        axi_read(7'h20, lo); axi_read(7'h24, hi); t = {hi, lo};
    endtask
    task automatic read_pps(output [63:0] t);
        reg [31:0] lo, hi;
        axi_read(7'h28, lo); axi_read(7'h2C, hi); t = {hi, lo};
    endtask
    task automatic set_ctrl0(input [31:0] v);
        reg [31:0] st;
        integer n;
        axi_write(7'h00, v);
        n = 0;
        do begin
            repeat (20) @(posedge axi_clk);
            axi_read(7'h08, st);
            n++;
        end while (st[14] && n < 1000);
        if (st[27]) fail($sformatf("CFG_ERROR after CTRL0=%h", v));
        repeat (200) @(posedge axi_clk);
    endtask

    // ---------------- events
    typedef struct { byte unsigned code; bit lost; int unsigned seq; longint unsigned t; } evt_t;
    evt_t evq[$];
    task automatic drain_events();
        reg [31:0] st, info, lo, hi;
        evt_t e;
        forever begin
            axi_read(7'h40, st);
            if (!st[0]) break;
            axi_read(7'h44, info); axi_read(7'h48, lo); axi_read(7'h4C, hi);
            axi_write(7'h40, 32'h1);
            e.code = info[31:24]; e.lost = info[23]; e.seq = info[15:0]; e.t = {hi, lo};
            $display("[%0t] EVENT code=%02h seq=%0d lost=%0d time=%0d", $time, e.code, e.seq, e.lost, e.t);
            evq.push_back(e);
        end
    endtask
    function automatic int count_events(input byte unsigned code);
        int n = 0;
        foreach (evq[i]) if (evq[i].code == code) n++;
        return n;
    endfunction

    // ---------------- USB transactions
    typedef bit [31:0] block_t [4096];
    task automatic usb_read_block(output block_t w, input int timeout_us = 2000);
        bit [31:0] arr [4101];
        realtime t0 = $realtime;
        @(negedge usb_clk);
        while (!rdne) begin
            @(negedge usb_clk);
            if ($realtime - t0 > timeout_us * 1000.0) begin fail("RDNE timeout"); return; end
        end
        sel = 2'b01; oe_n = 0;
        @(negedge usb_clk); cs_n = 0; rd_n = 0;
        for (int k = 0; k < 4101; k++) begin
            @(posedge usb_clk); #0.5; arr[k] = bus;
        end
        @(negedge usb_clk); rd_n = 1; oe_n = 1;
        @(negedge usb_clk); cs_n = 1; sel = 2'b11;
        for (int i = 0; i < 4096; i++) w[i] = arr[i + 1];
    endtask
    task automatic usb_write_block(input block_t w);
        @(negedge usb_clk);
        while (!wrnf) @(negedge usb_clk);
        sel = 2'b00;
        @(negedge usb_clk); cs_n = 0; wr_n = 0; drive = 1;
        for (int k = 0; k < 4096; k++) begin dout_tb = w[k]; @(negedge usb_clk); end
        wr_n = 1;
        @(negedge usb_clk); cs_n = 1; drive = 0; sel = 2'b11;
    endtask

    // ---------------- header helpers
    function automatic byte unsigned hcheck(input bit [31:0] h0, h1, h2, h3);
        byte unsigned c = h0[31:24] ^ h0[23:16] ^ h0[15:8];
        for (int k = 0; k < 4; k++) c ^= h1[k*8 +: 8] ^ h2[k*8 +: 8] ^ h3[k*8 +: 8];
        return c;
    endfunction
    function automatic byte unsigned wbyte(const ref block_t w, input int idx); // payload byte idx
        int word = 4 + idx / 4;
        return w[word][(idx % 4) * 8 +: 8];
    endfunction
    typedef struct { bit ok; byte unsigned flags; int unsigned seq, valid; longint unsigned t; } hdr_t;
    function automatic hdr_t parse_hdr(const ref block_t w);
        hdr_t h;
        h.ok = (w[0][31:16] == 16'h4F43) && (w[0][7:0] == hcheck(w[0], w[1], w[2], w[3]));
        h.flags = w[0][15:8]; h.seq = w[1][31:16]; h.valid = w[1][15:0]; h.t = {w[3], w[2]};
        return h;
    endfunction

    // mode: 0 dual12, 1 single12, 2 iq8
    longint off = 0; bit off_known = 0;
    task automatic check_payload(const ref block_t w, input hdr_t h, input int mode);
        int fb = (mode == 0) ? 6 : (mode == 1) ? 3 : 4;
        int bad = 0;
        for (int k = 0; k < h.valid; k++) begin
            byte unsigned b[6];
            bit [11:0] i0, q0, i1, q1;
            bit [23:0] s, e;
            for (int j = 0; j < fb; j++) b[j] = wbyte(w, k * fb + j);
            if (mode == 2) begin
                e = h.t + k - off;
                if (b[0] != e[11:4] || b[1] != e[23:16] || b[2] != ((e[11:0] ^ 12'hA5A) >> 4) ||
                    b[3] != ((e[23:12] ^ 12'h5A5) >> 4)) bad++;
                continue;
            end
            i0 = {b[0], b[1][7:4]}; q0 = {b[1][3:0], b[2]};
            s = {q0, i0};
            if (!off_known) begin off = h.t + k - s; off_known = 1; $display("sample/time offset = %0d", off); end
            e = h.t + k - off;
            if (s != e) bad++;
            if (mode == 0) begin
                i1 = {b[3], b[4][7:4]}; q1 = {b[4][3:0], b[5]};
                if (i1 != (e[11:0] ^ 12'hA5A) || q1 != (e[23:12] ^ 12'h5A5)) bad++;
            end
        end
        for (int k = h.valid * fb; k < 16368; k++) if (wbyte(w, k) != 0) begin bad++; break; end
        if (bad) fail($sformatf("payload mismatch in block seq=%0d (%0d bad)", h.seq, bad));
    endtask

    // ---------------- DAC monitor
    longint dac_start[$];         // time_now at the first frame of each burst
    int dac_frames[$];
    int cur_frames = 0; bit in_out = 0; longint cur_start;
    int dac_seq_err = 0;
    // Like axi_ad9361: the DAC consumes the value presented two clocks before
    // the exported dac_valid strobe (dac_data_out_int register + valid delay).
    reg [15:0] p1_i0, p1_q0, p1_i1, p1_q1, p2_i0, p2_q0, p2_i1, p2_q1;
    always @(posedge l_clk) begin
        {p2_i0, p2_q0, p2_i1, p2_q1} <= {p1_i0, p1_q0, p1_i1, p1_q1};
        {p1_i0, p1_q0, p1_i1, p1_q1} <= {do_i0, do_q0, do_i1, do_q1};
    end
    always @(posedge l_clk) if (dac_v) begin
        if (p2_q0 == {12'h123, 4'h0}) begin
            automatic bit [23:0] k = {p2_q1[15:4], p2_i0[15:4]};
            if (!in_out) begin in_out = 1; cur_start = dut_time; cur_frames = 0; end
            if (k != cur_frames + 1) dac_seq_err++;
            cur_frames++;
            if (p2_i1 != {12'h456, 4'h0}) dac_seq_err++;
        end else if (in_out) begin
            in_out = 0; dac_start.push_back(cur_start); dac_frames.push_back(cur_frames);
            $display("[%0t] DAC burst: start time=%0d frames=%0d", $time, cur_start, cur_frames);
        end
    end
    function automatic block_t tx_block(input byte unsigned flags, input int seq, input int valid,
                                        input longint unsigned t, input int first);
        block_t w;
        byte unsigned pb[16368];
        foreach (pb[i]) pb[i] = 0;
        for (int k = 0; k < valid; k++) begin
            bit [23:0] n = first + k + 1;
            bit [11:0] i0 = n[11:0], q0 = 12'h123, i1 = 12'h456, q1 = n[23:12];
            pb[k*6+0] = i0[11:4]; pb[k*6+1] = {i0[3:0], q0[11:8]}; pb[k*6+2] = q0[7:0];
            pb[k*6+3] = i1[11:4]; pb[k*6+4] = {i1[3:0], q1[11:8]}; pb[k*6+5] = q1[7:0];
        end
        w[1] = {seq[15:0], valid[15:0]}; w[2] = t[31:0]; w[3] = t[63:32];
        w[0] = {16'h4F43, flags, 8'h00};
        w[0][7:0] = hcheck(w[0], w[1], w[2], w[3]);
        for (int i = 0; i < 4092; i++) w[4 + i] = {pb[i*4+3], pb[i*4+2], pb[i*4+1], pb[i*4+0]};
        return w;
    endfunction

    // ======================================================================
    initial begin
        block_t w;
        hdr_t h, prev;
        reg [31:0] r;
        reg [63:0] t0, t1, tp;
        $timeformat(-6, 2, " us", 10);
        repeat (20) @(posedge axi_clk);
        resetn = 1;
        repeat (100) @(posedge axi_clk);

        // ---------------------------------------------------------------- A
        $display("=== A: version / set time now");
        axi_read(7'h54, r); if (r != 32'h0103_0001) fail("version");
        axi_read(7'h08, r); if (!r[30] || !r[31]) fail("time/framed capability bits");
        axi_write(7'h10, 32'd1000); axi_write(7'h14, 32'd0); axi_write(7'h18, 32'h1);
        repeat (100) @(posedge axi_clk);
        axi_read(7'h1C, r); if (!r[1]) fail("time_valid after NOW");
        read_time(t0); repeat (100) @(posedge axi_clk); read_time(t1);
        $display("time %0d -> %0d", t0, t1);
        if (!(t0 >= 1000 && t1 > t0 && t1 - t0 < 400)) fail("time not running from 1000");

        // ---------------------------------------------------------------- B
        $display("=== B: next PPS (external)");
        axi_write(7'h10, 32'h0000_5000); axi_write(7'h14, 32'h1); axi_write(7'h18, 32'h12); // NEXT_PPS, ext
        repeat (50) @(posedge axi_clk);
        axi_read(7'h1C, r); if (!r[0]) fail("not armed");
        read_time(t0); if (t0 >= 64'h1_0000_5000) fail("loaded before PPS");
        #2000 pps_ext = 1; #1000 pps_ext = 0;
        repeat (50) @(posedge axi_clk);
        axi_read(7'h1C, r); if (r[0]) fail("still armed after PPS");
        read_pps(tp); read_time(t1);
        $display("last_pps=%h now=%h", tp, t1);
        if (tp != 64'h1_0000_5000) fail("last_pps != loaded value");
        if (!(t1 > tp && t1 - tp < 200)) fail("time after PPS load");
        axi_read(7'h50, r); if (r != 1) fail("pps count");
        // a second edge latches last_pps without reloading
        #3000 pps_ext = 1; #1000 pps_ext = 0;
        repeat (50) @(posedge axi_clk); read_pps(t0);
        if (!(t0 > tp && t0 - tp > 200 && t0 - tp < 400)) fail($sformatf("2nd PPS delta %0d", t0 - tp));

        // ---------------------------------------------------------------- C
        $display("=== C: internal PPS from reference clock (REF_HZ=1000 in sim)");
        axi_write(7'h18, 32'h20);            // source internal, no command
        repeat (50) @(posedge axi_clk);
        axi_read(7'h1C, r); if (r[3:2] != 2) fail("pps source readback"); if (!r[4]) fail("ref present");
        axi_read(7'h50, r); t0 = r;
        #60000;
        axi_read(7'h50, r);
        if (r - t0 < 2 || r - t0 > 3) fail($sformatf("internal PPS edges %0d", r - t0));
        axi_read(7'h50, r); t0 = r; do axi_read(7'h50, r); while (r == t0); read_pps(tp);
        t0 = r; do axi_read(7'h50, r); while (r == t0); read_pps(t1);
        $display("internal PPS spacing = %0d samples (expect 25us/16ns = 1562.5)", t1 - tp);
        if (!(t1 - tp == 1562 || t1 - tp == 1563)) fail("internal PPS spacing");
        axi_write(7'h18, 32'h00);            // PPS off

        // ---------------------------------------------------------------- D
        $display("=== D: framed RX dual 12-bit, continuous, with overflow gap");
        set_ctrl0(32'h104); set_ctrl0(32'h105);
        prev.seq = 0;
        for (int b = 0; b < 4; b++) begin
            usb_read_block(w); h = parse_hdr(w);
            $display("RX blk seq=%0d flags=%02h valid=%0d time=%0d", h.seq, h.flags, h.valid, h.t);
            if (!h.ok) fail("header check");
            if (h.valid != 2728) fail("valid != 2728");
            if (!h.flags[0]) fail("TIME_VALID flag");
            if (b == 0 && !h.flags[1]) fail("SOB on first block");
            if (b > 0 && (h.seq != prev.seq + 1 || h.t != prev.t + 2728)) fail("block continuity");
            check_payload(w, h, 0);
            prev = h;
        end
        $display("... stalling host reader to force an overflow");
        #250000;
        begin
            automatic int gaps = 0, pads = 0;
            automatic longint lost = 0;
            for (int b = 0; b < 7; b++) begin
                usb_read_block(w); h = parse_hdr(w);
                $display("RX blk seq=%0d flags=%02h valid=%0d time=%0d", h.seq, h.flags, h.valid, h.t);
                if (!h.ok) fail("header check");
                if (h.seq != prev.seq + 1) fail("seq continuity");
                if (h.flags[5]) pads++;
                if (h.flags[2]) begin
                    gaps++;
                    lost = h.t - (prev.t + prev.valid);
                    $display("GAP: %0d samples lost (exact, from timestamps)", lost);
                    if (lost <= 0) fail("gap without lost samples");
                end else if (h.t != prev.t + prev.valid) fail("time continuity without GAP");
                check_payload(w, h, 0);
                prev = h;
            end
            if (gaps != 1 || pads != 1) fail($sformatf("expected 1 PAD + 1 GAP, got %0d/%0d", pads, gaps));
        end
        drain_events();
        if (count_events(8'h10) != 1) fail("RX_OVERFLOW event");
        set_ctrl0(32'h104); set_ctrl0(32'h0);

        // ---------------------------------------------------------------- E
        $display("=== E: timed RX commands");
        set_ctrl0(32'h304);   // DUAL|FRAMED|RX_TIMED (RX off)
        set_ctrl0(32'h305);   // + RX_EN, waits for a command
        repeat (2000) @(posedge axi_clk);
        axi_read(7'h0C, r); if (!r[31]) fail("RX_TIMED applied bit");
        read_time(t0);
        t1 = t0 + 3000;
        axi_write(7'h30, t1[31:0]); axi_write(7'h34, t1[63:32]); axi_write(7'h38, 32'd3000);
        axi_write(7'h3C, 32'h0);
        usb_read_block(w); h = parse_hdr(w);
        $display("timed blk seq=%0d flags=%02h valid=%0d time=%0d (cmd %0d)", h.seq, h.flags, h.valid, h.t, t1);
        if (h.t != t1) fail("timed start time");
        if (!h.flags[1] || h.flags[4] || h.valid != 2728) fail("timed first block flags/valid");
        check_payload(w, h, 0);
        usb_read_block(w); h = parse_hdr(w);
        $display("timed blk seq=%0d flags=%02h valid=%0d time=%0d", h.seq, h.flags, h.valid, h.t);
        if (h.valid != 272 || !h.flags[3] || !h.flags[5] || h.t != t1 + 2728) fail("timed EOB block");
        check_payload(w, h, 0);
        // late command
        read_time(t0); t1 = t0 - 10;
        axi_write(7'h30, t1[31:0]); axi_write(7'h34, t1[63:32]); axi_write(7'h38, 32'd50);
        axi_write(7'h3C, 32'h0);
        usb_read_block(w); h = parse_hdr(w);
        $display("late blk flags=%02h valid=%0d time=%0d (cmd %0d)", h.flags, h.valid, h.t, t1);
        if (!h.flags[4] || !h.flags[3] || h.valid != 50 || h.t <= t1) fail("late command block");
        check_payload(w, h, 0);
        drain_events();
        if (count_events(8'h11) != 1) fail("RX_LATE event");
        // immediate command, then a stop while streaming continuously
        axi_write(7'h38, 32'd0); axi_write(7'h3C, 32'h1);
        usb_read_block(w); h = parse_hdr(w);
        if (!h.flags[1] || h.valid != 2728) fail("immediate continuous start");
        check_payload(w, h, 0);
        axi_write(7'h3C, 32'h2);  // stop
        for (int b = 0; b < 6; b++) begin
            usb_read_block(w); h = parse_hdr(w);
            check_payload(w, h, 0);
            if (h.flags[3]) break;
            if (b == 5) fail("no EOB after stop");
        end
        $display("stop -> EOB block valid=%0d", h.valid);
        repeat (3000) @(posedge axi_clk);
        if (rdne) fail("data after stop/EOB");
        set_ctrl0(32'h304); set_ctrl0(32'h0);

        // ---------------------------------------------------------------- F
        $display("=== F: framed single-channel and IQ8 RX");
        set_ctrl0(32'h100); set_ctrl0(32'h101);
        for (int b = 0; b < 2; b++) begin
            usb_read_block(w); h = parse_hdr(w);
            $display("single blk seq=%0d flags=%02h valid=%0d", h.seq, h.flags, h.valid);
            if (!h.ok || h.valid != 5456) fail("single valid");
            check_payload(w, h, 1);
        end
        set_ctrl0(32'h100); set_ctrl0(32'h194); set_ctrl0(32'h195);
        for (int b = 0; b < 2; b++) begin
            usb_read_block(w); h = parse_hdr(w);
            $display("iq8 blk seq=%0d flags=%02h valid=%0d", h.seq, h.flags, h.valid);
            if (!h.ok || h.valid != 4092) fail("iq8 valid");
            check_payload(w, h, 2);
            if (b == 1 && h.t != prev.t + 4092) fail("iq8 time continuity");
            prev = h;
        end
        set_ctrl0(32'h194); set_ctrl0(32'h0);

        // ---------------------------------------------------------------- G
        $display("=== G: timed TX bursts (dual 12-bit)");
        evq.delete();
        set_ctrl0(32'h110); set_ctrl0(32'h112);           // TX_EN|TX_DUAL|FRAMED
        set_ctrl0(32'h152);           // + TX_RUN
        read_time(t0); t1 = t0 + 8000;
        usb_write_block(tx_block(8'h03, 0, 2728, t1, 0));        // HAS_TIME|SOB
        usb_write_block(tx_block(8'h08, 1, 1000, 0, 2728));      // EOB
        wait (dac_start.size() == 1);
        #2000;
        $display("burst1 T=%0d start=%0d frames=%0d", t1, dac_start[0], dac_frames[0]);
        if (dac_frames[0] != 3728) fail("burst1 length");
        read_time(t0); tp = t0 + 6000;
        usb_write_block(tx_block(8'h0B, 0, 500, tp, 0));         // HAS_TIME|SOB|EOB
        wait (dac_start.size() == 2);
        $display("burst2 T=%0d start=%0d frames=%0d", tp, dac_start[1], dac_frames[1]);
        if (dac_frames[1] != 500) fail("burst2 length");
        if (dac_start[0] - t1 != dac_start[1] - tp)
            fail($sformatf("TX start offset differs: %0d vs %0d", dac_start[0] - t1, dac_start[1] - tp));
        else $display("TX start offset = %0d sample(s), identical for both bursts", dac_start[0] - t1);
        // late burst: must not play
        read_time(t0);
        usb_write_block(tx_block(8'h0B, 0, 300, t0 - 20, 0));
        #20000;
        if (dac_start.size() != 2) fail("late burst was played");
        // untimed burst plays immediately
        usb_write_block(tx_block(8'h0A, 0, 100, 0, 0));
        #10000;
        if (dac_start.size() != 3 || dac_frames[2] != 100) fail("untimed burst");
        // underflow: a burst without EOB and no further data
        usb_write_block(tx_block(8'h02, 0, 2728, 0, 0));
        #60000;
        drain_events();
        if (count_events(8'h01) != 3) fail($sformatf("burst ACK events %0d", count_events(8'h01)));
        if (count_events(8'h03) != 1) fail("TX_LATE event");
        if (count_events(8'h02) < 1) fail("TX_UNDERFLOW event");
        if (dac_seq_err) fail($sformatf("DAC frame sequence errors %0d", dac_seq_err));
        set_ctrl0(32'h110); set_ctrl0(32'h0);

        // ---------------------------------------------------------------- H
        $display("=== H: framed-only: CTRL0 bit8 is ignored, the first RX block has a header");
        set_ctrl0(32'h004); set_ctrl0(32'h005);
        usb_read_block(w); h = parse_hdr(w);
        if (!h.ok || !h.flags[1] || h.valid != 2728) fail("first block without bit8 is not a framed SOB block");
        check_payload(w, h, 0);
        set_ctrl0(32'h004); set_ctrl0(32'h0);
        axi_read(7'h08, r);
        if (r[0] || r[3]) fail("legacy overflow/underflow error bits must stay 0");

        $display("==============================================");
        if (errors == 0) $display("TB RESULT: PASS");
        else $display("TB RESULT: FAIL (%0d errors)", errors);
        $finish;
    end
    initial begin
        #20ms;
        $display("TB RESULT: FAIL (global timeout)");
        $finish;
    end
endmodule
