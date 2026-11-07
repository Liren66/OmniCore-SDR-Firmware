# SDK 命令参考（SDK_CMD_1.1）

Zynq no-OS 程序在 PL UART16550 上提供文本命令行。主机经 CH32 的 USB EP2 访问它（3.125 MBd，与 CH32 `UART1_BRIDGE_BAUD` 一致）。
一般不需要手工发命令：OmniCore Host（OCH）的 `och get/set/action` 和 C API 会按正确顺序调用。本文用于调试、移植和理解 OCH 的行为。

本文描述当前版本（v2026.11.07，`SDK_CMD_1.1_20261107_FRAMED`）。历次协议扩展的完整记录见
[`COMMAND_PROTOCOL_V1.txt`](../OmniCore-SDR-Firmware.sdk/COMMAND_PROTOCOL_V1.txt)，其中第 3、5、6、7 节的
16 KiB 起始头、RX 重同步和 TX 预填门槛已在 1.1 中删除。

## 1. 语法

- 命令小写，以 LF 结束；后缀 `?` 查询，`=` 设置，`!` 动作。参数用空格分隔。
- 一次只发一条，收到回复后再发下一条。控制台 RX 由 UART 中断搬入 2 KiB 环形缓冲，但大量连续灌入仍可能丢行。
- 成功时返回 `name=value ...` 形式的键值行；失败返回 `ERR <原因> code=<n>`，例如 `ERR rf_not_ready code=-1`。
- `help?` 列出全部命令、参数个数、范围和示例。

### 事务信封（主机使用）

在命令前加 `@<id> `（id 为非零 uint32），回复会被 `RSP` 行包住，便于主机把回复和异步事件分开：

```text
> @17 rx_samp_freq?
< RSP id=17 begin
< rx_samp_freq=30720000
< RSP id=17 end result=0
```

`host_protocol?` 返回 `host_protocol=1 envelope=@id completion=RSP events=EVT`。

### 异步事件

以 `EVT` 开头的行随时可能出现在两条回复之间，主机必须持续读取 EP2 并解析：

| 格式 | 来源 |
|---|---|
| `EVT tseq=<n> type=<name> src=fpga_time code=<n> block=<seq> time=<ticks> lost_before=<0\|1>` | FPGA 事件 FIFO，带样点时间 |
| `EVT tseq=<n> type=time_invalidated src=sdk old_rate=<Hz> new_rate=<Hz>` | 采样率改变，时间作废 |
| `EVT seq=<n> stream=<n> type=<name> mask=<m> src=fpga ... status=0x...` | 桥接 IP 的其他错误锁存（配置错误、时钟停止等） |

`src=fpga_time` 的事件类型：

| code | type | 含义 |
|---|---|---|
| 0x01 | `tx_burst_ack` | 突发正常播放完毕，`time` 为完成时的样点时间 |
| 0x02 | `tx_underflow` | 突发中途 FIFO 断流，`time` 为发生时刻 |
| 0x03 | `tx_late` | 突发的开始时间已过，整个突发被丢弃，`time` 为请求的开始时间 |
| 0x04 | `tx_seq_error` | TX 块序号不连续 |
| 0x05 | `tx_header_error` | TX 块头魔数或校验错误，块被丢弃 |
| 0x10 | `rx_overflow_gap` | RX 溢出，下一块带 GAP 标志 |
| 0x11 | `rx_late_command` | 定时 RX 命令到达时时间已过，已立即开始 |
| 0x12 | `rx_command_ignored` | RX 命令在当前状态下无效 |

`lost_before=1` 表示事件 FIFO（32 项）曾经溢出，在这一项之前丢过事件。

**过载合并**：`tx_underflow` 与 `rx_overflow_gap` 在过载时可能每块都触发（2T2R 12 位 50 MS/s 超过 UHSIF 约 533 MB/s 时 TX 欠流约 16000 次/秒）。逐条打印会占满 3.125 MBd 控制串口、阻塞命令回复并使事件 FIFO 溢出，因此 SDK 对这两类事件按类型合并：

- 一串事件的第一条照常立即输出；
- 之后每 250 ms 最多输出一条汇总行，在原格式后追加字段：
  `EVT tseq=<n> type=tx_underflow src=fpga_time code=2 block=<最后块> time=<最后时间> lost_before=<0|1> count=<N> first_block=<首块> first_time=<首时间>`
  `count` 为本行代表的事件数（不含此前单独输出的第一条），`block`/`time` 为窗口内最后一条，`lost_before=1` 表示窗口内任一条之前 FIFO 丢过事件；
