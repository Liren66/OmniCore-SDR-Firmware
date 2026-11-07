# 更新记录

发布版本以日期命名（`vYYYY.MM.DD`），附件见 [GitHub Releases](https://github.com/Liren66/OmniCore-SDR-Firmware/releases)。

## 版本对应关系

| 发布 | FPGA `CH32H417_IP` | SDK 标识 | CH32 桥接 | OmniCore Host |
|---|---|---|---|---|
| v2026.11.07 | 1.3（`0x01030001`） | `SDK_CMD_1.1_20261107_FRAMED` | 1.0.0 | **0.6.0 及以上** |
| v2026.11.03 | 1.1 | `SDK_CMD_1.0_20261026_VCXO` | 1.0.0 | 0.5.x（USB 升级需 0.5.0+） |
| v2026.10.29 / .1 | 1.1 | `SDK_CMD_1.0_20261026_VCXO` | 无版本号、无 Bootloader | 0.5.x 及以前 |

`BOOT.bin` 与 OmniCore Host 必须按上表配套；CH32 桥接固件对数据内容透明，与 Zynq 固件版本无关。

## 未发布

- **修复**：TX/RX 过载（例如 2T2R 12 位 50 MS/s）时 `tx_underflow`/`rx_overflow_gap` 事件刷满控制串口，命令回复（包括停止命令）排队、事件 FIFO 溢出丢失 RX GAP 事件。SDK 现在合并这两类事件：第一条立即输出，之后每 250 ms 最多一条带 `count=` 的汇总行（见 [SDK 命令](sdk-commands.md#异步事件)）。行格式向后兼容 OCH 0.6.0。仅 SDK 改动，位流不变。

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
