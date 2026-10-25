`timescale 1ns / 1ps
module tb_board_ctrl_axi;
    parameter integer CLK_HZ = 100000000;
    localparam real CLOCK_NS = 1000000000.0 / CLK_HZ;
    reg clk = 0;
    always #(CLOCK_NS / 2.0) clk = ~clk;
    reg resetn = 0;
    reg [11:0] awaddr = 0, araddr = 0;
    reg [31:0] wdata = 0;
    reg [3:0] wstrb = 0;
    reg awvalid = 0, wvalid = 0, arvalid = 0, bready = 0, rready = 0;
    wire awready, wready, arready, bvalid, rvalid, rgb_data, fan_pwm;
    wire [1:0] bresp, rresp;
    wire [31:0] rdata;
    realtime reset_low_time = 0;
    function automatic real abs_real(input real value);
        abs_real = (value < 0.0) ? -value : value;
    endfunction
    board_ctrl_axi #(.CLK_FREQ_HZ(CLK_HZ)) dut (
        .s_axi_aclk(clk), .s_axi_aresetn(resetn),
        .s_axi_awaddr(awaddr), .s_axi_awprot(3'b0),
        .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(wstrb),
        .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arprot(3'b0),
        .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp),
        .s_axi_rvalid(rvalid), .s_axi_rready(rready),
        .rgb_data(rgb_data), .fan_pwm(fan_pwm)
    );

    task automatic wait_clocks(input integer n);
        repeat (n) @(posedge clk);
        #0.1;
    endtask
    task automatic apply_reset;
        @(negedge clk);
        resetn = 0;
        // Synchronous reset drives the pins low at the following rising edge.
        reset_low_time = $realtime + CLOCK_NS / 2.0;
        awvalid = 0; wvalid = 0; arvalid = 0; bready = 0; rready = 0;
        wait_clocks(8);
        if (bvalid || rvalid || rgb_data || fan_pwm || awready || wready || arready)
            $fatal(1, "Reset did not clear outputs/handshakes");
        @(negedge clk); resetn = 1;
    endtask
    task automatic send_aw(input [11:0] address, input integer delay_cycles);
        repeat (delay_cycles) @(negedge clk);
        @(negedge clk); awaddr = address; awvalid = 1;
        @(posedge clk);
        while (!awready) @(posedge clk);
        @(negedge clk); awvalid = 0;
    endtask
    task automatic send_w(input [31:0] value, input [3:0] strobes, input integer delay_cycles);
        repeat (delay_cycles) @(negedge clk);
        @(negedge clk); wdata = value; wstrb = strobes; wvalid = 1;
        @(posedge clk);
        while (!wready) @(posedge clk);
        @(negedge clk); wvalid = 0;
    endtask
    task automatic write_reg(input [11:0] address, input [31:0] value,
        input [3:0] strobes, input integer aw_delay, input integer w_delay,
        input integer stall, input [1:0] expected_resp);
        fork
            send_aw(address, aw_delay);
            send_w(value, strobes, w_delay);
        join
        wait(bvalid); #0.1;
        if (bresp !== expected_resp) $fatal(1, "Write response addr=%h", address);
        repeat (stall) begin
            @(posedge clk); #0.1;
            if (!bvalid || bresp !== expected_resp || awready || wready)
                $fatal(1, "Write response changed while stalled");
        end
        @(negedge clk); bready = 1;
        @(posedge clk);
        @(negedge clk); bready = 0;
    endtask
    task automatic read_reg(input [11:0] address, input [31:0] expected,
        input integer stall, input [1:0] expected_resp);
        @(negedge clk); araddr = address; arvalid = 1;
        @(posedge clk);
        while (!arready) @(posedge clk);
        @(negedge clk); arvalid = 0;
        wait(rvalid); #0.1;
        if (rresp !== expected_resp || rdata !== expected)
            $fatal(1, "Read addr=%h expected=%h got=%h resp=%h",address,expected,rdata,rresp);
        repeat (stall) begin
            @(posedge clk); #0.1;
            if (!rvalid || rresp !== expected_resp || rdata !== expected || arready)
                $fatal(1, "Read response changed while stalled");
        end
        @(negedge clk); rready = 1;
        @(posedge clk);
        @(negedge clk); rready = 0;
    endtask
    task automatic check_pwm(input integer period_cycles, input integer high_cycles);
        realtime rise_time, fall_time, next_rise;
        @(posedge fan_pwm); rise_time = $realtime;
        @(negedge fan_pwm); fall_time = $realtime;
        @(posedge fan_pwm); next_rise = $realtime;
        if (abs_real((fall_time-rise_time)/CLOCK_NS-high_cycles) > 0.01 ||
            abs_real((next_rise-rise_time)/CLOCK_NS-period_cycles) > 0.01)
            $fatal(1,"PWM timing: high=%f period=%f",(fall_time-rise_time)/CLOCK_NS,(next_rise-rise_time)/CLOCK_NS);
    endtask
    task automatic check_constant_pwm(input bit level, input integer cycles);
        repeat(cycles) begin
            @(posedge clk); #0.1;
            if (fan_pwm !== level) $fatal(1,"PWM expected constant %b",level);
        end
    endtask

    realtime last_frame_end = 0;
    task automatic check_led_frame(input [23:0] expected_rgb);
        realtime rise_time, fall_time, next_rise;
        realtime high_ns, low_ns;
        reg [23:0] received, expected_grb;
        integer k;
        expected_grb = {expected_rgb[15:8],expected_rgb[23:16],expected_rgb[7:0]};
        @(posedge rgb_data); rise_time = $realtime;
        if (rise_time-last_frame_end < 300000.0-0.1)
            $fatal(1,"WS2812 reset gap shorter than 300 us");
        for(k=0;k<24;k=k+1) begin
            @(negedge rgb_data); fall_time=$realtime;
            high_ns=fall_time-rise_time;
            received[23-k] = high_ns > 500.0;
            if (received[23-k]) begin
                if(high_ns<580.0 || high_ns>1000.0) $fatal(1,"T1H out of spec");
            end else if(high_ns<220.0 || high_ns>380.0) $fatal(1,"T0H out of spec");
            if(k<23) begin
                @(posedge rgb_data); next_rise=$realtime;
                low_ns=next_rise-fall_time;
                if(low_ns<580.0 || low_ns>1000.0) $fatal(1,"Data low time out of spec: %f",low_ns);
                if(abs_real(next_rise-rise_time-1250.0)>CLOCK_NS/2.0+0.1)
                    $fatal(1,"Bit period not 1.25 us to nearest clock");
                rise_time=next_rise;
            end
        end
        // End of nominal last bit, not its falling edge.
        last_frame_end = rise_time + $floor(1250.0/CLOCK_NS+0.5)*CLOCK_NS;
        if(received !== expected_grb)
            $fatal(1,"GRB expected=%h got=%h",expected_grb,received);
        $display("LED_FRAME_OK rgb=%h clock=%0d",expected_rgb,CLK_HZ);
    endtask

    initial begin
        #5000000;
        $fatal(1,"Test timeout");
    end

    integer n;
    reg [31:0] reference_rgb;
    reg [31:0] random_data;
    reg [3:0] random_strobe;
    integer lane;
    initial begin
        apply_reset();
        read_reg(12'h000,0,3,0);
        read_reg(12'h004,CLK_HZ/25000,0,0);
        read_reg(12'h008,0,0,0);
        read_reg(12'h00c,CLK_HZ,4,0);
        // Simultaneous channels, AW first, W first, response backpressure.
        write_reg(0,32'h00123456,4'hf,0,0,4,0);
        write_reg(0,32'h00abcdef,4'h2,0,7,3,0);
        read_reg(0,32'h0012cd56,3,0);
        write_reg(0,32'h00990000,4'h4,9,0,3,0);
        read_reg(0,32'h0099cd56,0,0);
        write_reg(0,32'hffffffff,4'h8,0,0,0,0); // reserved upper byte
        write_reg(0,0,4'h0,0,0,0,0); // WSTRB=0 is a no-op
        read_reg(0,32'h0099cd56,0,0);
        reference_rgb=32'h0099cd56;
        for(n=0;n<32;n=n+1) begin
            random_data=$random;
            random_strobe=$random;
            write_reg(0,random_data,random_strobe,n%4,(n+2)%5,n%3,0);
            for(lane=0;lane<3;lane=lane+1)
                if(random_strobe[lane]) reference_rgb[lane*8+:8]=random_data[lane*8+:8];
            read_reg(0,reference_rgb,n%3,0);
        end
        // Read response remains stable while another channel changes the register.
        fork
            read_reg(0,reference_rgb,40,0);
            begin wait(rvalid); write_reg(0,32'h00887766,15,0,0,1,0); end
        join
        read_reg(0,32'h00887766,0,0);
        // RO, unaligned and unmapped accesses all report SLVERR, no aliases.
        write_reg(12'h00c,0,15,0,0,2,2);
        write_reg(12'h002,0,15,0,0,2,2);
        write_reg(12'h100,0,15,0,0,2,2);
        read_reg(12'h002,0,2,2);
        read_reg(12'h100,0,2,2);
        read_reg(0,32'h00887766,0,0);
        write_reg(4,32'h00000123,15,0,0,0,0);
        write_reg(4,32'h00000200,2,0,0,0,0);
        read_reg(4,32'h00000223,0,0);
        write_reg(8,32'h12345678,15,0,0,0,0);
        write_reg(8,32'h00ab0000,4,0,0,0,0);
        read_reg(8,32'h12ab5678,0,0);

        // Reset flushes an unmatched AW and outstanding R/B responses.
        send_aw(0,0);
        apply_reset();
        write_reg(0,32'h00010203,15,4,0,0,0);
        read_reg(0,32'h00010203,0,0);
        fork send_aw(0,0); send_w(32'hffffff,15,0); join
        wait(bvalid);
        @(negedge clk); araddr=0; arvalid=1;
        @(posedge clk);
        @(negedge clk); arvalid=0;
        wait(rvalid);
        apply_reset();
        read_reg(0,0,0,0);

        // PWM edge measurements, including boundary-only duty updates.
        write_reg(4,40,15,0,0,0,0);
        write_reg(8,20,15,0,0,0,0);
        wait_clocks(CLK_HZ/25000+50);
        check_pwm(40,20);
        fork
            begin
                realtime t;
                @(posedge fan_pwm); t=$realtime;
                @(negedge fan_pwm);
                if(abs_real(($realtime-t)/CLOCK_NS-20)>0.01)
                    $fatal(1,"Mid-period write shortened current pulse");
            end
            begin @(posedge fan_pwm); write_reg(8,5,15,0,0,0,0); end
        join
        check_pwm(40,5);
        write_reg(8,0,15,0,0,0,0);
        wait_clocks(45); check_constant_pwm(0,80);
        write_reg(8,40,15,0,0,0,0);
        wait_clocks(45); check_constant_pwm(1,80);
        write_reg(8,100,15,0,0,0,0);
        wait_clocks(45); check_constant_pwm(1,80);
        write_reg(4,0,15,0,0,0,0); // treated as one, readback still zero
        wait_clocks(45); check_constant_pwm(1,10);
        read_reg(4,0,0,0);
        write_reg(8,0,15,0,0,0,0);
        wait_clocks(5); check_constant_pwm(0,10);
        write_reg(4,1,15,0,0,0,0);
        write_reg(8,1,15,0,0,0,0);
        wait_clocks(5); check_constant_pwm(1,10);
        $display("AXI_PWM_OK clock=%0d",CLK_HZ);

        // Restart timing epoch. A mid-frame RGB write must affect the next frame.
        apply_reset(); last_frame_end=reset_low_time;
        write_reg(0,32'h00123456,15,0,0,0,0);
        fork
            check_led_frame(24'h123456);
            begin
                @(posedge rgb_data); #10000;
                write_reg(0,32'h00abcdef,15,0,0,0,0);
            end
        join
        check_led_frame(24'habcdef);
        write_reg(0,0,15,0,0,0,0);
        check_led_frame(0);
        write_reg(0,32'h00ffffff,15,0,0,0,0);
        check_led_frame(24'hffffff);
        // Reset during a high pulse must suppress data and force a new reset gap.
        @(posedge rgb_data);
        apply_reset(); last_frame_end=reset_low_time;
        check_led_frame(0);
        $display("ALL_TESTS_PASSED CLK_HZ=%0d",CLK_HZ);
        $finish;
    end
endmodule
