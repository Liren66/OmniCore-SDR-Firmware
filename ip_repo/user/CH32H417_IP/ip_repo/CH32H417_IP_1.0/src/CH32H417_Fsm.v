`timescale 1ns / 1ps
`default_nettype none

// CH32H417 与 AD9361 成帧流桥接（CH32H417_IP 1.3，只有成帧模式）。
// RX = ADC -> FPGA -> CH32H417 -> PC；TX = 反方向。
// 四个 FIFO Generator 13.2 IP（名称必须一致）：
//   ch417_std_adc_single : 写24位/深8192， 读96位/深2048，读计数11位。
//   ch417_std_adc_dual   : 写48位/深8192， 读96位/深4096，读计数12位。
//   ch417_std_dac_single : 写96位/深2048， 读24位/深8192， 写11/读13位计数。
//   ch417_std_dac_dual   : 写96位/深4096， 读48位/深8192， 写12/读13位计数。
// 共同设置：Native、Independent Clocks Block RAM、Standard FIFO，读延迟1拍，
// 异步复位；本文件在每侧保持32拍复位，并等待对端准备好；三个流时钟必须运行。
// 子模块：oc_afifo（块头/事件小FIFO）、oc_hold_xfer（命令跨时钟域）。
//
// ctrl_clk 接 AXI 寄存器时钟，ctrl_resetn 接低有效 AXI 全局复位。
// CTRL0：0 RX_EN，1 TX_EN，2 RX_DUAL，3 RX_CH1，4 TX_DUAL，5 TX_CH1，6 TX_RUN，
//   7 IQ8（仅 RX_DUAL=TX_DUAL=1；改变时 RX_EN/TX_EN/TX_RUN 必须全为0），
//   8 保留（1.2 的 FRAMED，现恒为成帧，写入被忽略），
//   9 RX_TIMED（RX_EN 之后等待 RX 命令；只在 RX 关闭时改变）。
// 切换模式/通道：先关闭该方向，等待就绪位清零，再改模式，最后重新使能。
// CTRL1.bit0：每次写0、再写1，发起一次清错误；等 CLEAR_BUSY=0 后可写回0。
// STATUS0：0..7 错误（0 和 3 恒为0：溢出/欠流改由事件FIFO报告），8 IRQ，
// 9 RDNE，10 WRNF，11 TX_RUNNING，12/13 收发事务，14 CFG_BUSY，15 CLEAR_BUSY，
// 16/17 USB侧RX/TX就绪，18/19 ADC/DAC就绪，20..23 实际模式/通道，
// 24..26 实际RX_EN/TX_EN/TX_RUN，27 CFG_ERROR，28 实际IQ8，29 IQ8能力，
// 30 时间能力，31 成帧（恒为1）。
// STATUS1：[12:0] RX组数(含预读)，[28:16] TX主FIFO组数，31 实际RX_TIMED。
//
// 总线时序：OE比CS/RD提前1拍；RD低4101拍，只取第2..4097拍(从0计数)。
// WR低4096拍，第0..4095拍全部有效；CS多保持的1拍不取数据。
//
// 每个16KiB UHSIF块 = 16字节头 + 16368字节载荷。16368可被3/4/6/8/12整除：
//   12bit单通道5456样点，12bit双通道2728帧，IQ8双通道4092帧；每块整数个
//   96位FIFO组（12bit 1364组，IQ8 2046组），块之间没有相位拼接。
// 头为4个小端u32（总线字bit[7:0]是第一个字节）：
//   h0 = 0x4F43_0000 | flags<<8 | check；h1 = seq<<16 | valid；h2/h3 = 时间低/高。
//   check = h0[31:8]与h1..h3所有字节的XOR。valid=本块有效帧数，其余为补零。
//   RX flags：0 TIME_VALID，1 SOB，2 GAP（本块前丢样），3 EOB，4 LATE，5 PAD。
//   TX flags：0 HAS_TIME，1 SOB，3 EOB；valid=0的块被静默丢弃。
// 时间单位=样点（time_now，来自oc_timekeeper），h2/h3是本块第一个样点的时间。
// RX溢出时当前块补零结束并置下一块GAP；TX按头中时间在DAC侧精确起播；
// 迟到/欠流/突发结束等写入事件FIFO（带时间）。dac_clk必须与adc_clk同一时钟。
module CH32H417_Fsm (
    input  wire        ctrl_clk,
    input  wire        ctrl_resetn,
    output wire        Interrupt_flag,
    input  wire        CH32H417_CLK,
    input  wire        CH32H417_CS_n,
    input  wire        CH32H417_WR_n,
    input  wire        CH32H417_RD_n,
    input  wire        CH32H417_OE_n,
    input  wire        CH32H417_EOP_n, // 保留接口；本版只支持完整16KiB块
    input  wire [1:0]  CH32H417_SEL,
    inout  wire [31:0] CH32H417_DATA,
    output wire        CH32H417_RDNE,
    output wire        CH32H417_WRNF,
    input  wire        adc_clk,
    input  wire [15:0] adc_data_i0, adc_data_q0, adc_data_i1, adc_data_q1,
    input  wire        adc_valid_0, adc_valid_1, adc_enable_0, adc_enable_1,
    input  wire        dac_clk,
    input  wire [15:0] dac_data_i0, dac_data_q0, dac_data_i1, dac_data_q1,
    input  wire        dac_valid_0, dac_valid_1, dac_enable_0, dac_enable_1,
    output wire [15:0] dac_data_i0_out, dac_data_q0_out,
    output wire [15:0] dac_data_i1_out, dac_data_q1_out,
    input  wire [31:0] CH32H417_CTRL_REG0, CH32H417_CTRL_REG1,
    output wire [31:0] CH32H417_STATUS_REG0, CH32H417_STATUS_REG1,
    // Time base in the radio (adc_clk) domain.
    input  wire [63:0] time_now,
    input  wire        time_valid,
    // RX stream command (ctrl_clk): {stop, now, nsamp[31:0], time[63:0]}.
    input  wire        rx_cmd_start,
    input  wire [97:0] rx_cmd_word,
    output wire        rx_cmd_busy,
    // Event FIFO, first-word-fall-through in ctrl_clk:
    // [95:88] code, [87] lost-before, [79:64] block seq, [63:0] time.
    input  wire        evt_pop,
    output wire        evt_empty,
    output wire [95:0] evt_word,
    output wire [15:0] frame_status    // ctrl_clk, independently synchronized bits
);
    // Samples per 16368-byte payload and 96-bit groups per block.
    localparam [12:0] FR_S_SINGLE = 13'd5456;
    localparam [12:0] FR_S_DUAL   = 13'd2728;
    localparam [12:0] FR_S_IQ8    = 13'd4092;
    localparam [12:0] FR_G_12     = 13'd1364;
    localparam [12:0] FR_G_IQ8    = 13'd2046;
    localparam [7:0] EV_TX_ACK = 8'h01, EV_TX_UNDERFLOW = 8'h02, EV_TX_LATE = 8'h03,
                     EV_TX_SEQ = 8'h04, EV_TX_HEADER = 8'h05,
                     EV_RX_OVERFLOW = 8'h10, EV_RX_LATE = 8'h11, EV_RX_CMD_IGNORED = 8'h12;
    function [7:0] hdr_check;
        input [23:0] h0hi;
        input [95:0] rest;
        integer k;
        begin
            hdr_check = h0hi[23:16] ^ h0hi[15:8] ^ h0hi[7:0];
            for (k = 0; k < 12; k = k + 1) hdr_check = hdr_check ^ rest[k*8 +: 8];
        end
    endfunction
    // TX写入准入：主FIFO必须还能放下一整块载荷，16组余量覆盖计数跨时钟延迟。
    localparam TX_MAX_USED_SINGLE = 2048 - 1364 - 16; // 668
    localparam TX_MAX_USED_DUAL   = 4096 - 1364 - 16; // 2716
    localparam TX_MAX_USED_IQ8    = 4096 - 2046 - 16; // 2034
    function [31:0] swap32;
        input [31:0] x;
        begin
            swap32 = {x[7:0],x[15:8],x[23:16],x[31:24]};
        end
    endfunction

    // ========================================================================
    // 1. 全局复位分别在三个流时钟域中安全释放。
    // ========================================================================
    wire usb_resetn, adc_resetn, dac_resetn;
    // USB全局复位：异步进入复位；本地时钟连续运行32拍后才放行。
    wire usb_global_reset = !ctrl_resetn;
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg [31:0] usb_global_release;
    always @(posedge CH32H417_CLK or posedge usb_global_reset) begin
        if (usb_global_reset)
            usb_global_release <= 0;
        else
            usb_global_release <= {usb_global_release[30:0], 1'b1};
    end
    assign usb_resetn = usb_global_release[31];
    // ADC全局复位：异步进入复位；本地时钟连续运行32拍后才放行。
    wire adc_global_reset = !ctrl_resetn;
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg [31:0] adc_global_release;
    always @(posedge adc_clk or posedge adc_global_reset) begin
        if (adc_global_reset)
            adc_global_release <= 0;
        else
            adc_global_release <= {adc_global_release[30:0], 1'b1};
    end
    assign adc_resetn = adc_global_release[31];
    // DAC全局复位：异步进入复位；本地时钟连续运行32拍后才放行。
    wire dac_global_reset = !ctrl_resetn;
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg [31:0] dac_global_release;
    always @(posedge dac_clk or posedge dac_global_reset) begin
        if (dac_global_reset)
            dac_global_release <= 0;
        else
            dac_global_release <= {dac_global_release[30:0], 1'b1};
    end
    assign dac_resetn = dac_global_release[31];

    // ========================================================================
    // 2. 配置通过“保持数据 + 请求/应答”送到三个域，不能裸跨多位总线。
    // ========================================================================
    reg [9:0] cfg_held;                 // 当前已接受的配置，含IQ8/FRAMED/RX_TIMED
    reg [31:0] cfg_seen;                // 上次已处理的软件寄存器值
    reg cfg_req, config_error;
    reg [9:0] cfg_u, cfg_a, cfg_d;       // 三个时钟域实际使用的配置
    (* MARK_DEBUG = "TRUE" *) wire iq8_active_u = cfg_u[7];
    reg ack_u, ack_a, ack_d;
    wire [2:0] cfg_ack;
    wire clear_launch;
    // USB配置接收：先同步请求，再捕获源端保持不变的8位配置。
    // 应答回去之前，ctrl_clk 域不会修改 cfg_held。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg cfg_usb_req_meta, cfg_usb_req_sync;
    always @(posedge CH32H417_CLK or negedge usb_resetn) begin
        if (!usb_resetn) begin
            cfg_usb_req_meta <= 0;
            cfg_usb_req_sync <= 0;
            ack_u <= 0;
            cfg_u <= 0;
        end else begin
            cfg_usb_req_meta <= cfg_req;
            cfg_usb_req_sync <= cfg_usb_req_meta;
            if (cfg_usb_req_sync != ack_u) begin
                cfg_u <= cfg_held;
                ack_u <= cfg_usb_req_sync;
            end
        end
    end
    // ADC配置接收：先同步请求，再捕获源端保持不变的8位配置。
    // 应答回去之前，ctrl_clk 域不会修改 cfg_held。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg cfg_adc_req_meta, cfg_adc_req_sync;
    always @(posedge adc_clk or negedge adc_resetn) begin
        if (!adc_resetn) begin
            cfg_adc_req_meta <= 0;
            cfg_adc_req_sync <= 0;
            ack_a <= 0;
            cfg_a <= 0;
        end else begin
            cfg_adc_req_meta <= cfg_req;
            cfg_adc_req_sync <= cfg_adc_req_meta;
            if (cfg_adc_req_sync != ack_a) begin
                cfg_a <= cfg_held;
                ack_a <= cfg_adc_req_sync;
            end
        end
    end
    // DAC配置接收：先同步请求，再捕获源端保持不变的8位配置。
    // 应答回去之前，ctrl_clk 域不会修改 cfg_held。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg cfg_dac_req_meta, cfg_dac_req_sync;
    always @(posedge dac_clk or negedge dac_resetn) begin
        if (!dac_resetn) begin
            cfg_dac_req_meta <= 0;
            cfg_dac_req_sync <= 0;
            ack_d <= 0;
            cfg_d <= 0;
        end else begin
            cfg_dac_req_meta <= cfg_req;
            cfg_dac_req_sync <= cfg_dac_req_meta;
            if (cfg_dac_req_sync != ack_d) begin
                cfg_d <= cfg_held;
                ack_d <= cfg_dac_req_sync;
            end
        end
    end
    // 配置应答回传：ctrl_clk 域的两拍同步；只同步独立的状态位。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg [2:0] cfg_ack_meta, cfg_ack_sync;
    always @(posedge ctrl_clk or negedge ctrl_resetn) begin
        if (!ctrl_resetn) begin
            cfg_ack_meta <= 0;
            cfg_ack_sync <= 0;
        end else begin
            cfg_ack_meta <= {ack_d,ack_a,ack_u};
            cfg_ack_sync <= cfg_ack_meta;
        end
    end
    assign cfg_ack = cfg_ack_sync;
    wire cfg_idle = (cfg_ack == {3{cfg_req}});
    wire cfg_changed = (cfg_seen != CH32H417_CTRL_REG0);
    wire cfg_busy = !cfg_idle || cfg_changed;
    wire config_bad = (|CH32H417_CTRL_REG0[31:10]) ||
        (CH32H417_CTRL_REG0[7] &&
         (!CH32H417_CTRL_REG0[2] || !CH32H417_CTRL_REG0[4])) ||
        ((cfg_held[9] != CH32H417_CTRL_REG0[9]) &&
         (cfg_held[0] || CH32H417_CTRL_REG0[0])) ||
        ((cfg_held[7] != CH32H417_CTRL_REG0[7]) &&
         (cfg_held[0] || cfg_held[1] || cfg_held[6] ||
          CH32H417_CTRL_REG0[0] || CH32H417_CTRL_REG0[1] || CH32H417_CTRL_REG0[6])) ||
        ((cfg_held[0] || CH32H417_CTRL_REG0[0]) &&
         (cfg_held[3:2] != CH32H417_CTRL_REG0[3:2])) ||
        ((cfg_held[1] || CH32H417_CTRL_REG0[1]) &&
         (cfg_held[5:4] != CH32H417_CTRL_REG0[5:4]));
    always @(posedge ctrl_clk or negedge ctrl_resetn) begin
        if (!ctrl_resetn) begin
            cfg_held <= 0;
            cfg_seen <= 0;
            cfg_req <= 0;
            config_error <= 0;
        end else begin
            if (clear_launch) config_error <= 0;
            if (cfg_idle && cfg_changed) begin
                cfg_seen <= CH32H417_CTRL_REG0;
                if (config_bad) config_error <= 1; // 新错误优先于清除
                else begin
                    cfg_held <= CH32H417_CTRL_REG0[9:0];
                    cfg_req <= !cfg_req;
                end
            end
        end
    end

    // ========================================================================
    // 3. 关闭一个方向时，清空该方向两套FIFO和小缓存；错误锁存不清。
    // ========================================================================
    // 使用同一个稳定的EN发起两侧复位，再在各自时钟域延迟释放。
    wire rx_a_guard, rx_u_guard, tx_u_guard, tx_d_guard;
    // RX FIFO写侧复位：异步进入复位；本地时钟连续运行32拍后才放行。
    wire rx_adc_reset = !ctrl_resetn || !cfg_held[0];
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg [31:0] rx_adc_release;
    always @(posedge adc_clk or posedge rx_adc_reset) begin
        if (rx_adc_reset)
            rx_adc_release <= 0;
        else
            rx_adc_release <= {rx_adc_release[30:0], 1'b1};
    end
    assign rx_a_guard = rx_adc_release[31];
    // RX FIFO读侧复位：异步进入复位；本地时钟连续运行32拍后才放行。
    wire rx_usb_reset = !ctrl_resetn || !cfg_held[0];
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg [31:0] rx_usb_release;
    always @(posedge CH32H417_CLK or posedge rx_usb_reset) begin
        if (rx_usb_reset)
            rx_usb_release <= 0;
        else
            rx_usb_release <= {rx_usb_release[30:0], 1'b1};
    end
    assign rx_u_guard = rx_usb_release[31];
    // TX FIFO写侧复位：异步进入复位；本地时钟连续运行32拍后才放行。
    wire tx_usb_reset = !ctrl_resetn || !cfg_held[1];
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg [31:0] tx_usb_release;
    always @(posedge CH32H417_CLK or posedge tx_usb_reset) begin
        if (tx_usb_reset)
            tx_usb_release <= 0;
        else
            tx_usb_release <= {tx_usb_release[30:0], 1'b1};
    end
    assign tx_u_guard = tx_usb_release[31];
    // TX FIFO读侧复位：异步进入复位；本地时钟连续运行32拍后才放行。
    wire tx_dac_reset = !ctrl_resetn || !cfg_held[1];
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg [31:0] tx_dac_release;
    always @(posedge dac_clk or posedge tx_dac_reset) begin
        if (tx_dac_reset)
            tx_dac_release <= 0;
        else
            tx_dac_release <= {tx_dac_release[30:0], 1'b1};
    end
    assign tx_d_guard = tx_dac_release[31];
    wire rx_a_seen_u, rx_u_seen_a, tx_d_seen_u, tx_u_seen_d;
    // ADC侧RX复位已释放：CH32H417_CLK 域的两拍同步；只同步独立的状态位。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg rx_a_seen_u_meta, rx_a_seen_u_sync;
    always @(posedge CH32H417_CLK or negedge usb_resetn) begin
        if (!usb_resetn) begin
            rx_a_seen_u_meta <= 0;
            rx_a_seen_u_sync <= 0;
        end else begin
            rx_a_seen_u_meta <= rx_a_guard;
            rx_a_seen_u_sync <= rx_a_seen_u_meta;
        end
    end
    assign rx_a_seen_u = rx_a_seen_u_sync;
    // USB侧RX复位已释放：adc_clk 域的两拍同步；只同步独立的状态位。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg rx_u_seen_a_meta, rx_u_seen_a_sync;
    always @(posedge adc_clk or negedge adc_resetn) begin
        if (!adc_resetn) begin
            rx_u_seen_a_meta <= 0;
            rx_u_seen_a_sync <= 0;
        end else begin
            rx_u_seen_a_meta <= rx_u_guard;
            rx_u_seen_a_sync <= rx_u_seen_a_meta;
        end
    end
    assign rx_u_seen_a = rx_u_seen_a_sync;
    // DAC侧TX复位已释放：CH32H417_CLK 域的两拍同步；只同步独立的状态位。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg tx_d_seen_u_meta, tx_d_seen_u_sync;
    always @(posedge CH32H417_CLK or negedge usb_resetn) begin
        if (!usb_resetn) begin
            tx_d_seen_u_meta <= 0;
            tx_d_seen_u_sync <= 0;
        end else begin
            tx_d_seen_u_meta <= tx_d_guard;
            tx_d_seen_u_sync <= tx_d_seen_u_meta;
        end
    end
    assign tx_d_seen_u = tx_d_seen_u_sync;
    // USB侧TX复位已释放：dac_clk 域的两拍同步；只同步独立的状态位。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg tx_u_seen_d_meta, tx_u_seen_d_sync;
    always @(posedge dac_clk or negedge dac_resetn) begin
        if (!dac_resetn) begin
            tx_u_seen_d_meta <= 0;
            tx_u_seen_d_sync <= 0;
        end else begin
            tx_u_seen_d_meta <= tx_u_guard;
            tx_u_seen_d_sync <= tx_u_seen_d_meta;
        end
    end
    assign tx_u_seen_d = tx_u_seen_d_sync;
    wire rx_adc_ready = rx_a_guard && rx_u_seen_a && cfg_a[0];
    wire rx_usb_ready = rx_u_guard && rx_a_seen_u && cfg_u[0];
    wire tx_usb_ready = tx_u_guard && tx_d_seen_u && cfg_u[1];
    wire tx_dac_ready = tx_d_guard && tx_u_seen_d && cfg_d[1];

    // ========================================================================
    // 4. ADC入队：保留旧工程12位I/Q格式。单通道选择0或1；双通道同时入队。
    // ========================================================================
    wire [23:0] adc_single = cfg_a[3] ?
        {adc_data_i1[11:0],adc_data_q1[11:0]} :
        {adc_data_i0[11:0],adc_data_q0[11:0]};
    wire adc_need = cfg_a[2] ?
        (adc_enable_0 && adc_enable_1 && adc_valid_0 && adc_valid_1) :
        (cfg_a[3] ? (adc_enable_1 && adc_valid_1) : (adc_enable_0 && adc_valid_0));
    wire adc_pair_bad = rx_adc_ready && cfg_a[2] && (adc_valid_0 || adc_valid_1) &&
        (!adc_enable_0 || !adc_enable_1 || (adc_valid_0 != adc_valid_1));
    wire [95:0] rx_s_dout, rx_d_dout;
    wire rx_s_empty, rx_d_empty, rx_s_valid, rx_d_valid;
    wire [10:0] rx_s_count;
    wire [11:0] rx_d_count;
    // Sample, its time and its mode-qualified request travel together.
    reg [23:0] adc_single_pipe;
    reg [47:0] adc_dual_pipe;
    reg [63:0] adc_time_pipe;
    reg adc_tvalid_pipe;
    reg adc_valid_pipe;
    always @(posedge adc_clk) begin
        adc_single_pipe <= adc_single;
        adc_dual_pipe <= {adc_data_i0[11:0],adc_data_q0[11:0],
                          adc_data_i1[11:0],adc_data_q1[11:0]};
        adc_time_pipe <= time_now;      // time of the sample in this stage
        adc_tvalid_pipe <= time_valid;
    end
    always @(posedge adc_clk or negedge rx_a_guard) begin
        if (!rx_a_guard) begin
            adc_valid_pipe <= 1'b0;
        end else begin
            adc_valid_pipe <= rx_adc_ready && adc_need;
        end
    end

    // ------------------------------------------------------------------------
    // 4b. 成帧RX写入（adc_clk）。两级附加流水用于把样点时间与命令时间比较。
    // ------------------------------------------------------------------------
    wire [12:0] fr_a_samples = !cfg_a[2] ? FR_S_SINGLE : (cfg_a[7] ? FR_S_IQ8 : FR_S_DUAL);
    wire rxc_pulse;
    wire [97:0] rxc_word;
    oc_hold_xfer #(.W(98)) u_rx_cmd (
        .sclk(ctrl_clk), .srstn(ctrl_resetn), .start(rx_cmd_start),
        .sdata(rx_cmd_word), .busy(rx_cmd_busy),
        .dclk(adc_clk), .drstn(adc_resetn), .dpulse(rxc_pulse), .ddata(rxc_word)
    );
    reg rxc_pend, rxc_stop, rxc_now;
    reg [31:0] rxc_n;
    reg [63:0] rxc_t;
    wire rxc_take;
    always @(posedge adc_clk or negedge adc_resetn) begin
        if (!adc_resetn) begin
            rxc_pend <= 0; rxc_stop <= 0; rxc_now <= 0; rxc_n <= 0; rxc_t <= 0;
        end else if (rxc_pulse) begin
            rxc_pend <= 1'b1;
            {rxc_stop, rxc_now, rxc_n, rxc_t} <= rxc_word;
        end else if (rxc_take) begin
            rxc_pend <= 1'b0;
        end
    end

    reg [63:0] w_T;
    reg [23:0] s2_single, s3_single;
    reg [47:0] s2_dual, s3_dual;
    reg [63:0] s2_time, s3_time;
    reg s2_tv, s3_tv, s2_v, s3_v;
    reg s2_hi_gt, s2_hi_eq, s2_lo_ge, s2_lo_gt, s3_ge, s3_gt;
    always @(posedge adc_clk) begin
        s2_single <= adc_single_pipe;  s3_single <= s2_single;
        s2_dual <= adc_dual_pipe;      s3_dual <= s2_dual;
        s2_time <= adc_time_pipe;      s3_time <= s2_time;
        s2_tv <= adc_tvalid_pipe;      s3_tv <= s2_tv;
        s2_hi_gt <= adc_time_pipe[63:32] >  w_T[63:32];
        s2_hi_eq <= adc_time_pipe[63:32] == w_T[63:32];
        s2_lo_ge <= adc_time_pipe[31:0]  >= w_T[31:0];
        s2_lo_gt <= adc_time_pipe[31:0]  >  w_T[31:0];
        s3_ge <= s2_hi_gt || (s2_hi_eq && s2_lo_ge);
        s3_gt <= s2_hi_gt || (s2_hi_eq && s2_lo_gt);
    end
    always @(posedge adc_clk or negedge rx_a_guard) begin
        if (!rx_a_guard) begin
            s2_v <= 1'b0; s3_v <= 1'b0;
        end else begin
            s2_v <= adc_valid_pipe;
            s3_v <= s2_v;
        end
    end

    localparam [1:0] W_IDLE = 2'd0, W_WAIT = 2'd1, W_RUN = 2'd2, W_PAD = 2'd3;
    reg [1:0] wst;
    reg [12:0] wpos, wvalid;
    reg [63:0] wtime;
    reg [15:0] wseq;
    reg w_sob_next, w_gap_next;
    reg f_tv, f_sob, f_gap, f_late;
    reg w_finite, w_eob, w_auto_done;
    reg [31:0] w_left;
    reg wpos_z, wpos_l, w_left1;  // registered wpos==0, wpos==last, w_left==1
    reg [1:0] w_settle;
    reg pa_v, pb_v;
    reg [63:0] pa_time;
    reg [15:0] pa_seq;
    reg [12:0] pa_cnt;
    reg [7:0] pa_flags;
    reg [127:0] pb_hdr;
    reg ea_v;
    reg [7:0] ea_code;
    reg [15:0] ea_seq;
    reg [63:0] ea_time;
    reg rtag_lost;
    wire rtag_full;
    wire rx_s_full, rx_d_full;
    wire fr_full = cfg_a[2] ? rx_d_full : rx_s_full;
    // Two-entry skid register: framed write decisions only see its occupancy
    // (registered); the BRAM write enable is just skid_valid && !full.
    reg [47:0] sk_d0, sk_d1;
    reg [1:0] sk_n;
    reg sk_nz;
    wire sk_full = (sk_n == 2'd2);
    wire sk_pop = sk_nz && !fr_full;
    wire fr_on = rx_adc_ready;
    // After an overflow the writer waits until the host has drained the FIFO
    // to less than one block (rx_low from the USB domain) before it pads the
    // broken block and resumes. Without this hysteresis every block the host
    // frees would be consumed by padding and the stream would never recover.
    wire rx_low_u;
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg rx_low_a1, rx_low_a2;
    always @(posedge adc_clk or negedge rx_a_guard) begin
        if (!rx_a_guard) begin
            rx_low_a1 <= 1'b0; rx_low_a2 <= 1'b0;
        end else begin
            rx_low_a1 <= rx_low_u; rx_low_a2 <= rx_low_a1;
        end
    end
    reg w_ovf_pad;
    wire fr_stop = rxc_pend && rxc_stop;
    wire fr_start_cmd = rxc_pend && !rxc_stop;
    wire fr_last = w_finite && w_left1;
    wire fr_wait_go = (wst == W_WAIT) && (w_settle == 2'd0) && s3_v && s3_ge && !fr_stop;
    wire fr_run_sample = fr_on && s3_v && (((wst == W_RUN) && !fr_stop) || fr_wait_go);
    wire fr_blocked = sk_full || (wpos_z && w_gap_next && !rx_low_a2);
    wire fr_pad_go = (wst == W_PAD) && !sk_full && (!w_ovf_pad || rx_low_a2);
    wire fr_wr = fr_on && ((fr_run_sample && !fr_blocked) || fr_pad_go);
    wire [47:0] fr_din = (wst == W_PAD) ? 48'd0 : (cfg_a[2] ? s3_dual : {24'd0, s3_single});
    always @(posedge adc_clk or negedge rx_a_guard) begin
        if (!rx_a_guard) begin
            sk_n <= 0; sk_nz <= 1'b0; sk_d0 <= 0; sk_d1 <= 0;
        end else begin
            sk_nz <= (sk_n + {1'b0, fr_wr} - {1'b0, sk_pop}) != 2'd0;
            case ({fr_wr, sk_pop})
            2'b10: begin
                if (sk_n == 0) sk_d0 <= fr_din; else sk_d1 <= fr_din;
                sk_n <= sk_n + 1'b1;
            end
            2'b01: begin
                sk_d0 <= sk_d1;
                sk_n <= sk_n - 1'b1;
            end
            2'b11: begin
                if (sk_n == 2'd1) sk_d0 <= fr_din;
                else begin sk_d0 <= sk_d1; sk_d1 <= fr_din; end
            end
            default: ;
            endcase
        end
    end
    assign rxc_take = fr_on && rxc_pend &&
        ((wst == W_IDLE && cfg_a[9]) || wst == W_WAIT || wst == W_PAD ||
         (wst == W_RUN && (fr_stop || fr_start_cmd)));

    always @(posedge adc_clk or negedge rx_a_guard) begin
        if (!rx_a_guard) begin
            wst <= W_IDLE; wpos <= 0; wpos_z <= 1'b1; wpos_l <= 1'b0; wvalid <= 0; wtime <= 0; wseq <= 0;
            w_sob_next <= 1'b1; w_gap_next <= 0;
            f_tv <= 0; f_sob <= 0; f_gap <= 0; f_late <= 0;
            w_finite <= 0; w_eob <= 0; w_auto_done <= 0; w_left <= 0; w_left1 <= 1'b0; w_settle <= 0;
            w_T <= 64'hFFFF_FFFF_FFFF_FFFF;
            pa_v <= 0; pa_time <= 0; pa_seq <= 0; pa_cnt <= 0; pa_flags <= 0;
            pb_v <= 0; pb_hdr <= 0;
            ea_v <= 0; ea_code <= 0; ea_seq <= 0; ea_time <= 0;
            rtag_lost <= 0; w_ovf_pad <= 0;
        end else begin
            pa_v <= 1'b0;
            ea_v <= 1'b0;
            ea_seq <= wseq;          // event time/seq: sample in stage 3 this cycle
            ea_time <= s3_time;
            if (w_settle != 0) w_settle <= w_settle - 1'b1;
            // Header stage: the tag word is the complete 16-byte block header.
            pb_v <= pa_v;
            if (pa_v)
                pb_hdr <= {pa_time, pa_seq, 3'b000, pa_cnt, 16'h4F43, pa_flags,
                           hdr_check({16'h4F43, pa_flags}, {pa_time, pa_seq, 3'b000, pa_cnt})};
            if (pb_v && rtag_full) rtag_lost <= 1'b1;

            if (fr_on) begin
                // ---------------- commands and state transitions ----------------
                case (wst)
                W_IDLE: begin
                    if (!cfg_a[9]) begin
                        if (!w_auto_done) begin
                            wst <= W_RUN; w_auto_done <= 1'b1;
                            w_sob_next <= 1'b1; w_finite <= 1'b0;
                        end
                    end else if (rxc_pend && !rxc_stop) begin
                        w_sob_next <= 1'b1; w_gap_next <= 1'b0;
                        w_finite <= (rxc_n != 0); w_left <= rxc_n; w_left1 <= (rxc_n == 32'd1);
                        if (rxc_now) begin
                            wst <= W_RUN;
                        end else begin
                            wst <= W_WAIT; w_T <= rxc_t; w_settle <= 2'd3;
                        end
                    end
                end
                W_WAIT: begin
                    if (fr_stop) wst <= W_IDLE;
                    else if (rxc_pend) begin
                        ea_v <= 1'b1; ea_code <= EV_RX_CMD_IGNORED;
                    end
                end
                W_RUN: begin
                    if (fr_stop) begin
                        wvalid <= wpos; w_eob <= 1'b1; wst <= W_PAD;
                        if (wpos_z) wtime <= time_now;
                    end else if (fr_start_cmd) begin
                        ea_v <= 1'b1; ea_code <= EV_RX_CMD_IGNORED;
                    end
                end
                default: begin // W_PAD
                    if (fr_stop) w_eob <= 1'b1;
                    else if (rxc_pend) begin
                        ea_v <= 1'b1; ea_code <= EV_RX_CMD_IGNORED;
                    end
                    // Samples arriving while padding are lost; time still runs.
                    if (s3_v && w_finite && !w_eob) begin
                        w_left <= w_left - 1'b1; w_left1 <= (w_left == 32'd2);
                        if (fr_last) w_eob <= 1'b1;
                    end
                    if (fr_pad_go) begin
                        if (wpos_z) begin
                            f_tv <= time_valid; f_sob <= w_sob_next; f_gap <= w_gap_next; f_late <= 1'b0;
                            w_sob_next <= 1'b0; w_gap_next <= 1'b0;
                        end
                        if (wpos_l) begin
                            pa_v <= 1'b1; pa_time <= wtime; pa_seq <= wseq; pa_cnt <= wvalid;
                            pa_flags <= {2'b00, 1'b1, f_late, w_eob || fr_stop, f_gap, f_sob, f_tv};
                            wpos <= 0; wpos_z <= 1'b1; wpos_l <= 1'b0; wseq <= wseq + 1'b1; w_ovf_pad <= 1'b0;
                            if (w_eob || fr_stop) begin
                                wst <= W_IDLE; w_eob <= 1'b0;
                            end else begin
                                wst <= W_RUN;
                            end
                        end else begin
                            wpos <= wpos + 1'b1; wpos_z <= 1'b0; wpos_l <= (wpos == fr_a_samples - 2'd2);
                        end
                    end
                end
                endcase

                // ---------------- one sample in RUN (or the WAIT start) ----------------
                if (fr_run_sample) begin
                    if (w_finite) w_left <= w_left - 1'b1; w_left1 <= (w_left == 32'd2);
                    if (fr_wait_go && s3_gt) begin
                        ea_v <= 1'b1; ea_code <= EV_RX_LATE;
                    end
                    if (!fr_blocked) begin
                        if (wpos_z) begin
                            wtime <= s3_time; f_tv <= s3_tv; f_sob <= w_sob_next;
                            f_gap <= w_gap_next; f_late <= fr_wait_go && s3_gt;
                            w_sob_next <= 1'b0; w_gap_next <= 1'b0;
                        end
                        if (wpos_l) begin
                            pa_v <= 1'b1; pa_time <= wtime; pa_seq <= wseq; pa_cnt <= fr_a_samples;
                            pa_flags <= {2'b00, 1'b0, f_late, fr_last, f_gap, f_sob, f_tv};
                            wpos <= 0; wpos_z <= 1'b1; wpos_l <= 1'b0; wseq <= wseq + 1'b1;
                            wst <= fr_last ? W_IDLE : W_RUN;
                        end else begin
                            wpos <= wpos + 1'b1; wpos_z <= 1'b0; wpos_l <= (wpos == fr_a_samples - 2'd2);
                            if (fr_last) begin
                                wvalid <= wpos + 1'b1; w_eob <= 1'b1; wst <= W_PAD;
                            end else begin
                                wst <= W_RUN;
                            end
                        end
                    end else begin
                        // FIFO full: close the current block with zeros and
                        // mark the next block as following a gap.
                        if (!w_gap_next) begin
                            ea_v <= 1'b1; ea_code <= EV_RX_OVERFLOW;
                        end
                        w_gap_next <= 1'b1;
                        if (!wpos_z) begin
                            wvalid <= wpos; wst <= W_PAD; w_ovf_pad <= 1'b1;
                            if (fr_last) w_eob <= 1'b1;
                        end else if (fr_last) begin
                            wvalid <= 0; wtime <= s3_time; w_eob <= 1'b1; wst <= W_PAD;
                        end else begin
                            wst <= W_RUN;
                        end
                    end
                end
            end
        end
    end

    wire rx_fifo_read;
    ch417_std_adc_single rx_single (
        .wr_clk(adc_clk), .wr_rst(!rx_a_guard),
        .rd_clk(CH32H417_CLK), .rd_rst(!rx_u_guard),
        .din(sk_d0[23:0]),
        .wr_en(sk_pop && !cfg_a[2]),
        .rd_en(rx_fifo_read && !cfg_u[2]), .dout(rx_s_dout),
        .full(rx_s_full), .empty(rx_s_empty), .valid(rx_s_valid),
        .rd_data_count(rx_s_count)
    );
    ch417_std_adc_dual rx_dual (
        .wr_clk(adc_clk), .wr_rst(!rx_a_guard),
        .rd_clk(CH32H417_CLK), .rd_rst(!rx_u_guard),
        .din(sk_d0),
        .wr_en(sk_pop && cfg_a[2]),
        .rd_en(rx_fifo_read && cfg_u[2]),
        .dout(rx_d_dout), .full(rx_d_full), .empty(rx_d_empty),
        .valid(rx_d_valid), .rd_data_count(rx_d_count)
    );
    // RX block headers: written after the block's last group, read by USB.
    wire rtag_empty;
    wire [127:0] rtag_rdata;
    wire rtag_ren;
    oc_afifo #(.W(128), .AW(3)) u_rx_tag (
        .wclk(adc_clk), .wrstn(rx_a_guard), .wen(pb_v), .wdata(pb_hdr), .wfull(rtag_full),
        .rclk(CH32H417_CLK), .rrstn(rx_u_guard), .ren(rtag_ren), .rdata(rtag_rdata),
        .rempty(rtag_empty)
    );

    // Standard FIFO读请求后一拍才返回数据，不能直接把dout当作FIFO首字。
    // 先把返回的数据放入4字寄存器缓存，USB从缓存连续取数。
    wire [95:0] rx_dout;
    wire rx_valid, rx_pop;
    reg [2:0] rx_cached, rx_reserved;
    wire rx_fifo_empty = cfg_u[2] ? rx_d_empty : rx_s_empty;
    // RX预读缓存：仅4个寄存器字，直接写在当前模块中。
    // cached = 已返回、可使用的字数；reserved = 已请求但尚未消费的字数。
    // reserved 包含还在FIFO输出流水线中的字，防止暂停时缓存被挤满。
    reg [95:0] rx_cache [0:3];
    reg [1:0] rx_cache_rd_ptr, rx_cache_wr_ptr;
    wire [95:0] rx_return_data = cfg_u[2] ? rx_d_dout : rx_s_dout;
    wire rx_return_valid = cfg_u[2] ? rx_d_valid : rx_s_valid;
    wire rx_cache_push = rx_usb_ready && rx_return_valid;
    wire rx_cache_pop = rx_usb_ready && rx_pop && (rx_cached != 0);

    assign rx_fifo_read = rx_usb_ready && !(rx_fifo_empty) && (rx_reserved < 4);
    assign rx_valid = (rx_cached != 0);
    assign rx_dout = rx_cache[rx_cache_rd_ptr];

    always @(posedge CH32H417_CLK or negedge rx_u_guard) begin
        if (!rx_u_guard) begin
            rx_cached <= 0;
            rx_reserved <= 0;
            rx_cache_rd_ptr <= 0;
            rx_cache_wr_ptr <= 0;
        end else if (!rx_usb_ready) begin
            rx_cached <= 0;
            rx_reserved <= 0;
            rx_cache_rd_ptr <= 0;
            rx_cache_wr_ptr <= 0;
        end else begin
            // 返回数据入队；消费数据出队，两者可以同拍进行。
            if (rx_cache_push) begin
                rx_cache[rx_cache_wr_ptr] <= rx_return_data;
                rx_cache_wr_ptr <= rx_cache_wr_ptr + 1'b1;
            end
            if (rx_cache_pop)
                rx_cache_rd_ptr <= rx_cache_rd_ptr + 1'b1;

            case ({rx_cache_push, rx_cache_pop})
                2'b10: rx_cached <= rx_cached + 1'b1;
                2'b01: rx_cached <= rx_cached - 1'b1;
                default: rx_cached <= rx_cached;
            endcase
            case ({rx_fifo_read, rx_cache_pop})
                2'b10: rx_reserved <= rx_reserved + 1'b1;
                2'b01: rx_reserved <= rx_reserved - 1'b1;
                default: rx_reserved <= rx_reserved;
            endcase
        end
    end
    wire [12:0] rx_count = (cfg_u[2] ? {1'b0,rx_d_count} : {2'b0,rx_s_count}) +
                            {10'd0,rx_reserved};
    wire rx_empty = !rx_valid;

    // ========================================================================
    // 5. USB写入DAC FIFO：先收齐3个32位字，再写1个96位字。
    // ========================================================================
    wire [23:0] tx_s_dout;
    wire [47:0] tx_d_dout;
    wire tx_s_full, tx_d_full, tx_s_empty, tx_d_empty, tx_s_valid, tx_d_valid;
    wire [10:0] tx_s_count;
    wire [11:0] tx_d_count;
    wire [12:0] tx_s_samples;
    wire [12:0] tx_d_samples;
    wire tx_full = cfg_u[4] ? tx_d_full : tx_s_full;
    wire [12:0] tx_count = cfg_u[4] ? {1'b0,tx_d_count} : {2'b0,tx_s_count};
    reg [63:0] tx_partial;
    reg [47:0] tx_iq8_partial;
    // Capture the physical write data and qualifiers in the same input stage.
    // Raw WR_n previously drove the wide tx_partial clock-enable decode; its
    // first beat could advance tx_phase without loading the corresponding word.
    (* IOB = "TRUE" *) reg [31:0] tx_pin_data;
    (* IOB = "TRUE" *) reg tx_pin_cs_n, tx_pin_wr_n, tx_pin_rd_n, tx_pin_oe_n;
    (* IOB = "TRUE" *) reg [1:0] tx_pin_sel;
    always @(posedge CH32H417_CLK) begin
        tx_pin_data <= CH32H417_DATA;
        tx_pin_cs_n <= CH32H417_CS_n;
        tx_pin_wr_n <= CH32H417_WR_n;
        tx_pin_rd_n <= CH32H417_RD_n;
        tx_pin_oe_n <= CH32H417_OE_n;
        tx_pin_sel <= CH32H417_SEL;
    end
    wire [31:0] tx_word = swap32(tx_pin_data);
    // Expand before the unchanged 96-to-48 FIFO. Each byte is signed IQ8;
    // placing it in [11:4] preserves its sign and leaves the low four bits zero.
    wire [47:0] tx_iq8_frame = {tx_word[31:24],4'b0,tx_word[23:16],4'b0,
                               tx_word[15:8],4'b0,tx_word[7:0],4'b0};
    wire [95:0] tx_din = iq8_active_u ? {tx_iq8_partial,tx_iq8_frame} :
                                               {tx_partial,tx_word};
    wire tx_push, tx_pop, tx_fifo_read;
    ch417_std_dac_single tx_single (
        .wr_clk(CH32H417_CLK), .wr_rst(!tx_u_guard),
        .rd_clk(dac_clk), .rd_rst(!tx_d_guard),
        .din(tx_din), .wr_en(tx_push && !cfg_u[4]),
        .rd_en(tx_fifo_read && !cfg_d[4]), .dout(tx_s_dout),
        .full(tx_s_full), .empty(tx_s_empty), .valid(tx_s_valid),
        .wr_data_count(tx_s_count), .rd_data_count(tx_s_samples)
    );
    ch417_std_dac_dual tx_dual (
        .wr_clk(CH32H417_CLK), .wr_rst(!tx_u_guard),
        .rd_clk(dac_clk), .rd_rst(!tx_d_guard),
        .din(tx_din), .wr_en(tx_push && cfg_u[4]),
        .rd_en(tx_fifo_read && cfg_d[4]), .dout(tx_d_dout),
        .full(tx_d_full), .empty(tx_d_empty), .valid(tx_d_valid),
        .wr_data_count(tx_d_count), .rd_data_count(tx_d_samples)
    );
    wire [47:0] dac_sample;
    wire dac_sample_valid;
    reg [2:0] dac_cached, dac_reserved;
    // DAC预读缓存：仅4个寄存器字，直接写在当前模块中。
    // cached = 已返回、可使用的字数；reserved = 已请求但尚未消费的字数。
    // reserved 包含还在FIFO输出流水线中的字，防止暂停时缓存被挤满。
    reg [47:0] dac_cache [0:3];
    reg [1:0] dac_cache_rd_ptr, dac_cache_wr_ptr;
    // Register each BRAM output BEFORE the single/dual selection mux.
    // This breaks the BRAM -> mux -> four-entry cache path at 245.76 MHz.
    // reserved already includes every outstanding read, so the extra cycle
    // does not change cache capacity accounting or sample consumption order.
    reg [23:0] tx_s_dout_pipe;
    reg [47:0] tx_d_dout_pipe;
    reg tx_s_valid_pipe, tx_d_valid_pipe;
    always @(posedge dac_clk) begin
        tx_s_dout_pipe <= tx_s_dout;
        tx_d_dout_pipe <= tx_d_dout;
    end
    always @(posedge dac_clk or negedge tx_d_guard) begin
        if (!tx_d_guard) begin
            tx_s_valid_pipe <= 1'b0;
            tx_d_valid_pipe <= 1'b0;
        end else begin
            tx_s_valid_pipe <= tx_dac_ready && tx_s_valid;
            tx_d_valid_pipe <= tx_dac_ready && tx_d_valid;
        end
    end
    wire [47:0] dac_return_data = cfg_d[4] ? tx_d_dout_pipe : {24'd0,tx_s_dout_pipe};
    wire dac_return_valid = cfg_d[4] ? tx_d_valid_pipe : tx_s_valid_pipe;
    wire dac_cache_push = tx_dac_ready && dac_return_valid;
    wire dac_cache_pop = tx_dac_ready && tx_pop && (dac_cached != 0);

    assign tx_fifo_read = tx_dac_ready &&
        !(cfg_d[4] ? tx_d_empty : tx_s_empty) && (dac_reserved < 4);
    assign dac_sample_valid = (dac_cached != 0);
    assign dac_sample = dac_cache[dac_cache_rd_ptr];

    always @(posedge dac_clk or negedge tx_d_guard) begin
        if (!tx_d_guard) begin
            dac_cached <= 0;
            dac_reserved <= 0;
            dac_cache_rd_ptr <= 0;
            dac_cache_wr_ptr <= 0;
        end else if (!tx_dac_ready) begin
            dac_cached <= 0;
            dac_reserved <= 0;
            dac_cache_rd_ptr <= 0;
            dac_cache_wr_ptr <= 0;
        end else begin
            // 返回数据入队；消费数据出队，两者可以同拍进行。
            if (dac_cache_push) begin
                dac_cache[dac_cache_wr_ptr] <= dac_return_data;
                dac_cache_wr_ptr <= dac_cache_wr_ptr + 1'b1;
            end
            if (dac_cache_pop)
                dac_cache_rd_ptr <= dac_cache_rd_ptr + 1'b1;

            case ({dac_cache_push, dac_cache_pop})
                2'b10: dac_cached <= dac_cached + 1'b1;
                2'b01: dac_cached <= dac_cached - 1'b1;
                default: dac_cached <= dac_cached;
            endcase
            case ({tx_fifo_read, dac_cache_pop})
                2'b10: dac_reserved <= dac_reserved + 1'b1;
                2'b01: dac_reserved <= dac_reserved - 1'b1;
                default: dac_reserved <= dac_reserved;
            endcase
        end
    end
    wire tx_empty = !dac_sample_valid;

    // ========================================================================
    // 6. UHSIF整块计数。OE提前的一拍不计数，只以RD/WR低电平计数。
    // ========================================================================
    // 地址01读ADC，地址00写DAC；选通信号不正确时不消费数据。
    wire read_bus = !CH32H417_CS_n && !CH32H417_RD_n && !CH32H417_OE_n &&
                    CH32H417_WR_n && (CH32H417_SEL == 2'b01);
    wire write_bus = !tx_pin_cs_n && !tx_pin_wr_n && tx_pin_oe_n &&
                     tx_pin_rd_n && (tx_pin_sel == 2'b00);
    reg [12:0] rd_cycle, wr_cycle;
    reg rx_admitted, tx_admitted;
    reg [1:0] rx_phase, tx_phase; // 12位：0/1/2；IQ8：0/1各代表完整双通道帧
    wire [1:0] last_phase = iq8_active_u ? 2'd1 : 2'd2;
    wire tx_space = !tx_full && (iq8_active_u ? (tx_count <= TX_MAX_USED_IQ8) :
        (cfg_u[4] ? (tx_count <= TX_MAX_USED_DUAL) : (tx_count <= TX_MAX_USED_SINGLE)));
    // A read needs one complete block (header tag + all groups);
    // a write also needs room in the TX header FIFO.
    wire [12:0] fr_u_groups = iq8_active_u ? FR_G_IQ8 : FR_G_12;
    wire rx_space_f = !rtag_empty && rx_valid && (rx_count >= fr_u_groups);
    wire rx_space_sel = rx_space_f;
    assign rx_low_u = rx_usb_ready && (rx_count < fr_u_groups);
    wire wtag_full;
    wire tx_space_sel = tx_space && !wtag_full;
    assign CH32H417_RDNE = rx_usb_ready && ((rd_cycle == 0) ? rx_space_sel :
                                             (rx_admitted && rd_cycle < 4098));
    assign CH32H417_WRNF = tx_usb_ready && ((wr_cycle == 0) ? tx_space_sel :
                                             (tx_admitted && wr_cycle < 4096));
    wire tx_window = write_bus && tx_usb_ready && wr_cycle < 4096;
    // Words 0..3 of each transaction are the header, 4..4095 the payload.
    // The source advances one cycle before the output register launches it.
    wire rx_window_f = read_bus && rx_usb_ready && rd_cycle >= 5 && rd_cycle < 4097;
    wire rx_take = rx_window_f && rx_admitted && rx_valid;
    wire tx_take = tx_window && ((wr_cycle == 0) ? tx_space_sel : tx_admitted);
    wire tx_take_pl = tx_take && !(wr_cycle < 4);
    assign rx_pop = rx_take && (rx_phase == last_phase);
    assign tx_push = tx_take_pl && (tx_phase == last_phase) && !tx_full;
    reg [127:0] rx_hdr;
    wire rx_hdr_slot = (rd_cycle >= 1) && (rd_cycle <= 4);
    wire [31:0] rx_hdr_word = (rd_cycle[2:0] == 3'd1) ? rx_hdr[31:0] :
                              (rd_cycle[2:0] == 3'd2) ? rx_hdr[63:32] :
                              (rd_cycle[2:0] == 3'd3) ? rx_hdr[95:64] : rx_hdr[127:96];
    assign rtag_ren = !CH32H417_RD_n && (rd_cycle == 0) && read_bus &&
                      rx_usb_ready && rx_space_f;
    always @(posedge CH32H417_CLK) if (rtag_ren) rx_hdr <= rtag_rdata;
    // TX header capture (raw little-endian bus words) and validation.
    reg [127:0] tx_hdr;
    reg tx_hdr_ok;
    always @(posedge CH32H417_CLK) begin
        if (tx_take && wr_cycle < 4) tx_hdr[wr_cycle[1:0]*32 +: 32] <= tx_pin_data;
        tx_hdr_ok <= (tx_hdr[31:16] == 16'h4F43) &&
                     (tx_hdr[7:0] == hdr_check(tx_hdr[31:8], tx_hdr[127:32]));
    end
    // Push the tag as soon as the header is validated (word 5), not at the end
    // of the block: the payload arrives ~3x faster than the DAC consumes it,
    // so playback may start while the rest of the block is still written.
    wire wtag_wen = tx_take && (wr_cycle == 13'd5);
    wire [47:0] rx_iq8_frame = (rx_phase == 0) ? rx_dout[95:48] : rx_dout[47:0];
    wire [31:0] rx_word = iq8_active_u ?
        {rx_iq8_frame[47:40],rx_iq8_frame[35:28],rx_iq8_frame[23:16],rx_iq8_frame[11:4]} :
        ((rx_phase == 0) ? rx_dout[95:64] :
         (rx_phase == 1) ? rx_dout[63:32] : rx_dout[31:0]);
    // Bus word bits [7:0] are the first byte in the CH32 little-endian buffer:
    // header words go out as is, IQ words through swap32 (big-endian packing).
    // Register all 32 output bits together in the I/O bank. The previous
    // combinational cache/phase mux allowed word transitions at the CH32
    // sampling edge. Keep output enable independent of RDNE for the last word.
    (* IOB = "TRUE" *) reg [31:0] rx_pin_word;
    // A separate fabric register observes the same D/clock without loading the
    // IOB register Q. DONT_TOUCH prevents synthesis from merging the two banks.
    // This probe is an equal-value shadow, not an electrical readback of the pad.
    (* IOB = "FALSE", KEEP = "TRUE", DONT_TOUCH = "TRUE", MARK_DEBUG = "TRUE" *)
    reg [31:0] dbg_usb_rx_word;
    always @(posedge CH32H417_CLK) begin
        rx_pin_word <= rx_hdr_slot ? rx_hdr_word : (rx_valid ? swap32(rx_word) : 32'd0);
        dbg_usb_rx_word <= rx_hdr_slot ? rx_hdr_word : (rx_valid ? swap32(rx_word) : 32'd0);
    end
    assign CH32H417_DATA = (!CH32H417_CS_n && !CH32H417_OE_n &&
        CH32H417_WR_n && CH32H417_SEL == 2'b01 && rx_usb_ready) ?
        rx_pin_word : 32'bz;
    always @(posedge CH32H417_CLK or negedge rx_u_guard) begin
        if (!rx_u_guard) begin
            rd_cycle <= 0;
            rx_admitted <= 0;
            rx_phase <= 0;
        end
        else begin
            if (CH32H417_RD_n) begin
                rd_cycle <= 0;
                rx_admitted <= 0;
                rx_phase <= 0;          // every block is phase aligned
            end
            else begin
                if (rd_cycle < 4102) rd_cycle <= rd_cycle + 1'b1;
                if (rd_cycle == 0)
                    rx_admitted <= read_bus && rx_usb_ready && rx_space_sel;
            end
            if (rx_take) rx_phase <= (rx_phase == last_phase) ? 0 : rx_phase + 1'b1;
        end
    end
    always @(posedge CH32H417_CLK or negedge tx_u_guard) begin
        if (!tx_u_guard) begin
            wr_cycle <= 0;
            tx_admitted <= 0;
            tx_phase <= 0;
            tx_partial <= 0;
            tx_iq8_partial <= 0;
        end else begin
            if (tx_pin_wr_n) begin
                wr_cycle <= 0;
                tx_admitted <= 0;
                tx_phase <= 0;
            end
            else begin
                if (wr_cycle < 4097) wr_cycle <= wr_cycle + 1'b1;
                if (wr_cycle == 0) tx_admitted <= write_bus && tx_usb_ready && tx_space_sel;
            end
            if (tx_take_pl) begin
                if (tx_phase == 0) tx_partial[63:32] <= tx_word;
                if (tx_phase == 1) tx_partial[31:0] <= tx_word;
                if (tx_phase == 0) tx_iq8_partial <= tx_iq8_frame;
                tx_phase <= (tx_phase == last_phase) ? 0 : tx_phase + 1'b1;
            end
        end
    end
    // 4092个载荷字正好是整数组，每块结束相位归零，块之间没有拼接。

    // ========================================================================
    // 7. DAC消费缓存。数据在消费沿之前已经有效，不额外延迟DAC输出。
    // ========================================================================
    // 预填充期间不报下溢；启动后缺数输出0并锁存错误，后来有数继续输出。
    wire dac_need = cfg_d[4] ?
        (dac_enable_0 && dac_enable_1 && dac_valid_0 && dac_valid_1) :
        (cfg_d[5] ? (dac_enable_1 && dac_valid_1) : (dac_enable_0 && dac_valid_0));

    // ------------------------------------------------------------------------
    // 7b. 成帧TX（dac_clk == adc_clk）：按块头时间精确起播。
    // ------------------------------------------------------------------------
    reg d_run;   // registered: tx_dac_ready && TX_RUN
    always @(posedge dac_clk or negedge tx_d_guard)
        if (!tx_d_guard) d_run <= 1'b0;
        else d_run <= tx_dac_ready && cfg_d[6];
    wire [12:0] fr_d_samples = !cfg_d[4] ? FR_S_SINGLE : (cfg_d[7] ? FR_S_IQ8 : FR_S_DUAL);
    wire tf_empty;
    wire [128:0] tf_rdata;
    wire dtag_ren;                 // consumer takes the prefetched tag
    reg nt_v, nt_vok;
    reg [128:0] nt_q;
    wire tf_ren = !tf_empty && !nt_v;
    oc_afifo #(.W(129), .AW(3)) u_tx_tag (
        .wclk(CH32H417_CLK), .wrstn(tx_u_guard), .wen(wtag_wen),
        .wdata({tx_hdr_ok, tx_hdr}), .wfull(wtag_full),
        .rclk(dac_clk), .rrstn(tx_d_guard), .ren(tf_ren), .rdata(tf_rdata),
        .rempty(tf_empty)
    );
    always @(posedge dac_clk or negedge tx_d_guard) begin
        if (!tx_d_guard) begin
            nt_v <= 1'b0; nt_q <= 0; nt_vok <= 1'b0;
        end else if (tf_ren) begin
            nt_v <= 1'b1; nt_q <= tf_rdata;
            nt_vok <= (tf_rdata[47:32] <= {3'b000, fr_d_samples});
        end else if (dtag_ren) begin
            nt_v <= 1'b0;
        end
    end
    wire dtag_empty = !nt_v;
    wire [128:0] dtag_rdata = nt_q;
    wire        nt_ok       = dtag_rdata[128];
    wire [7:0]  nt_flags    = dtag_rdata[15:8];
    wire [15:0] nt_valid    = dtag_rdata[47:32];
    wire [15:0] nt_seq      = dtag_rdata[63:48];
    wire [63:0] nt_time     = dtag_rdata[127:64];
    wire        nt_has_time = nt_flags[0];
    wire        nt_sob      = nt_flags[1];
    wire        nt_eob      = nt_flags[3];
    wire        nt_valid_ok = nt_vok;

    localparam [1:0] D_IDLE = 2'd0, D_WAIT = 2'd1, D_PLAY = 2'd2, D_DISC = 2'd3;
    reg [1:0] dst;
    reg [12:0] d_cnt, d_valid;
    reg [63:0] d_T;
    reg [63:0] d_Tc;   // d_T - 2: arm one sample early, first pop at T
    reg d_armed;       // WAIT: frame 0 presented, pop at the next strobe
    reg d_untimed;     // WAIT without a time: arm at the next strobe
    reg [15:0] d_seq, d_exp;
    reg d_eob, d_in_burst, d_drop, d_play_blk, d_uf;
    reg [12:0] d_vm1;          // d_valid - 1, registered at load
    reg d_cnt_last, d_cnt_vend; // registered d_cnt == samples-1, d_cnt == d_valid-1
    reg d_vfull;               // d_valid == samples per block
    reg d_play_q;              // registered play/start decision (drives output)
    reg rq_uf, rq_late, rq_ack, rq_seq, rq_hdr;   // event requests
    reg [1:0] d_settle;
    reg t_eq, t_hi_gt, t_hi_eq, t_lo_gt, t_gt;
    always @(posedge dac_clk) begin
        t_eq <= (time_now == d_Tc);
        t_hi_gt <= time_now[63:32] > d_Tc[63:32];
        t_hi_eq <= time_now[63:32] == d_Tc[63:32];
        t_lo_gt <= time_now[31:0] > d_Tc[31:0];
        t_gt <= t_hi_gt || (t_hi_eq && t_lo_gt);
    end
    wire d_settled = (d_settle == 2'd0);
    // The axi_ad9361 DAC path consumes the value presented about two clocks
    // before the exported dac_valid strobe. Frame 0 is therefore presented
    // one strobe early (arm) and popped at the strobe of time T.
    wire d_start_ok = (dst == D_WAIT) && d_settled && t_eq && !d_armed;
    wire d_playing = d_run && ((dst == D_PLAY) || ((dst == D_WAIT) && d_armed));
    wire d_play_pop = d_run && ((dst == D_PLAY) || ((dst == D_WAIT) && d_armed)) &&
                      dac_need && !tx_empty;
    wire d_arm = d_run && (dst == D_WAIT) && !d_armed && (d_untimed || (d_settled && d_play_q)) &&
                 dac_need && !tx_empty;
    wire d_disc_pop = d_run && (dst == D_DISC) && !tx_empty;
    wire d_pop = d_play_pop || d_disc_pop;
    wire d_blk_end = d_pop && d_cnt_last;
    wire d_valid_end = d_play_pop && d_cnt_vend && !d_vfull;
    wire d_late = d_run && (dst == D_WAIT) && d_settled && t_gt && !d_armed && !d_play_q && !d_untimed;
    wire d_end_eob = d_blk_end && d_eob;
    wire d_burst_eff = d_in_burst && !d_end_eob;
    wire d_drop_eff = d_drop && !d_end_eob;
    assign dtag_ren = d_run && !dtag_empty && ((dst == D_IDLE) || d_blk_end);
    reg ed_v;
    reg [7:0] ed_code;
    reg [15:0] ed_seq;
    reg [63:0] ed_time;
    reg [63:0] tn_q;
    reg [15:0] d_seq_prev;
    // Event word built one cycle after the request flags (short paths).
    always @(posedge dac_clk or negedge tx_d_guard) begin
        if (!tx_d_guard) begin
            ed_v <= 0; ed_code <= 0; ed_seq <= 0; ed_time <= 0; tn_q <= 0; d_seq_prev <= 0;
            d_play_q <= 1'b0;
        end else begin
            tn_q <= time_now;
            d_seq_prev <= d_seq;
            d_play_q <= d_start_ok;
            ed_v <= rq_uf || rq_late || rq_ack || rq_seq || rq_hdr;
            if (rq_hdr) begin
                ed_code <= EV_TX_HEADER; ed_seq <= d_seq; ed_time <= tn_q;
            end else if (rq_seq) begin
                ed_code <= EV_TX_SEQ; ed_seq <= d_seq; ed_time <= tn_q;
            end else if (rq_late) begin
                ed_code <= EV_TX_LATE; ed_seq <= d_seq_prev; ed_time <= d_T;
            end else if (rq_ack) begin
                ed_code <= EV_TX_ACK; ed_seq <= d_seq_prev; ed_time <= tn_q;
            end else if (rq_uf) begin
                ed_code <= EV_TX_UNDERFLOW; ed_seq <= d_seq_prev; ed_time <= tn_q;
            end
        end
    end
    always @(posedge dac_clk or negedge tx_d_guard) begin
        if (!tx_d_guard) begin
            dst <= D_IDLE; d_cnt <= 0; d_cnt_last <= 1'b0; d_cnt_vend <= 1'b0; d_valid <= 0; d_T <= 64'hFFFF_FFFF_FFFF_FFFF; d_Tc <= 64'hFFFF_FFFF_FFFF_FFFD; d_armed <= 1'b0; d_untimed <= 1'b0;
            d_seq <= 0; d_exp <= 0; d_eob <= 0; d_in_burst <= 0; d_drop <= 0;
            d_play_blk <= 0; d_uf <= 0; d_settle <= 0; d_vm1 <= 0; d_vfull <= 0;
            rq_uf <= 0; rq_late <= 0; rq_ack <= 0; rq_seq <= 0; rq_hdr <= 0;
        end else begin
            rq_uf <= 1'b0; rq_late <= 1'b0; rq_ack <= 1'b0; rq_seq <= 1'b0; rq_hdr <= 1'b0;
            if (d_settle != 0) d_settle <= d_settle - 1'b1;
            if (!d_run) begin
                dst <= D_IDLE; d_cnt <= 0; d_cnt_last <= 1'b0; d_cnt_vend <= (d_vm1 == 13'd0); d_in_burst <= 0; d_drop <= 0; d_uf <= 0;
            end else begin
                if (d_pop) begin
                    d_cnt <= d_cnt + 1'b1;
                    d_cnt_last <= (d_cnt == fr_d_samples - 2'd2);
                    d_cnt_vend <= (d_cnt + 1'b1 == d_vm1);
                end
                if (dac_need && !d_drop && ((dst == D_PLAY && tx_empty) ||
                                 (dst == D_IDLE && d_in_burst && dtag_empty))) begin
                    if (!d_uf) rq_uf <= 1'b1;
                    d_uf <= 1'b1;
                end
                if (d_play_pop) d_uf <= 1'b0;
                if (d_arm) d_armed <= 1'b1;
                if (dst == D_WAIT && d_play_pop) begin dst <= D_PLAY; d_armed <= 1'b0; end
                if (d_late) begin
                    rq_late <= 1'b1;
                    d_drop <= 1'b1; d_play_blk <= 1'b0; dst <= D_DISC;
                end
                if (d_valid_end) dst <= D_DISC;
                if (d_blk_end) begin
                    d_cnt <= 0; d_cnt_last <= 1'b0; d_cnt_vend <= (d_vm1 == 13'd0);
                    dst <= D_IDLE;
                    if (d_eob) begin
                        if (d_play_blk) rq_ack <= 1'b1;
                        d_in_burst <= 1'b0; d_drop <= 1'b0;
                    end
                end
                if (dtag_ren) begin
                    d_cnt <= 0; d_cnt_last <= 1'b0; d_cnt_vend <= (d_vm1 == 13'd0);
                    d_T <= nt_time; d_Tc <= nt_time - 2'd2; d_armed <= 1'b0; d_valid <= nt_valid[12:0]; d_eob <= nt_eob;
                    d_vm1 <= nt_valid[12:0] - 1'b1; d_vfull <= (nt_valid[12:0] == fr_d_samples);
                    d_cnt_vend <= (nt_valid[12:0] == 13'd1); d_cnt_last <= 1'b0;
                    d_seq <= nt_seq; d_exp <= nt_seq + 1'b1;
                    if (nt_ok && d_burst_eff && !nt_sob && nt_seq != d_exp) rq_seq <= 1'b1;
                    if (!nt_ok || !nt_valid_ok) begin
                        rq_hdr <= 1'b1;
                        d_drop <= 1'b1; d_play_blk <= 1'b0; d_eob <= 1'b0; dst <= D_DISC;
                    end else if (d_drop_eff && !nt_sob) begin
                        d_play_blk <= 1'b0; dst <= D_DISC;
                    end else if (nt_valid == 0) begin
                        d_drop <= 1'b0; d_play_blk <= d_burst_eff; dst <= D_DISC;
                    end else begin
                        d_drop <= 1'b0; d_play_blk <= 1'b1; d_in_burst <= 1'b1;
                        if (nt_has_time) begin
                            dst <= D_WAIT; d_settle <= 2'd3; d_untimed <= 1'b0;
                        end else if (d_blk_end && dst == D_PLAY) begin
                            dst <= D_PLAY;               // seamless continuation in a burst
                        end else begin
                            dst <= D_WAIT; d_untimed <= 1'b1;   // untimed start: arm first
                        end
                    end
                end
            end
        end
    end

    wire dac_pair_bad = tx_dac_ready && cfg_d[6] && (dst == D_PLAY) &&
        cfg_d[4] && (dac_valid_0 || dac_valid_1) &&
        (!dac_enable_0 || !dac_enable_1 || (dac_valid_0 != dac_valid_1));
    assign tx_pop = d_pop;
    wire dac_present = d_playing && !tx_empty;
    wire [15:0] ext_i0 = cfg_d[4] ? {dac_sample[47:36],4'b0} :
                        (cfg_d[5] ? 16'd0 : {dac_sample[23:12],4'b0});
    wire [15:0] ext_q0 = cfg_d[4] ? {dac_sample[35:24],4'b0} :
                        (cfg_d[5] ? 16'd0 : {dac_sample[11:0],4'b0});
    wire [15:0] ext_i1 = cfg_d[4] ? {dac_sample[23:12],4'b0} :
                        (cfg_d[5] ? {dac_sample[23:12],4'b0} : 16'd0);
    wire [15:0] ext_q1 = cfg_d[4] ? {dac_sample[11:0],4'b0} :
                        (cfg_d[5] ? {dac_sample[11:0],4'b0} : 16'd0);
    assign dac_data_i0_out = !cfg_d[1] ? dac_data_i0 : (dac_present ? ext_i0 : 16'd0);
    assign dac_data_q0_out = !cfg_d[1] ? dac_data_q0 : (dac_present ? ext_q0 : 16'd0);
    assign dac_data_i1_out = !cfg_d[1] ? dac_data_i1 : (dac_present ? ext_i1 : 16'd0);
    assign dac_data_q1_out = !cfg_d[1] ? dac_data_q1 : (dac_present ? ext_q1 : 16'd0);

    // ------------------------------------------------------------------------
    // 7c. 事件FIFO：RX/TX事件（带样点时间）送到AXI时钟域。
    // adc_clk 与 dac_clk 为同一时钟，因此两侧事件寄存器可在同一域合并。
    // ------------------------------------------------------------------------
    reg ev_pend_a, ev_pend_d, ev_lost;
    reg [95:0] ev_word_a, ev_word_d;
    wire evf_full;
    wire evf_wen = !evf_full && (ev_pend_d || ev_pend_a);
    wire [95:0] evf_wdata = ev_pend_d ? (ev_word_d | {8'd0, ev_lost, 87'd0}) :
                                        (ev_word_a | {8'd0, ev_lost, 87'd0});
    always @(posedge adc_clk or negedge adc_resetn) begin
        if (!adc_resetn) begin
            ev_pend_a <= 0; ev_pend_d <= 0; ev_lost <= 0; ev_word_a <= 0; ev_word_d <= 0;
        end else begin
            if (evf_wen) begin
                ev_lost <= 1'b0;
                if (ev_pend_d) ev_pend_d <= 1'b0; else ev_pend_a <= 1'b0;
            end
            if (ea_v) begin
                if (ev_pend_a && !(evf_wen && !ev_pend_d)) ev_lost <= 1'b1;
                ev_pend_a <= 1'b1;
                ev_word_a <= {ea_code, 8'd0, ea_seq, ea_time};
            end
            if (ed_v) begin
                if (ev_pend_d && !evf_wen) ev_lost <= 1'b1;
                ev_pend_d <= 1'b1;
                ev_word_d <= {ed_code, 8'd0, ed_seq, ed_time};
            end
        end
    end
    oc_afifo #(.W(96), .AW(5)) u_events (
        .wclk(adc_clk), .wrstn(adc_resetn), .wen(evf_wen), .wdata(evf_wdata), .wfull(evf_full),
        .rclk(ctrl_clk), .rrstn(ctrl_resetn), .ren(evt_pop), .rdata(evt_word), .rempty(evt_empty)
    );
    // Debug/status bits for software, each synchronized independently.
    wire [15:0] frame_status_raw = {6'd0, cfg_a[9], rtag_lost, d_drop, d_in_burst, dst,
                                    w_gap_next, rxc_pend, wst};
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg [15:0] fs_meta, fs_sync;
    always @(posedge ctrl_clk or negedge ctrl_resetn) begin
        if (!ctrl_resetn) begin
            fs_meta <= 0; fs_sync <= 0;
        end else begin
            fs_meta <= frame_status_raw;
            fs_sync <= fs_meta;
        end
    end
    assign frame_status = fs_sync;

    // ========================================================================
    // 8. 清错误命令：兼容原AXI普通RW寄存器，在本模块检测bit0的上升沿。
    // ========================================================================
    // 内部翻转请求不会像单拍脉冲那样被慢时钟漏掉，忙时还可暂存一次请求。
    reg clear_bit_d, clear_req, clear_pending;
    wire [2:0] clear_acks;
    wire clear_rise = CH32H417_CTRL_REG1[0] && !clear_bit_d;
    wire clear_idle = (clear_acks == {3{clear_req}});
    assign clear_launch = clear_idle && (clear_pending || clear_rise);
    wire clear_busy = !clear_idle || clear_pending || clear_rise;
    always @(posedge ctrl_clk or negedge ctrl_resetn) begin
        if (!ctrl_resetn) begin
            clear_bit_d <= 0;
            clear_req <= 0;
            clear_pending <= 0;
        end
        else begin
            clear_bit_d <= CH32H417_CTRL_REG1[0];
            if (clear_rise) clear_pending <= 1;
            if (clear_launch) begin
                clear_req <= !clear_req;
                clear_pending <= 0;
            end
        end
    end
    wire clear_u, clear_a, clear_d;
    reg clear_ack_u, clear_ack_a, clear_ack_d;
    // USB清错请求：CH32H417_CLK 域的两拍同步；只同步独立的状态位。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg clear_u_meta, clear_u_sync;
    always @(posedge CH32H417_CLK or negedge usb_resetn) begin
        if (!usb_resetn) begin
            clear_u_meta <= 0;
            clear_u_sync <= 0;
        end else begin
            clear_u_meta <= clear_req;
            clear_u_sync <= clear_u_meta;
        end
    end
    assign clear_u = clear_u_sync;
    // ADC清错请求：adc_clk 域的两拍同步；只同步独立的状态位。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg clear_a_meta, clear_a_sync;
    always @(posedge adc_clk or negedge adc_resetn) begin
        if (!adc_resetn) begin
            clear_a_meta <= 0;
            clear_a_sync <= 0;
        end else begin
            clear_a_meta <= clear_req;
            clear_a_sync <= clear_a_meta;
        end
    end
    assign clear_a = clear_a_sync;
    // DAC清错请求：dac_clk 域的两拍同步；只同步独立的状态位。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg clear_d_meta, clear_d_sync;
    always @(posedge dac_clk or negedge dac_resetn) begin
        if (!dac_resetn) begin
            clear_d_meta <= 0;
            clear_d_sync <= 0;
        end else begin
            clear_d_meta <= clear_req;
            clear_d_sync <= clear_d_meta;
        end
    end
    assign clear_d = clear_d_sync;
    // 清错应答回传：ctrl_clk 域的两拍同步；只同步独立的状态位。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg [2:0] clear_acks_meta, clear_acks_sync;
    always @(posedge ctrl_clk or negedge ctrl_resetn) begin
        if (!ctrl_resetn) begin
            clear_acks_meta <= 0;
            clear_acks_sync <= 0;
        end else begin
            clear_acks_meta <= {clear_ack_d,clear_ack_a,clear_ack_u};
            clear_acks_sync <= clear_acks_meta;
        end
    end
    assign clear_acks = clear_acks_sync;

    // ========================================================================
    // 9. 错误在发生的时钟域锁存，错误置位优先于清除。
    // ========================================================================
    // 空/满本身不是错误；只有确实需要读/写却不能完成时才记上下溢出。
    reg [1:0] err_a, err_d;
    reg [3:0] err_u;
    // ADC overflow / DAC underflow are reported (with time) by the event FIFO.
    wire [1:0] event_a = {adc_pair_bad, 1'b0};
    wire [1:0] event_d = {dac_pair_bad, 1'b0};
    wire rx_protocol = rx_usb_ready && (
        (!CH32H417_RD_n && rd_cycle==0 && (!read_bus || !rx_space_sel)) ||
        (CH32H417_RD_n && rd_cycle!=0 && rd_cycle!=4101) ||
        (!CH32H417_RD_n && rd_cycle>=4101) ||
        (!CH32H417_RD_n && rd_cycle!=0 && !read_bus));
    wire tx_protocol = tx_usb_ready && (
        (!tx_pin_wr_n && wr_cycle==0 && (!write_bus || !tx_space_sel)) ||
        (tx_pin_wr_n && wr_cycle!=0 && wr_cycle!=4096) ||
        (!tx_pin_wr_n && wr_cycle>=4096) ||
        (!tx_pin_wr_n && wr_cycle!=0 && !write_bus));
    wire [3:0] event_u = {tx_protocol,rx_protocol,
                          tx_window && tx_full,
                          rx_window_f && rx_empty};
    always @(posedge adc_clk or negedge adc_resetn) begin
        if (!adc_resetn) begin
            err_a <= 0;
            clear_ack_a <= 0;
        end
        else begin
            err_a <= ((clear_a != clear_ack_a) ? 2'b0 : err_a) | event_a;
            clear_ack_a <= clear_a;
        end
    end
    always @(posedge dac_clk or negedge dac_resetn) begin
        if (!dac_resetn) begin
            err_d <= 0;
            clear_ack_d <= 0;
        end
        else begin
            err_d <= ((clear_d != clear_ack_d) ? 2'b0 : err_d) | event_d;
            clear_ack_d <= clear_d;
        end
    end
    always @(posedge CH32H417_CLK or negedge usb_resetn) begin
        if (!usb_resetn) begin
            err_u <= 0;
            clear_ack_u <= 0;
        end
        else begin
            err_u <= ((clear_u != clear_ack_u) ? 4'b0 : err_u) | event_u;
            clear_ack_u <= clear_u;
        end
    end
    wire [1:0] errors_a, errors_d;
    wire [3:0] errors_u;
    // ADC锁存错误回传：ctrl_clk 域的两拍同步；只同步独立的状态位。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg [1:0] errors_a_meta, errors_a_sync;
    always @(posedge ctrl_clk or negedge ctrl_resetn) begin
        if (!ctrl_resetn) begin
            errors_a_meta <= 0;
            errors_a_sync <= 0;
        end else begin
            errors_a_meta <= err_a;
            errors_a_sync <= errors_a_meta;
        end
    end
    assign errors_a = errors_a_sync;
    // DAC锁存错误回传：ctrl_clk 域的两拍同步；只同步独立的状态位。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg [1:0] errors_d_meta, errors_d_sync;
    always @(posedge ctrl_clk or negedge ctrl_resetn) begin
        if (!ctrl_resetn) begin
            errors_d_meta <= 0;
            errors_d_sync <= 0;
        end else begin
            errors_d_meta <= err_d;
            errors_d_sync <= errors_d_meta;
        end
    end
    assign errors_d = errors_d_sync;
    // USB锁存错误回传：ctrl_clk 域的两拍同步；只同步独立的状态位。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg [3:0] errors_u_meta, errors_u_sync;
    always @(posedge ctrl_clk or negedge ctrl_resetn) begin
        if (!ctrl_resetn) begin
            errors_u_meta <= 0;
            errors_u_sync <= 0;
        end else begin
            errors_u_meta <= err_u;
            errors_u_sync <= errors_u_meta;
        end
    end
    assign errors_u = errors_u_sync;
    wire [7:0] errors = {errors_d[1],errors_a[1],errors_u[3],errors_u[2],
                         errors_d[0],errors_u[1],errors_u[0],errors_a[0]};
    assign Interrupt_flag = (|errors) || config_error;

    // ========================================================================
    // 10. 水位和实时状态用握手快照传给AXI，避免读到撕裂的多位计数。
    // ========================================================================
    reg status_req, status_ack_u, status_seen;
    reg [44:0] status_held, status_snapshot;
    reg [4:0] status_timer;
    wire status_req_u, status_ack;
    wire [1:0] dac_live_u;
    // DAC运行状态：CH32H417_CLK 域的两拍同步；只同步独立的状态位。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg [1:0] dac_live_u_meta, dac_live_u_sync;
    always @(posedge CH32H417_CLK or negedge usb_resetn) begin
        if (!usb_resetn) begin
            dac_live_u_meta <= 0;
            dac_live_u_sync <= 0;
        end else begin
            dac_live_u_meta <= {tx_dac_ready,d_run};
            dac_live_u_sync <= dac_live_u_meta;
        end
    end
    assign dac_live_u = dac_live_u_sync;
    // 状态快照请求：CH32H417_CLK 域的两拍同步；只同步独立的状态位。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg status_req_u_meta, status_req_u_sync;
    always @(posedge CH32H417_CLK or negedge usb_resetn) begin
        if (!usb_resetn) begin
            status_req_u_meta <= 0;
            status_req_u_sync <= 0;
        end else begin
            status_req_u_meta <= status_req;
            status_req_u_sync <= status_req_u_meta;
        end
    end
    assign status_req_u = status_req_u_sync;
    // 状态快照应答：ctrl_clk 域的两拍同步；只同步独立的状态位。
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *)
    reg status_ack_meta, status_ack_sync;
    always @(posedge ctrl_clk or negedge ctrl_resetn) begin
        if (!ctrl_resetn) begin
            status_ack_meta <= 0;
            status_ack_sync <= 0;
        end else begin
            status_ack_meta <= status_ack_u;
            status_ack_sync <= status_ack_meta;
        end
    end
    assign status_ack = status_ack_sync;
    always @(posedge CH32H417_CLK or negedge usb_resetn) begin
        if (!usb_resetn) begin
            status_ack_u <= 0;
            status_held <= 0;
        end
        else if (status_req_u != status_ack_u) begin
            status_held <= {cfg_u[9:0],dac_live_u[1],rx_a_seen_u,
                tx_usb_ready,rx_usb_ready,(wr_cycle!=0),(rd_cycle!=0),
                dac_live_u[0],CH32H417_WRNF,CH32H417_RDNE,tx_count,rx_count};
            status_ack_u <= status_req_u;
        end
    end
    always @(posedge ctrl_clk or negedge ctrl_resetn) begin
        if (!ctrl_resetn) begin
            status_req <= 0;
            status_snapshot <= 0;
            status_timer <= 0;
            status_seen <= 0;
        end else if (status_ack != status_seen) begin
            status_snapshot <= status_held;
            status_seen <= status_ack;
            status_timer <= 0;
        end else if (status_ack == status_req) begin
            if (status_timer==31) begin
                status_req <= !status_req;
                status_timer <= 0;
            end
            else status_timer <= status_timer + 1'b1;
        end
    end
    // STATUS1[31] = applied RX_TIMED; STATUS0[31] = applied FRAMED,
    // STATUS0[30] = time/framing capability of this PL build.
    assign CH32H417_STATUS_REG1 =
        {status_snapshot[44],2'b0,status_snapshot[25:13],3'b0,status_snapshot[12:0]};
    assign CH32H417_STATUS_REG0 = {1'b1,1'b1,1'b1,status_snapshot[42],config_error,status_snapshot[41],
        status_snapshot[36:35],status_snapshot[40:37],status_snapshot[34:31],
        clear_busy,cfg_busy,status_snapshot[30:26],Interrupt_flag,errors};

    // Named observation nets for the isolated validation build. No ILA or FIFO
    // is instantiated here. USB probes use CH32H417_CLK; ADC/DAC probes use
    // their corresponding clocks. MARK_DEBUG only preserves these nets.
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [95:0] dbg_usb_rx_group = rx_dout;
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [95:0] dbg_usb_tx_group = tx_din;
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [31:0] dbg_usb_tx_word = tx_pin_data;
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [15:0] dbg_usb_flags =
        {iq8_active_u,rx_usb_ready,tx_usb_ready,rx_hdr_slot,rx_valid,rx_take,rx_pop,
         tx_take,tx_push,rx_fifo_read,rx_return_valid,tx_full,rx_phase,tx_phase};
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [63:0] dbg_adc_iq =
        {adc_data_i0,adc_data_q0,adc_data_i1,adc_data_q1};
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [7:0] dbg_adc_flags =
        {cfg_a[7],rx_adc_ready,s3_v,rx_d_full,
         adc_enable_1,adc_enable_0,adc_valid_1,adc_valid_0};
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [63:0] dbg_dac_iq =
        {dac_data_i0_out,dac_data_q0_out,dac_data_i1_out,dac_data_q1_out};
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [47:0] dbg_dac_sample = dac_sample;
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [11:0] dbg_dac_flags =
        {cfg_d[7],tx_dac_ready,d_run,dac_present,tx_pop,tx_fifo_read,
         dac_return_valid,dac_sample_valid,dac_enable_1,dac_enable_0,dac_valid_1,dac_valid_0};
endmodule
`default_nettype wire
