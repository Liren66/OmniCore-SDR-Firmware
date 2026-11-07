# 更新记录

发布版本以日期命名（`vYYYY.MM.DD`），附件见 [GitHub Releases](https://github.com/Liren66/OmniCore-SDR-Firmware/releases)。

## 版本对应关系

| 发布 | FPGA `CH32H417_IP` | SDK 标识 | CH32 桥接 | OmniCore Host |
|---|---|---|---|---|
| v2026.11.07.2 | 1.3（`0x01030001`）+ OC_SYNC_AXI 1.0 | `SDK_CMD_1.2_20261107_SYNC` | 1.0.0 | 0.6.0 及以上；多板同步 `och sync` 需 0.7.0 |
| v2026.11.07.1 | 1.3（`0x01030001`，位流不变） | `SDK_CMD_1.1_20261107_EVTCOUNT` | 1.0.0 | 0.6.0 及以上（0.6.1 读取事件 `count`） |
| v2026.11.07 | 1.3（`0x01030001`） | `SDK_CMD_1.1_20261107_FRAMED` | 1.0.0 | **0.6.0 及以上** |
| v2026.11.03 | 1.1 | `SDK_CMD_1.0_20261026_VCXO` | 1.0.0 | 0.5.x（USB 升级需 0.5.0+） |
| v2026.10.29 / .1 | 1.1 | `SDK_CMD_1.0_20261026_VCXO` | 无版本号、无 Bootloader | 0.5.x 及以前 |

`BOOT.bin` 与 OmniCore Host 必须按上表配套；CH32 桥接固件对数据内容透明，与 Zynq 固件版本无关。

## v2026.11.07.2：多板同步（SDK `SDK_CMD_1.2_20261107_SYNC`）

位流与 Zynq 应用都有更新，CH32 桥接固件不变；`CH32H417_IP` 仍为 1.3。配套 OmniCore Host 0.7.0（`och sync`）。

- **FPGA**：新增 IP `OC_SYNC_AXI` 1.0（`0x43C30000`）。REF_OUT（T9→J13）由 CLK_FB_FPGA（H17）四分频输出 10 MHz；TRIG（Y6→J9）支持命令、周期、在 PPS 时触发；AD9361_SYNC（U20）与 PPS_EXT 的沿计数和间隔测量；时间基准的 PPS 输入可在 PPS_EXT 和 AD9361_SYNC 之间切换。时序收敛：WNS +0.217 ns，WHS +0.036 ns。
- **SDK**：新增 `sync_status?`、`ref_out=`、`trig_*`、`sync_loopback=`、`sync_clear!`、`mcs_step=`、`mcs_run!`、`time_source=sync` 和 `EVT type=sync_in`。上电默认关闭输出，时间基准 PPS 仍取 PPS_EXT，原有行为不变。
- **已测**（单板、无外部参考）：IP 仿真通过；`och sync --loopback --no-mcs --monitor 1` 时间在 TRIG 沿装载，1 s 周期下 SYNC 间隔恒为 40000000，相邻沿的样点时间相差恰好 30720000；MCS 0–5 步与 `mcs_run!` 后，`hw_timestamp_test` 通过，bench 2T2R 44、2T1R 58、1T2R 58.5、2RX 61.44 MS/s 均无故障。
- **未测**：需要两块以上的板子才能验证 REF_OUT 波形与从板锁定、线缆上的 SYNC、MCS 的实际对齐效果，以及公共参考模式。

## v2026.11.07.1：过载事件合并

只更新 Zynq 应用（`BOOT.bin`），位流与 CH32 桥接固件不变。

- **修复**：TX/RX 过载（例如 2T2R 12 位 50 MS/s）时 `tx_underflow`/`rx_overflow_gap` 事件刷满控制串口，命令回复（包括停止命令）排队、事件 FIFO 溢出丢失 RX GAP 事件。SDK 现在合并这两类事件：第一条立即输出，之后每 250 ms 最多一条带 `count=` 的汇总行（见 [SDK 命令](sdk-commands.md#异步事件)）。停止某个方向时立即补报剩余汇总。行格式向后兼容 OCH 0.6.0；OCH 0.6.1 读取 `count`。仅 SDK 改动，位流不变。
- **吞吐实测更正**：以前的档位搜索（61.44/56/50/46/42）跳过了中间速率。按 0.5 MS/s 细扫并做 300 秒长测后，2T2R 12 bit 最高 **44 MS/s**（原记 42），1T2R 最高 **58.5 MS/s**、2T1R 最高 **58 MS/s**（原记均为 56）；上限仍是 UHSIF 总线约 532 MB/s（RX + TX）。

## v2026.11.07：成帧 USB 传输与硬件时间戳

Zynq 固件更新，CH32 桥接固件不变。**不兼容旧上位机。**

- **新增**
  - 64 位样点时间基准，1 tick = 1 个样点；可立即设置或在下一个 PPS 沿装载。
  - PPS 源：外部（J6 `PPS_EXT`）或内部（AD9361 `CLK_OUT` 40 MHz 经 FPGA 分频）。为此开启 AD9361 `CLK_OUT`，FPGA 新增 U10 输入。
  - 成帧传输：每个 16 KiB 块 = 16 字节头 + 16368 字节载荷，块内整数个样点，无相位拼接。
  - RX：时间戳、定时开始、指定样点数、停止命令；溢出时补零并在下一块标 GAP，丢样数精确可知。
  - TX：按块头时间在指定样点开始突发；迟到突发丢弃；突发完成、迟到、欠流均以带时间的事件上报。
  - SDK 命令：`time_*`、`rx_timed=`、`rx_stream=`、`rx_stream_stop!`、`bist_loopback=`；`system_info?` 给出 FPGA IP 版本。
  - IP 版本寄存器（`0x54`），自检仿真测试平台，位流构建与 JTAG 加载脚本。
- **删除**
  - 16 KiB 递增起始头、溢出后关闭再打开 RX_EN 的重同步、TX 预填门槛、`usb_framing=` 命令。
  - STATUS0 的 ADC 溢出、DAC 欠流错误位（恒为 0，改由事件上报）。
- **吞吐**：与旧模式相同，详见 [hw-timestamps.md](hw-timestamps.md#11-吞吐对比windowsusb32026-11-07)。

## v2026.11.03：CH32 桥接 1.0.0 与 USB 升级

- CH32 桥接固件 1.0.0：版本号、V5F Bootloader、`.ocfw` 升级包，可用 `och firmware update` 通过 USB 升级。
- 修复：Bootloader 擦写 Flash 时，固件信息查询在 USB 中断中读 Flash 导致控制传输失败；改为从 RAM 缓存应答。
- 加入 GPL-3.0 许可证与第三方许可说明。
- README 增加逐步的 CH32 升级说明。

## v2026.10.29.1

- 加入 FT2232 JTAG 接口的 EEPROM 配置工具。
- 更正 `Merge.bin` 的发布命名。

## v2026.10.29：统一仓库

- 原 OmniCore-SDR-Bridge 工程并入本仓库的 `OmniCore-SDR-Bridge/`，Zynq 与 CH32 固件统一发布。

## 发布前的主要提交（2026-10-26 / 27）

- Vivado / SDK 2018.3 工程初始版本：AD9361 2R2T LVDS、CH32H417 UHSIF 桥、IQ8 双通道模式。
- 提交 BD 的 IP `.xci` 与 `system.bxml`，修复 clone 后 BD 41-967 警告。
- PPS 驯服晶振模式（`clock_ref_source` + 2 × `pps_input_en`）与 CH32 PA4 握手；控制台 UART 改为中断接收。