- 一个 250 ms 窗口内没有新事件即视为该串结束，下一条事件再次立即输出。

因此过载时每种类型最多约 4 行/秒。其他类型（`tx_burst_ack`、`tx_late`、`tx_seq_error`、`tx_header_error`、`rx_late_command`、`rx_command_ignored`）不限速；输出它们之前先把待汇总的计数输出，保证行按时间顺序。没有 `count` 字段的行表示 1 个事件，旧主机（OCH 0.6.0）忽略新增字段，只会把每行当作一个事件计数。

## 2. 命令一览

### 射频（AD9361 原有命令）

| 命令 | 单位 / 取值 |
|---|---|
| `rx_lo_freq` / `tx_lo_freq` | MHz，可带小数；查询值为 PLL 实际可实现值 |
| `rx_samp_freq` / `tx_samp_freq` | Hz。修改前必须停 TX 和双向 USB |
| `rx_rf_bandwidth` / `tx_rf_bandwidth` | Hz |
| `rx1_gc_mode` / `rx2_gc_mode` | 增益控制模式 |
| `rx1_rf_gain` / `rx2_rf_gain` | dB |
| `tx1_attenuation` / `tx2_attenuation` | mdB，0..89750，步进 250 |
| `rx_fir_en` / `tx_fir_en`，`rx_fir_config` / `tx_fir_config` | FIR 开关；`=0` 恢复默认 64 抽头配置 |
| `dds_tx{1,2}_tone{1,2}_{freq,phase,scale}` | Hz / 度 / 百万分之一（250000 = 0.25） |
| `clock_chain?` | RX/TX 各六级时钟，Hz |
| `rx_dc_correction`、`rx_iq_correction` | 0 冻结 / 1 持续跟踪（默认 1） |
| `rx_lo_source` / `tx_lo_source` | 0 内部 / 1 外部（J11 / J10，输入频率为 2 × RF） |
| `rx_rssi?`、`rssi_enable` | 相对 RSSI，需先 `rssi_enable=1` |

分段上传自定义 FIR 和完整时钟链的事务命令（`rx_fir_begin=`、`fir_data=`、`fir_commit=`、`clock_begin=` 等）见 `help?` 的说明。

### 数据流

| 命令 | 说明 |
|---|---|
| `rx_channel?/=` | 0 关闭上传，1 RX1，2 RX2，3 双通道 |
| `tx_channel?/=` | 0 停止发射，1 TX1，2 TX2，3 双通道 |
| `tx_source?/=` | 0 DDS，1 USB；只选源，不启动 |
| `iq_format?/=` | 12（默认）或 8。8 位只用于 2T2R 且 TX 源为 USB |
| `usb_data_en?/= <rx> <tx>` | 打开或关闭 USB 上传 / 下载方向 |
| `tx_enable?/=` | 1 启动发射。USB 源时会等待 FPGA 报告 TX 已开始播放（STATUS0 bit11） |
| `usb_status?`、`usb_irq?` | 桥接 IP 状态、FIFO 水位、错误历史与中断诊断 |
| `usb_stop!` | 停止双向 USB；USB 源发射先静默 |
| `usb_clear!` | 请求清除错误锁存，不清计数和历史 |
| `usb_config?/=`、`usb_rx=`、`usb_tx=` | 底层调试入口，受与上面相同的状态检查保护；旧 `ch32_*` 名称为别名 |

### 时间基准与定时收发（1.1 新增）

| 命令 | 回复示例 | 说明 |
|---|---|---|
| `time_now?` | `time_now=123456 valid=1 tick_rate=30720000` | 当前样点时间 |
| `time_now=<ticks>` | `time_now=0 applied=now tick_rate=...` | 立即装载 |
| `time_next_pps=<ticks>` | `time_next_pps=0 armed=1 source=internal ...` | 下一个 PPS 沿装载 |
| `time_last_pps?` | `time_last_pps=... pps_count=... source=...` | 上一个 PPS 之后第一个样点的时间 |
| `time_source?/=` | `time_source=internal` | `none` / `external`（J6）/ `internal`（AD9361 40 MHz 分频） |
| `time_status?` | `time_status valid=1 armed=0 source=... version=0x01030001 ...` | 全部时间状态 |
| `time_invalidate!` | `time_invalidate=1` | 作废当前时间 |
| `rx_timed?/=` | `rx_timed=1` | 1：RX 打开后等待 `rx_stream=` 命令才开始 |
| `rx_stream=now\|<ticks> <nsamp>` | `rx_stream=1000000 nsamp=4096` | 定时开始，`nsamp=0` 为连续 |
| `rx_stream_stop!` | `rx_stream=stop` | 以 EOB 块结束当前接收 |
| `bist_loopback?/=0\|1` | `bist_loopback=1 path=ad9361_data_port_tx_to_rx` | AD9361 数据口 TX→RX 回环，仅用于测试 |

