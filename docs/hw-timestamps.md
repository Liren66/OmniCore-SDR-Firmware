# 硬件时间戳与成帧收发（CH32H417_IP 1.3）

本功能参照 USRP B2x0 的 vita_time 设计：FPGA 内有一个 64 位样点计数器，RX 数据带时间戳，RX/TX 可以按样点精确定时。

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
  | `internal` | AD9361 `CLK_OUT` → FPGA U10，FPGA 将 40 MHz 分频得到 1 PPS |

  `internal` 需要 SDK 把 `clk_output_mode_select` 设为 `BUFFERED_XTALN_DCXO`（已改）。接入 10 MHz 后，ADF4002 会驯服该 40 MHz。
- **失效条件**：采样率变化、`rf_reinit!` 或 PL 复位后，时间被置为无效。SDK 每 250 ms 检查一次采样率，变化时会上报 `EVT type=time_invalidated`。

## 2. 成帧格式（唯一的 USB 传输格式）

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

TX 中 `valid=0` 的块会被静默丢弃。

- **RX 溢出**：FPGA 把当前块补零后结束，等主机把 FIFO 读到不足一块时再恢复。下一块置 GAP 标志，丢失的样点数等于两块时间戳之差，是精确值。这种情况下不再需要 16 KiB 递增起始头和 RX 重启。
- **定时 RX**（`rx_timed=1`）：开启 RX 后 FPGA 等待命令。
  - `rx_stream=<ticks>|now <nsamp>`：在时间到达的那个样点开始接收，`nsamp=0` 表示连续接收。
  - 命令到达时如果时间已过：立即开始，并置 LATE 标志、产生 `rx_late_command` 事件。
  - `rx_stream_stop!`：以 EOB 块结束。
- **定时 TX**：
  - 带 HAS_TIME 的块在 `time == T` 的那个 DAC 样点开始播放。
  - 一个突发（burst）内的后续块无缝衔接，EOB 块之后回到空闲。
  - 迟到的块（`tx_late`）及其所在的整个突发会被丢弃。
  - 突发中途缺数据报 `tx_underflow`，突发结束报 `tx_burst_ack`。
- **事件上报**：事件都带样点时间，格式为 `EVT tseq=… type=… src=fpga_time code=… block=… time=…`。

## 3. AXI 寄存器（基址 0x43C10000，32 个）

| 偏移 | 名称 | 说明 |
|---|---|---|
| 0x00/0x04/0x08/0x0C | CTRL0/CTRL1/STATUS0/STATUS1 | 原有寄存器。CTRL0.bit8 保留（1.2 的 FRAMED，写入被忽略），bit9 RX_TIMED；STATUS0.bit31 成帧（恒为1），bit30 时间能力位，bit11 TX 运行中；STATUS1.bit31 RX_TIMED 已生效 |
| 0x10/0x14 | TIME_SET_LO/HI | 待装载的时间 |
| 0x18 | TIME_CTRL | 写触发：[1:0] 1=立即，2=下一个 PPS，3=作废；[5:4] PPS 源；读 bit31 = busy |
| 0x1C | TIME_STATUS | bit0 armed，bit1 valid，[3:2] 源，bit4 ref40 存在，bit5 射频时钟停止 |
| 0x20/0x24 | TIME_NOW | 先读 LO，此时 HI 被锁存 |
| 0x28/0x2C | TIME_PPS | 上一个 PPS 的时间，先读 LO |
| 0x30/0x34/0x38/0x3C | RX_CMD_TIME/NSAMP/RX_CMD | 写 0x3C 触发：bit0 立即，bit1 停止 |
| 0x40..0x4C | EVT_STATUS/INFO/TIME | 读 0x40.bit0 表示有事件，写 0x40=1 弹出一个事件 |
| 0x50 | PPS_COUNT | PPS 计数 |
| 0x54 | VERSION | 0x01030001 |
| 0x58 | FRAME_INFO | 头 16 字节 / 载荷 16368 字节 |

## 4. 工程与文件

- **IP 源码**（`ip_repo/user/CH32H417_IP/ip_repo/CH32H417_IP_1.0/`）：
  - `src/oc_timekeeper.v`：时间计数器与 PPS
  - `src/oc_afifo.v`：小型异步 FIFO
  - `src/oc_hold_xfer.v`：跨时钟域保持握手
  - `src/CH32H417_Fsm.v`：成帧 RX/TX 与事件
  - `hdl/CH32H417_IP_v1_0_S00_AXI.v`：寄存器
- **仿真**：`sim/run_sim.sh`（Vivado xsim），自检测试平台为 `sim/tb_ch32h417_time.sv`。
- **IP 重打包**：`scripts/package_ip_1_2.tcl`。
- **主工程 Block Design**：由 `scripts/build_time_base_bitstream.tcl` 完成以下步骤：
  1. 把 `CH32H417_IP_0` 升级到 1.2；
  2. 连接 `gps_pps_0 → pps_ext`；
  3. 新建端口 `AD9361_CLK_OUT` 并连接到 `ref_clk_in`；
  4. 编译生成位流并导出 hdf。

  XDC 增加了 U10、40 MHz 时钟约束和跨时钟域约束。
- **SDK**：
  - `src/console_commands/sdr_time.c`：新命令，以及 `bist_loopback=` 测试回环
  - 驱动 `CH32H417_IP.c/.h`：TimeCommand、GetTime、RxCommand、PopEvent

## 5. 版本与兼容性

1.3 起只保留成帧传输（2026-11-07）：删除了 16 KiB 递增起始头、旧的 RX_EN 重启式重同步、TX 预填门槛和 4096÷3 相位拼接。SDK 不再有 `usb_framing=` 命令；STATUS0 的错误位 0（ADC 溢出）和 3（DAC 欠流）恒为 0，改由带时间的事件上报。SDK 在 PL 缺少成帧能力（STATUS0.bit30=0）时拒绝启动 USB 数据流（`fpga_without_framed_transport`）。需要 OCH 0.6.0 及以上；旧版上位机（0.5.x 及以前，包括依赖起始头的 SDR++ 旧插件）不能再用于收发数据。

## 6. 吞吐对比（Windows，USB3，2026-11-07）

`och bench` 5 秒、零错误（无主机丢样、USB 错误、设备溢出/欠流、事件丢失）的最高采样率，MS/s 为每通道，搜索档位 61.44/56/50/46/42 MS/s。“旧模式”与“1.2 成帧”在删除旧模式前的同一位流上测量；“1.3”为删除旧模式并将 TX 块头提前入队后的最终版本。

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

结论：成帧对带宽没有可测影响。每块头开销 16/16384 = 0.098%；各配置上限由 UHSIF 总线（32 位 × 133.33 MHz ≈ 533 MB/s，RX 与 TX 共享）决定，而不是块头。1.3 中 TX 块头在第 5 个字就入队（不等整块写完），避免了单通道 TX（FIFO 仅约 1.5 块）在 1T2R 高负载下启动瞬间的一次欠流；该配置 56 MS/s 连续 5 次 10 秒均零错误。长时间测试：2T2R 8bit 61.44 MS/s 60 秒、2RX 61.44 MS/s 60 秒均零错误。
