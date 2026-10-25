`timescale 1ns / 1ps
`default_nettype none

// CH32H417 与 AD9361 连续流桥接，单模块 Standard FIFO 版本。
// RX = ADC -> FPGA -> CH32H417 -> PC；TX = 反方向。
// 整个文件只有一个 module；除下面四个 FIFO 外，没有其他模块例化。
// 同步、复位、配置握手、预读缓存全部直接写在本模块中。
// 阅读顺序：复位 -> 配置 -> FIFO -> 总线收发 -> DAC -> 清错/状态。
// 仅需自行生成下面四个 FIFO Generator 13.2 IP，名称必须完全一致：
//   ch417_std_adc_single : 写24位/深8192， 读96位/深2048，读计数11位。
//   ch417_std_adc_dual   : 写48位/深8192， 读96位/深4096，读计数12位。
//   ch417_std_dac_single : 写96位/深2048， 读24位/深8192， 写11/读13位计数。
//   ch417_std_dac_dual   : 写96位/深4096， 读48位/深8192， 写12/读13位计数。
// 共同设置：Native、Independent Clocks Block RAM、Standard FIFO。
// 读延迟1拍，关闭额外输出寄存器，开启高有效 VALID 和上述 Data Count。
// 异步复位，关闭内部 Reset Synchronization / Safety Circuit；
// 生成 wr_rst、rd_rst 端口；Full Flags Reset Value=1，Dout Reset Value=0。
// 本文件在每侧保持32拍复位，并等待对端准备好；三个流时钟必须运行。
// 四个 FIFO 使用 Standard 模式；本模块内部的4字缓存负责预读。
// 文件中的寄存器、always、assign 都是普通 RTL，不需要生成额外 IP。
//
// ctrl_clk 接 AXI 寄存器时钟，ctrl_resetn 接低有效 AXI 全局复位。
// 原 CTRL_REG0/1 是普通读写寄存器；原 STATUS_REG0/1 接只读状态。
// CTRL0：0 RX_EN，1 TX_EN，2 RX_DUAL，3 RX_CH1，4 TX_DUAL，5 TX_CH1，6 TX_RUN。
// CTRL0.bit7：IQ8；仅 RX_DUAL=TX_DUAL=1 时合法，复位默认12位。
// 改变IQ8时，当前和目标RX_EN/TX_EN/TX_RUN必须全部为0。
// 切换模式/通道：先关闭该方向，等待就绪位清零，再改模式，最后重新使能。
// CTRL1.bit0：每次写0、再写1，发起一次清错误；等 CLEAR_BUSY=0 后可写回0。
// 长期保持1不会持续清错误。清标志不清FIFO；关闭EN才清该方向FIFO及拼接相位。
// STATUS0：0..7 错误，8 IRQ，9 RDNE，10 WRNF，11 TX_STARTED，12/13 收发事务，
// 14 CFG_BUSY，15 CLEAR_BUSY，16/17 USB侧RX/TX就绪，18/19 ADC/DAC就绪，
// 20..23 实际模式/通道，24..26 实际RX_EN/TX_EN/TX_RUN，27 CFG_ERROR。
// STATUS0.bit28：实际IQ8；bit29：本版支持IQ8，固定为1。
// STATUS1：[12:0] RX组数(含预读)，[28:16] TX主FIFO组数；一组96位=12字节。
// IQ8保持FIFO内12位格式，一组在USB线上为8字节；每帧字节I0,Q0,I1,Q1。
// RX取各12位值[11:4]；TX将8位放回[11:4]、[3:0]补零。
// 配置非法时保留已应用配置，并置CFG_ERROR；原AXI寄存器仍读回软件写入值。
//
// 实测时序：OE比CS/RD提前1拍；RD低4101拍，只取第2..4097拍(从0计数)。
// WR低4096拍，第0..4095拍全部有效；CS多保持的1拍不取数据。
// RX先发生、随后RX/TX交替均可；本模块不规定主机的收发先后顺序。
// RX每次EN从0到1后，先发送16KiB字节递增测试块：00..FF循环64次。
// 测试块不消耗ADC FIFO；完整4101拍读事务结束后才切换到ADC数据。
// 首块提前结束/选通错误时，下次重发测试块；清错误不会重新触发测试块。
// AF_n/AE_n不属于本模块端口，继续在原顶层固定为0。
module CH32H417_Fsm #(
    parameter integer DAC_PREFILL_GROUPS = 1024  // 默认预存12KiB再启播
)(
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
    output wire [31:0] CH32H417_STATUS_REG0, CH32H417_STATUS_REG1
);
    // 每块4096个32位字；每3个总线字组成1个96位FIFO字。
    // 整块准入额外保留余量，不依赖计数在跨时钟边界完全实时。
    localparam RX_START_GROUPS = 1370; // ceil(4096/3)+4
    localparam RX_START_GROUPS_IQ8 = 2052; // 4096/2+4，FIFO宽度/深度不变
    // 单通道主FIFO为2048组，双通道仍为4096组；两种模式都必须留足一整块。
    // 16组余量覆盖计数跨时钟延迟以及上一块剩余的32/64位拼接数据。
    localparam TX_MAX_USED_SINGLE = 2048 - ((4096 + 2) / 3) - 16; // 666
    localparam TX_MAX_USED_DUAL   = 4096 - ((4096 + 2) / 3) - 16; // 2714
    localparam TX_MAX_USED_IQ8    = 4096 - (4096 / 2) - 16; // 2032
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
    reg [7:0] cfg_held;                 // 当前已接受的配置，含IQ8
    reg [31:0] cfg_seen;                // 上次已处理的软件寄存器值
    reg cfg_req, config_error;
    reg [7:0] cfg_u, cfg_a, cfg_d;       // 三个时钟域实际使用的配置
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
    wire config_bad = (|CH32H417_CTRL_REG0[31:8]) ||
        (CH32H417_CTRL_REG0[7] &&
         (!CH32H417_CTRL_REG0[2] || !CH32H417_CTRL_REG0[4])) ||
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
                    cfg_held <= CH32H417_CTRL_REG0[7:0];
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
    wire rx_s_full, rx_d_full, rx_s_empty, rx_d_empty, rx_s_valid, rx_d_valid;
    wire [10:0] rx_s_count;
    wire [11:0] rx_d_count;
    // Pipeline the sample and its mode-qualified write request together.
    // The FIFO full flag is checked at the actual write cycle, not one cycle
    // early; a rejected pending sample still raises the ADC overflow event.
    reg [23:0] adc_single_pipe;
    reg [47:0] adc_dual_pipe;
    reg adc_single_valid_pipe, adc_dual_valid_pipe;
    always @(posedge adc_clk) begin
        adc_single_pipe <= adc_single;
        adc_dual_pipe <= {adc_data_i0[11:0],adc_data_q0[11:0],
                          adc_data_i1[11:0],adc_data_q1[11:0]};
    end
    always @(posedge adc_clk or negedge rx_a_guard) begin
        if (!rx_a_guard) begin
            adc_single_valid_pipe <= 1'b0;
            adc_dual_valid_pipe <= 1'b0;
        end else begin
            adc_single_valid_pipe <= rx_adc_ready && adc_need && !cfg_a[2];
            adc_dual_valid_pipe <= rx_adc_ready && adc_need && cfg_a[2];
        end
    end
    wire rx_fifo_read;
    ch417_std_adc_single rx_single (
        .wr_clk(adc_clk), .wr_rst(!rx_a_guard),
        .rd_clk(CH32H417_CLK), .rd_rst(!rx_u_guard),
        .din(adc_single_pipe), .wr_en(adc_single_valid_pipe && !rx_s_full),
        .rd_en(rx_fifo_read && !cfg_u[2]), .dout(rx_s_dout),
        .full(rx_s_full), .empty(rx_s_empty), .valid(rx_s_valid),
        .rd_data_count(rx_s_count)
    );
    ch417_std_adc_dual rx_dual (
        .wr_clk(adc_clk), .wr_rst(!rx_a_guard),
        .rd_clk(CH32H417_CLK), .rd_rst(!rx_u_guard),
        .din(adc_dual_pipe), .wr_en(adc_dual_valid_pipe && !rx_d_full),
        .rd_en(rx_fifo_read && cfg_u[2]),
        .dout(rx_d_dout), .full(rx_d_full), .empty(rx_d_empty),
        .valid(rx_d_valid), .rd_data_count(rx_d_count)
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
    reg rx_test_pending, rx_test_active, rx_test_good;
    reg [5:0] rx_test_word; // 64 words = 256 bytes, repeats within the 16KiB block.
    wire rx_space = rx_test_pending || (rx_valid &&
        (rx_count >= (iq8_active_u ? RX_START_GROUPS_IQ8 : RX_START_GROUPS)));
    wire tx_space = !tx_full && (iq8_active_u ? (tx_count <= TX_MAX_USED_IQ8) :
        (cfg_u[4] ? (tx_count <= TX_MAX_USED_DUAL) : (tx_count <= TX_MAX_USED_SINGLE)));
    assign CH32H417_RDNE = rx_usb_ready && ((rd_cycle == 0) ? rx_space :
                                             (rx_admitted && rd_cycle < 4098));
    assign CH32H417_WRNF = tx_usb_ready && ((wr_cycle == 0) ? tx_space :
                                             (tx_admitted && wr_cycle < 4096));
    // Advance the source one cycle before the output register launches it.
    // Exactly 4096 words are consumed, preserving the 96-bit packing phase.
    wire rx_window = read_bus && rx_usb_ready && rd_cycle >= 1 && rd_cycle < 4097;
    wire tx_window = write_bus && tx_usb_ready && wr_cycle < 4096;
    wire rx_take = rx_window && rx_admitted && !rx_test_active && rx_valid;
    wire tx_take = tx_window && ((wr_cycle == 0) ? tx_space : tx_admitted);
    assign rx_pop = rx_take && (rx_phase == last_phase);
    assign tx_push = tx_take && (tx_phase == last_phase) && !tx_full;
    wire [47:0] rx_iq8_frame = (rx_phase == 0) ? rx_dout[95:48] : rx_dout[47:0];
    wire [31:0] rx_word = iq8_active_u ?
        {rx_iq8_frame[47:40],rx_iq8_frame[35:28],rx_iq8_frame[23:16],rx_iq8_frame[11:4]} :
        ((rx_phase == 0) ? rx_dout[95:64] :
         (rx_phase == 1) ? rx_dout[63:32] : rx_dout[31:0]);
    // Physical bus word 0x03020100 becomes bytes 00 01 02 03 in the
    // CH32 little-endian receive buffer, matching the existing ADC swap32.
    wire [31:0] rx_test_bus_word = {rx_test_word,2'b11,rx_test_word,2'b10,
                                  rx_test_word,2'b01,rx_test_word,2'b00};
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
        rx_pin_word <= rx_test_active ? rx_test_bus_word :
                       (rx_valid ? swap32(rx_word) : 32'd0);
        dbg_usb_rx_word <= rx_test_active ? rx_test_bus_word :
                           (rx_valid ? swap32(rx_word) : 32'd0);
    end
    assign CH32H417_DATA = (!CH32H417_CS_n && !CH32H417_OE_n &&
        CH32H417_WR_n && CH32H417_SEL == 2'b01 && rx_usb_ready) ?
        rx_pin_word : 32'bz;
    always @(posedge CH32H417_CLK or negedge rx_u_guard) begin
        if (!rx_u_guard) begin
            rd_cycle <= 0;
            rx_admitted <= 0;
            rx_phase <= 0;
            rx_test_pending <= 1;
            rx_test_active <= 0;
            rx_test_good <= 0;
            rx_test_word <= 0;
        end
        else begin
            if (CH32H417_RD_n) begin
                // Commit only a complete, correctly selected transaction.
                // Aborted/overlong test blocks are retried without touching IQ.
                if (rx_test_active && rx_test_good && rx_admitted && rd_cycle == 4101)
                    rx_test_pending <= 0;
                rd_cycle <= 0;
                rx_admitted <= 0;
                rx_test_active <= 0;
                rx_test_good <= 0;
                rx_test_word <= 0;
            end
            else begin
                if (rd_cycle < 4102) rd_cycle <= rd_cycle + 1'b1;
                if (rd_cycle == 0) begin
                    rx_admitted <= read_bus && rx_usb_ready && rx_space;
                    rx_test_active <= rx_test_pending;
                    rx_test_good <= read_bus && rx_usb_ready;
                end else if (!read_bus)
                    rx_test_good <= 0;
            end
            if (rx_window && rx_admitted && rx_test_active)
                rx_test_word <= rx_test_word + 1'b1;
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
            end
            else begin
                if (wr_cycle < 4097) wr_cycle <= wr_cycle + 1'b1;
                if (wr_cycle == 0) tx_admitted <= write_bus && tx_usb_ready && tx_space;
            end
            if (tx_take) begin
                if (tx_phase == 0) tx_partial[63:32] <= tx_word;
                if (tx_phase == 1) tx_partial[31:0] <= tx_word;
                if (tx_phase == 0) tx_iq8_partial <= tx_iq8_frame;
                tx_phase <= (tx_phase == last_phase) ? 0 : tx_phase + 1'b1;
            end
        end
    end
    // 4096除以3余1：块结束时只清rd_cycle/wr_cycle，不清rx_phase/tx_phase。
    // 因而连续块不会丢掉残留32/64位；有限TX建议按3块(48KiB)对齐。
    // IQ8每2个总线字一组，16KiB块正好2048组，块末相位回到0。

    // ========================================================================
    // 7. DAC消费缓存。数据在消费沿之前已经有效，不额外延迟DAC输出。
    // ========================================================================
    // 预填充期间不报下溢；启动后缺数输出0并锁存错误，后来有数继续输出。
    reg tx_started;
    wire dac_need = cfg_d[4] ?
        (dac_enable_0 && dac_enable_1 && dac_valid_0 && dac_valid_1) :
        (cfg_d[5] ? (dac_enable_1 && dac_valid_1) : (dac_enable_0 && dac_valid_0));
    wire dac_pair_bad = tx_dac_ready && cfg_d[6] && tx_started && cfg_d[4] &&
        (dac_valid_0 || dac_valid_1) &&
        (!dac_enable_0 || !dac_enable_1 || (dac_valid_0 != dac_valid_1));
    wire [14:0] dac_available = {2'b0,(cfg_d[4] ? tx_d_samples : tx_s_samples)} +
                               {12'd0,dac_reserved};
    wire prefilled = dac_available >= DAC_PREFILL_GROUPS * (cfg_d[4] ? 2 : 4);
    always @(posedge dac_clk or negedge dac_resetn) begin
        if (!dac_resetn) tx_started <= 0;
        else if (!tx_dac_ready || !cfg_d[6]) tx_started <= 0;
        else if (prefilled && dac_cached >= 2) tx_started <= 1;
    end
    assign tx_pop = tx_dac_ready && cfg_d[6] && tx_started && dac_need && !tx_empty;
    wire dac_present = tx_dac_ready && cfg_d[6] && tx_started && !tx_empty;
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
    wire [1:0] event_a = {adc_pair_bad,
        (adc_single_valid_pipe && rx_s_full) || (adc_dual_valid_pipe && rx_d_full)};
    wire [1:0] event_d = {dac_pair_bad,
        tx_dac_ready && cfg_d[6] && tx_started && dac_need && tx_empty};
    wire rx_protocol = rx_usb_ready && (
        (!CH32H417_RD_n && rd_cycle==0 && (!read_bus || !rx_space)) ||
        (CH32H417_RD_n && rd_cycle!=0 && rd_cycle!=4101) ||
        (!CH32H417_RD_n && rd_cycle>=4101) ||
        (!CH32H417_RD_n && rd_cycle!=0 && !read_bus));
    wire tx_protocol = tx_usb_ready && (
        (!tx_pin_wr_n && wr_cycle==0 && (!write_bus || !tx_space)) ||
        (tx_pin_wr_n && wr_cycle!=0 && wr_cycle!=4096) ||
        (!tx_pin_wr_n && wr_cycle>=4096) ||
        (!tx_pin_wr_n && wr_cycle!=0 && !write_bus));
    wire [3:0] event_u = {tx_protocol,rx_protocol,
                          tx_window && tx_full,rx_window && !rx_test_active && rx_empty};
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
    reg [42:0] status_held, status_snapshot;
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
            dac_live_u_meta <= {tx_dac_ready,tx_started};
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
            status_held <= {cfg_u[7:0],dac_live_u[1],rx_a_seen_u,
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
    assign CH32H417_STATUS_REG1 =
        {3'b0,status_snapshot[25:13],3'b0,status_snapshot[12:0]};
    assign CH32H417_STATUS_REG0 = {2'b0,1'b1,status_snapshot[42],config_error,status_snapshot[41],
        status_snapshot[36:35],status_snapshot[40:37],status_snapshot[34:31],
        clear_busy,cfg_busy,status_snapshot[30:26],Interrupt_flag,errors};

    // Named observation nets for the isolated validation build. No ILA or FIFO
    // is instantiated here. USB probes use CH32H417_CLK; ADC/DAC probes use
    // their corresponding clocks. MARK_DEBUG only preserves these nets.
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [95:0] dbg_usb_rx_group = rx_dout;
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [95:0] dbg_usb_tx_group = tx_din;
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [31:0] dbg_usb_tx_word = tx_pin_data;
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [15:0] dbg_usb_flags =
        {iq8_active_u,rx_usb_ready,tx_usb_ready,rx_test_active,rx_valid,rx_take,rx_pop,
         tx_take,tx_push,rx_fifo_read,rx_return_valid,tx_full,rx_phase,tx_phase};
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [63:0] dbg_adc_iq =
        {adc_data_i0,adc_data_q0,adc_data_i1,adc_data_q1};
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [7:0] dbg_adc_flags =
        {cfg_a[7],rx_adc_ready,adc_dual_valid_pipe,rx_d_full,
         adc_enable_1,adc_enable_0,adc_valid_1,adc_valid_0};
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [63:0] dbg_dac_iq =
        {dac_data_i0_out,dac_data_q0_out,dac_data_i1_out,dac_data_q1_out};
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [47:0] dbg_dac_sample = dac_sample;
    (* KEEP = "TRUE", MARK_DEBUG = "TRUE" *) wire [11:0] dbg_dac_flags =
        {cfg_d[7],tx_dac_ready,tx_started,dac_present,tx_pop,tx_fifo_read,
         dac_return_valid,dac_sample_valid,dac_enable_1,dac_enable_0,dac_valid_1,dac_valid_0};
endmodule
`default_nettype wire