ticks 是十进制 64 位无符号整数，1 tick = 1 个样点。详见 [hw-timestamps.md](hw-timestamps.md)。

### 参考时钟、PPS 与板级

| 命令 | 说明 |
|---|---|
| `clock_ref_source?/=` | 0 内部 40 MHz 自由运行；1 ADF4002 用 J3 的 10 MHz 驯服 |
| `pps_input_en?/=`、`pps_status?` | PPS 频率监测；与 `clock_ref_source` 共同决定 VCXO 驯服模式 |
| `vcxo_status?` | CH32 PA4 PPS 驯服环路状态 |
| `adf4002_status?`、`adf4002_self_test!` | ADF4002 配置状态；自检让 D5 闪烁三次 |
| `rgb_enable`、`rgb_status?` | RGB 指示灯 |
| `fan_enable`、`fan_status?`、`core_temp?` | 风扇温控与 Zynq 温度 |
| `system_info?` | 固件版本、FPGA IP 版本、能力列表、IQ 与传输格式 |
| `system_status?` | 初始化、通道、USB、参考、PPS、故障记录 |
| `rf_reinit!` | 停流后重建 RF 与数字接口，恢复最后有效配置 |
| `digital_self_test!` | RX PRBS 与 TX 数字回环自检 |

`pps_status?` 测的是 DATA_CLK 频率，和硬件时间戳的 PPS 是两套独立功能：前者用于驯服晶振，后者用于对时。

## 3. `system_info?` 中与版本相关的字段

```text
firmware=SDK_CMD_1.1_20261107_FRAMED ... fpga=CH32H417_IP_0x01030001
time_capabilities hw_time=1 framing=1 ...
... transport=framed16k header_bytes=16 payload_bytes=16368 ...
overflow_policy=framed_gap_event
```

OCH 0.6.0 打开设备时检查 `transport=framed16k`，不满足则拒绝连接。PL 不具备成帧能力（STATUS0 bit30 = 0）时，
SDK 拒绝打开 USB 数据流并返回 `ERR fpga_without_framed_transport load_CH32H417_IP_1.3`。

## 4. 典型流程

主机必须先提交 EP1 读请求，再打开 USB 上传；先持续写入 TX 数据，再 `tx_enable=1`。

**双通道连续接收**

```text
tx_enable=0
usb_data_en=0 0
rx_channel=3
rx_samp_freq=30720000
usb_data_en=1 0          # 此前主机已提交 EP1 IN 请求
```

**定时接收 4096 个样点**

```text
time_source=internal
time_next_pps=0          # 下一个内部 PPS 沿置 0
rx_timed=1
usb_data_en=1 0
rx_stream=30720000 4096  # 在 t = 1 s 开始，收满后以 EOB 块结束
```

**USB 双通道发射**

```text
tx_enable=0
usb_data_en=0 0
tx_source=1
tx_channel=3
usb_data_en=0 1          # 主机持续写 EP1 OUT；带 HAS_TIME 的块在指定时刻开始
tx_enable=1
```

SDK 不再检查 TX 预填水位：`tx_enable=1` 后，FPGA 从 FIFO 中的第一块开始播放，定时突发则等到块头时间。
OCH 在发出 `tx_enable=1` 之前，会自行等待约一块数据进入 FIFO。

## 5. 修改约束

- 修改采样率、FIR、RX DC/IQ 校正、参考源、LO 源前，必须 `tx_enable=0` 并 `usb_data_en=0 0`，否则返回 `ERR busy ...`。
- 修改 RX 通道前先停上传；修改 TX 通道或数据源前先停 TX 和 USB TX。
- 修改采样率后硬件时间作废，SDK 在 250 ms 内上报 `time_invalidated`，需要重新设置时间。
- `iq_format=8` 要求 `rx_channel=3`、`tx_channel=3`、`tx_source=1`，切换前需停流。
