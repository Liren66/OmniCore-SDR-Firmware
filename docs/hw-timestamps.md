# 硬件时间戳与成帧收发（CH32H417_IP 1.3）

本功能参照 USRP B2x0 的 vita_time 设计：FPGA 内有一个 64 位样点计数器，RX 数据带时间戳，RX/TX 可以按样点精确定时。
v2026.11.07 起这是唯一的 USB 传输格式，需要 OmniCore Host 0.6.0 及以上。

## 1. 时间基准

- **计数器位置**：`CH32H417_IP` 内的 `oc_timekeeper`，运行在 `l_clk` 域。
- **计数规则**：每个 AD9361 接口样点选通（`adc_valid`）加 1。所以 1 tick = 1 个样点，tick_rate 等于当前采样率。
- **设置时间**：
  - `time_now=<ticks>`：立即装载。
  - `time_next_pps=<ticks>`：在所选 PPS 源的下一个上升沿装载。
  - `time_last_pps?`：读取上一个 PPS 之后第一个样点的时间。
- **PPS 源**（`time_source=`）：

  | 取值 | 含义 |
  |---|---|
  | `none` | 不使用 PPS |
  | `external` | J6 `PPS_EXT` → FPGA Y7（即 `gps_pps_0`），也同时接入 `axi_ad9361` 的 PPS 监测 |
  | `internal` | AD9361 `CLK_OUT` → FPGA U10，FPGA 将 40 MHz 分频得到 1 PPS（高电平 100 ms） |

  `internal` 需要 SDK 把 `clk_output_mode_select` 设为 `BUFFERED_XTALN_DCXO`（已在 `board_config.c` 中设置）。
  接入 10 MHz 后，ADF4002 会驯服该 40 MHz，内部 PPS 也随之与外参考同源。
- **失效条件**：采样率变化、`rf_reinit!` 或 PL 复位后，时间被置为无效。SDK 每 250 ms 检查一次采样率，变化时会上报 `EVT type=time_invalidated`。
- **状态**：`time_status?` 中的 `ref40_present` 表示 U10 上检测到 40 MHz；`radio_clock_stalled` 表示 `l_clk` 停止（例如 AD9361 未初始化）。

## 2. 成帧格式

每个 16 KiB UHSIF 块的结构是 16 字节头 + 16368 字节载荷。16368 能被 3、4、6、8、12 整除，所以每块都装整数个样点，块与块之间没有相位拼接。

| 模式 | 每帧字节 | 每块帧数 | 每块 96 位组 |
|---|---|---|---|
| 12bit 单通道 | 3 | 5456 | 1364 |
| 12bit 双通道 | 6 | 2728 | 1364 |
| IQ8 双通道 | 4 | 4092 | 2046 |

块头由 4 个小端 u32 组成：

```
h0 = 0x4F43_0000 | flags<<8 | check     // 字节序：check, flags, 0x43 'C', 0x4F 'O'
h1 = seq<<16 | valid                    // valid = 本块有效帧数，其余补零
h2 = 时间低32位, h3 = 时间高32位        // 本块第一个样点的时间
check = h0 的字节1..3 与 h1..h3 全部字节的 XOR
```

| 方向 | flags 位定义 |
|---|---|
| RX | bit0 TIME_VALID，bit1 SOB，bit2 GAP（本块之前丢过样），bit3 EOB，bit4 LATE，bit5 PAD |
| TX | bit0 HAS_TIME，bit1 SOB，bit3 EOB |

`seq` 是 16 位块序号，每个方向独立递增。TX 中 `valid=0` 的块会被静默丢弃。

### 样点打包

| 模式 | 字节布局 |
|---|---|
| 12bit | 每个 I/Q 对 3 字节：`I[11:4]`、`I[3:0]<<4 \| Q[11:8]`、`Q[7:0]`，有符号二补码；双通道顺序为 I1,Q1,I2,Q2 |
| IQ8 | `I1[11:4]`、`Q1[11:4]`、`I2[11:4]`、`Q2[11:4]`，每字节 signed8；RX 舍去低 4 位，TX 低 4 位补零 |

### 解析示例（C）

```c
static int parse_header(const uint8_t *b, uint32_t *flags, uint16_t *seq,
                        uint16_t *valid, uint64_t *time)
{
    uint32_t h[4];
    uint8_t x = 0;
    memcpy(h, b, 16);                       /* 小端主机 */
    if ((h[0] >> 16) != 0x4F43) return -1;  /* 魔数 'OC' */
    for (int i = 1; i < 16; ++i) x ^= b[i]; /* 字节 1..15 */
    if (x != b[0]) return -1;
    *flags = (h[0] >> 8) & 0xFF;
    *seq = h[1] >> 16;
    *valid = h[1] & 0xFFFF;
    *time = ((uint64_t)h[3] << 32) | h[2];
    return 0;
}
```

## 3. RX 与 TX 行为

- **RX 溢出**：FPGA 把当前块补零后结束，等主机把 FIFO 读到不足一块时再恢复。下一块置 GAP 标志，丢失的样点数等于两块时间戳之差，是精确值。
  不再需要 16 KiB 递增起始头和 RX 重启。
- **定时 RX**（`rx_timed=1`）：开启 RX 后 FPGA 等待命令。
  - `rx_stream=<ticks>|now <nsamp>`：在时间到达的那个样点开始接收，`nsamp=0` 表示连续接收。
  - 命令到达时如果时间已过：立即开始，并置 LATE 标志、产生 `rx_late_command` 事件。
  - `rx_stream_stop!`：以 EOB 块结束。
- **定时 TX**：
  - 带 HAS_TIME 的块在 `time == T` 的那个 DAC 样点开始播放。
  - 一个突发（burst）内的后续块无缝衔接，EOB 块之后回到空闲。
  - 迟到的块（`tx_late`）及其所在的整个突发会被丢弃。
  - 突发中途缺数据报 `tx_underflow`，突发结束报 `tx_burst_ack`。
- **事件上报**：事件都带样点时间，格式为 `EVT tseq=… type=… src=fpga_time code=… block=… time=…`，类型列表见 [sdk-commands.md](sdk-commands.md#异步事件)。

## 4. AXI 寄存器

基址 `0x43C10000`，共 32 个 32 位寄存器（`C_S00_AXI_ADDR_WIDTH = 7`）。

| 偏移 | 名称 | 说明 |
|---|---|---|
| 0x00/0x04/0x08/0x0C | CTRL0/CTRL1/STATUS0/STATUS1 | 原有寄存器。CTRL0.bit7 IQ8，bit8 保留（1.2 的 FRAMED，写入被忽略），bit9 RX_TIMED；STATUS0.bit31 成帧（恒为 1），bit30 时间能力位，bit11 TX 运行中；STATUS1.bit31 RX_TIMED 已生效 |
| 0x10/0x14 | TIME_SET_LO/HI | 待装载的时间 |
| 0x18 | TIME_CTRL | 写触发：[1:0] 1=立即，2=下一个 PPS，3=作废，0=只改 PPS 源；[5:4] PPS 源（0 无，1 外部，2 内部）；读 bit31 = busy |
| 0x1C | TIME_STATUS | bit0 armed，bit1 valid，[3:2] 源，bit4 ref40 存在，bit5 射频时钟停止 |
| 0x20/0x24 | TIME_NOW | 先读 LO，此时 HI 被锁存 |
| 0x28/0x2C | TIME_PPS | 上一个 PPS 的时间，先读 LO |
| 0x30/0x34 | RX_CMD_TIME_LO/HI | 定时 RX 开始时间 |
| 0x38 | RX_CMD_NSAMP | 样点数，0 为连续 |
| 0x3C | RX_CMD | 写触发：bit0 立即，bit1 停止；读 bit31 busy，低 16 位为成帧状态 |
| 0x40 | EVT_STATUS | 读 bit0 表示有事件；写 1 弹出一个事件 |
| 0x44 | EVT_INFO | [31:24] 事件码，bit23 之前丢过事件，[15:0] 块序号 |
| 0x48/0x4C | EVT_TIME_LO/HI | 事件的样点时间 |
| 0x50 | PPS_COUNT | PPS 计数 |
| 0x54 | VERSION | `0x01030001` |
| 0x58 | FRAME_INFO | 头 16 字节 / 载荷 16368 字节 |

驱动函数在 BSP 的 `CH32H417_IP.c/.h`：`CH32H417_IP_TimeCommand`、`GetTime`、`RxCommand`、`PopEvent`、`Version`。

## 5. 主机侧用法（OCH 0.6.0）

```c
och_set_time_source(dev, OCH_TIME_SOURCE_INTERNAL);   // 或 EXTERNAL
och_set_time(dev, 0, 1);                              // 下一个 PPS 沿置 0

/* 定时 RX：t = 1 s 起接收 4096 个样点 */
och_stream_args a; och_default_stream_args(&a, OCH_RX);
a.start_flags = OCH_RX_TIMED; a.num_samples = 4096;
a.start_time_lo = (uint32_t)T; a.start_time_hi = (uint32_t)(T >> 32);

/* 定时 TX：在样点时间 T 发出一个突发 */
och_tx_metadata md = {sizeof md,
    OCH_TX_HAS_TIME | OCH_TX_START_OF_BURST | OCH_TX_END_OF_BURST, T};
och_send_burst(tx, samples, n, &md, &accepted, 1000);
```

RX 的 `och_metadata` 中带 `time_ticks`、`tick_rate`、`device_dropped_samples`（由时间戳差得到的精确丢样数）和 EOB/LATE 标志。
完整说明见 OmniCore-SDR-Host 仓库的 `docs/timestamps.md`。

## 6. 多机同步

1. 各板共享 J3 的 10 MHz（`clock_ref_source=1`，ADF4002 把 40 MHz 锁到外参考，采样时钟同源）。
2. 各板 J6 接同一个 PPS，`time_source=external`。
3. 设置相同采样率后，各板执行 `time_next_pps=0`，在同一个 PPS 沿把时间置 0。
4. 之后用相同的时间戳下发定时 RX/TX 命令。

预期的样点对齐为 ±1 个样点。限制：

- 未实现 AD9361 多芯片同步（MCS），各板 LO 相位随机，需要相位相干的应用要在主机侧标定。
- 外部 PPS 装载只在仿真中验证，尚未在多块板上实测。

## 7. 实测

在一块 OmniCore SDR 上（Windows 11，USB3），位流和程序经 JTAG 加载到 RAM：

- 内部 PPS 间隔恰好等于采样率（61.44、30.72、7.68 MS/s 下验证）；下一个 PPS 沿装载结果精确。
- 定时 RX 首样点时间等于请求时间，样点数准确，并带 EOB；迟到命令带 LATE。
- AD9361 数据口回环（`bist_loopback=1`）下，TX→RX 延迟在同一次加载内对每个突发都相同（10 或 11 个样点，随负载而定）；迟到突发不发射并上报事件。

## 8. 实现要点

- **DAC 取数时序**：`axi_ad9361` 实际在导出的 `dac_valid` 之前约 2 个时钟就锁存了数据。若在 `dac_valid` 时才弹出样点，突发开头会丢 1 帧。
  因此 DAC 侧在 `T-2` 先预备（arm），测试平台也按这个提前量建模。
- **TX 块头标签提前入队**：块头在写突发第 5 个字时即进入标签 FIFO，不等整块写完。单通道 TX 的 FIFO 只有约 1.5 块，
  否则 1T2R 高负载下启动瞬间会欠流。
- **RX 溢出迟滞**：溢出后等 FIFO 降到不足一块才恢复，避免补零块不断占用刚释放的空间。
- **时序收敛**：245.76 MHz 域中，BRAM 写使能前加了 skid 寄存器，比较和事件请求均为寄存器输出。最终 WNS +0.156 ns。
- **USB 尾部**：CH32 桥在最后一块后不发短包，主机在定时 RX 下必须使用 16 KiB 的 USB 传输。

## 9. 工程与文件

- **IP 源码**（`ip_repo/user/CH32H417_IP/ip_repo/CH32H417_IP_1.0/`）：
  - `src/oc_timekeeper.v`：时间计数器与 PPS
  - `src/oc_afifo.v`：小型异步 FIFO（分布式 RAM，Gray 指针，首字直通）
  - `src/oc_hold_xfer.v`：跨时钟域保持握手
  - `src/CH32H417_Fsm.v`：成帧 RX/TX、定时与事件
  - `hdl/CH32H417_IP_v1_0_S00_AXI.v`：寄存器
- **仿真**：`sim/run_sim.sh`（Vivado xsim），自检测试平台 `sim/tb_ch32h417_time.sv`，覆盖 A 时间设置、B 外部 PPS、C 内部 PPS、
  D 成帧 RX 与溢出 GAP、E 定时 RX、F 单通道与 IQ8、G 定时 TX、H 只有成帧模式。
- **IP 重打包**：`scripts/package_ip.tcl`。
- **主工程 Block Design**：`scripts/build_time_base_bitstream.tcl` 依次完成：
  1. 把 `CH32H417_IP_0` 升级到 1.3，AXI 地址宽度设为 7；
  2. 连接 `gps_pps_0 → pps_ext`；
  3. 新建端口 `AD9361_CLK_OUT` 并连接到 `ref_clk_in`；
  4. 生成位流并导出 hdf，同时输出时序、资源和 CDC 报告。

  XDC 增加了 U10、40 MHz 时钟约束和跨时钟域约束。构建与调试步骤见 [build.md](build.md)。
- **SDK**：
  - `src/console_commands/sdr_time.c`：时间与定时 RX 命令、事件上报、`bist_loopback=` 测试回环
  - BSP 驱动 `CH32H417_IP.c/.h`

## 10. 版本与兼容性

1.3 起只保留成帧传输（2026-11-07）：删除了 16 KiB 递增起始头、旧的 RX_EN 重启式重同步、TX 预填门槛和 4096÷3 相位拼接。
SDK 不再有 `usb_framing=` 命令；STATUS0 的错误位 0（ADC 溢出）和 3（DAC 欠流）恒为 0，改由带时间的事件上报。
SDK 在 PL 缺少成帧能力（STATUS0.bit30=0）时拒绝启动 USB 数据流（`fpga_without_framed_transport`）。
需要 OCH 0.6.0 及以上；旧版上位机（0.5.x 及以前，包括依赖起始头的 SDR++ 旧插件）不能再用于收发数据。

1.2 是开发中的过渡版本（成帧与旧模式可切换），没有单独发布。

## 11. 吞吐对比（Windows，USB3，2026-11-07）

`och bench` 5 秒、零错误（无主机丢样、USB 错误、设备溢出/欠流、事件丢失）的最高采样率，MS/s 为每通道，搜索档位 61.44/56/50/46/42 MS/s。
“旧模式”与“1.2 成帧”在删除旧模式前的同一位流上测量；“1.3”为删除旧模式并将 TX 块头提前入队后的最终版本。

| 配置 | 旧模式 | 1.2 成帧 | 1.3（最终） | 1.3 吞吐 RX / TX（MB/s） |
|---|---:|---:|---:|---|
| 1RX | 61.44 | 61.44 | 61.44 | 184.1 / — |
| 2RX | 61.44 | 61.44 | 61.44 | 368.5 / — |
| 1T1R | 61.44 | 失败¹ | 61.44 | 183.9 / 183.8 |
| 2T1R | 56 | 56 | 56 | 167.6 / 335.1 |
| 1T2R | 56 | 失败¹ | 56 | 335.2 / 167.6 |
| 2T2R 12bit | 42 | 42 | 42 | 251.4 / 251.4 |
| 2T2R 8bit | 61.44 | 失败¹ | 61.44 | 245.2 / 245.1 |

¹ 1.2 时 OCH 的单通道/IQ8 TX 启动门槛（整块 1364/2046 组）因 DAC 预取缓存已取走 1 组而永远达不到，已修复。

结论：成帧对带宽没有可测影响。每块头开销 16/16384 = 0.098%；各配置上限由 UHSIF 总线（32 位 × 133.33 MHz ≈ 533 MB/s，RX 与 TX 共享）决定，而不是块头。
长时间测试：2T2R 8bit 61.44 MS/s 60 秒、2RX 61.44 MS/s 60 秒均零错误；1T2R 56 MS/s 连续 5 次 10 秒均零错误。
OmniCore-SDR-Host 仓库的 `tools/bench_sweep.py` 可复现这张表。
