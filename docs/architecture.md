# 系统架构

OmniCore SDR 由三颗主要芯片组成：AD9361 射频收发器、Zynq-7020（`xc7z020clg400-2`）和 CH32H417 USB 3.0 桥。
本仓库包含 Zynq 的 PL 逻辑、PS 裸机程序，以及 CH32H417 的桥接固件。

```text
                     +------------------------- Zynq-7020 --------------------------+
+-----------+  LVDS  | PL                                                           |
|  AD9361   |<======>| axi_ad9361 --adc/dac (l_clk)--> CH32H417_IP 1.3              |
|           |  SPI   |                                  - CH32H417_Fsm (framing)    |   UHSIF 32-bit
|  CLK_OUT  |------->| U10 -> ref_clk_in -------------> - oc_timekeeper (time base) |<===============+
+-----^-----+ 40 MHz | Y7  -> pps_ext (J6 PPS_EXT) ---> - AXI-Lite regs (32)        |  133.33 MHz    |
      |              |                                                              |                |
+-----+-----+        | axi_uart16550 <-------------------------------------------+  |                |
|  ADF4002  |<- J3   |                                                           |  |                |
|  40 MHz   | 10 MHz | PS (Cortex-A9, no-OS): AD9361 driver, commands, events    |  |                |
|  VCTCXO   |        +-----------------------------------------------------------+--+                |
+-----------+                                          UART 3.125 MBd (G19/H18)  |                   |
                                                                       +---------+-------------------+--+
                                                                       | CH32H417 (V3F + V5F)           |
                                                                       |  USART1 <-> EP2  UHSIF <-> EP1 |
                                                                       +---------------+----------------+
                                                                                       | USB 3.0 (1a86:5537)
                                                                                 Host (OmniCore Host / OCH)
```

## 数据通路

| 方向 | 路径 |
|---|---|
| RX | AD9361 → `axi_ad9361`（`adc_data`/`adc_valid`，`l_clk` 域）→ `CH32H417_Fsm` 打包成 96 位组、写入块头 → FIFO Generator 异步 FIFO → UHSIF 读突发 → CH32H417 → USB EP1 IN |
| TX | USB EP1 OUT → CH32H417 → UHSIF 写突发 → `CH32H417_Fsm` 校验块头、取出时间标签 → DAC FIFO → 按样点时间播放 → `axi_ad9361` → AD9361 |
| 控制 | USB EP2 ⇄ CH32 USART1 ⇄ PL `axi_uart16550`（3.125 MBd）⇄ PS 控制台；PS 经 SPI 配置 AD9361，经 AXI-Lite 配置 `CH32H417_IP` |
| 诊断 | CH32 EP3 IN 输出桥接日志；EP0 厂商请求用于诊断与固件升级 |

USB 上的 IQ 数据统一按 16 KiB 块传输，每块 16 字节头 + 16368 字节载荷，格式见 [hw-timestamps.md](hw-timestamps.md)。
控制命令走 EP2 文本协议，与 IQ 数据完全分离，见 [sdk-commands.md](sdk-commands.md)。

## 时钟

| 时钟 | 频率 | 来源 | 用途 |
|---|---|---|---|
| `l_clk`（`rx_clk`） | DATA_CLK，2R2T LVDS 下为 4 × 采样率，最高 245.76 MHz | AD9361 `DATA_CLK` | `axi_ad9361` 接口、RX/TX 成帧逻辑、时间计数器 |
| `CH32H417_CLK` | 133.33 MHz | CH32H417 UHSIF | UHSIF 读写、FIFO 读写侧 |
| `ad9361_ref_clk` | 40 MHz | AD9361 `CLK_OUT` → U10 | 内部 PPS 分频（`time_source=internal`） |
| PS FCLK | — | Zynq PS | AXI-Lite 寄存器、UART |

板载 40 MHz VCTCXO 同时作为 AD9361 参考。它可以由 ADF4002 用 J3 的外部 10 MHz 驯服（`clock_ref_source=1`），
也可以由 CH32 PA4 DAC 按 PPS 闭环调节（`pps_input_en=1`）。注意板上 J6 丝印为 `10M_EXT`，实际网络是 `PPS_EXT`；真正的 10 MHz 输入是 J3。

跨时钟域：IQ 数据走 FIFO Generator 的异步 FIFO；块头标签和事件走 `oc_afifo`（Gray 指针）；
时间设置、RX 命令、时间快照走 `oc_hold_xfer`（toggle 握手）。约束中对这些路径设置了 datapath-only 最大延迟。

## UHSIF 带宽

UHSIF 为 32 位 × 133.33 MHz ≈ 533 MB/s，RX 与 TX 共享。读突发占 4101 个时钟（取第 2..4097 个时钟的数据），写突发 4096 个时钟。
它决定了各配置的最高采样率：

| 配置 | 每样点字节（RX + TX） | 最高采样率 | 总线占用 |
|---|---:|---:|---:|
| 1RX / 2RX | 3 / 6 | 61.44 MS/s | 184 / 369 MB/s |
| 1T1R | 3 + 3 | 61.44 MS/s | 369 MB/s |
| 2T1R / 1T2R | 6 + 3 | 56 MS/s | 504 MB/s |
| 2T2R 12bit | 6 + 6 | 42 MS/s | 504 MB/s |
| 2T2R IQ8 | 4 + 4 | 61.44 MS/s | 492 MB/s |

AD9361 本身最高 61.44 MS/s。块头只占 16/16384 ≈ 0.1%。

## AXI 地址

| 外设 | 基址 | 说明 |
|---|---|---|
| `axi_ad9361` | `0x79020000`（TX 为 `+0x4000`） | ADI 接口核 |
| `axi_uart16550` | `0x43C00000` | 连接 CH32 USART1 的控制台 |
| `CH32H417_IP` | `0x43C10000` | UHSIF 桥、时间基准，寄存器见 [hw-timestamps.md](hw-timestamps.md#4-axi-寄存器) |
| `BOARD_CTRL_AXI` | `0x43C20000` | RGB 指示灯（WS2812）与风扇 PWM，见 [IP 说明](../ip_repo/user/CH32H417_IP/ip_repo/BOARD_CTRL_AXI_1.0/README.md) |

## 中断

| 源 | PL→PS | GIC ID |
|---|---|---|
| `CH32H417_IP` 错误中断 | `IRQ_F2P[15]` | 91 |
| UART16550 接收 | `IRQ_F2P[14]` | 90 |
| PPS 监测 | `IRQ_F2P[0]` | 61 |

BSP 自动生成的这几个中断宏与本设计的实际连线不一致，代码中使用 `ch32h417_irq.h` 等文件里按布线确认过的编号。

## 关键 FPGA 引脚

| 信号 | 引脚 | 说明 |
|---|---|---|
| `AD9361_CLK_OUT` | U10 | 40 MHz 参考输出，LVCMOS25 |
| `gps_pps_0` | Y7 | J6 `PPS_EXT` |
| `UART_RX` / `UART_TX` | G19 / H18 | 连接 CH32 USART1 |
| `CH32H417_CLK_0` | H16 | UHSIF 时钟 |
| `CH32H417_DATA_0[31:0]` | 见 XDC | UHSIF 数据 |
| `rgb_data_0` / `fan_pwm_0` | E19 / D19 | `BOARD_CTRL_AXI` 输出 |
| `rx_clk_in_p/n` | N20 / P20 | AD9361 DATA_CLK |

完整约束见 `OmniCore-SDR-Firmware.srcs/constrs_1/new/constrain.xdc`；原理图为工作区中的 `AD9361_MIX(3).pdf`（不在本仓库）。

## CH32H417 桥接

V3F 核运行 stage-0，决定 V5F 启动应用还是 Bootloader；V5F 核运行 USB 协议栈并在 UHSIF 与 EP1 之间搬运 16 KiB 块。
桥接固件对数据内容透明，不解析块头。Flash 布局、Bootloader 和 USB 升级协议见 [Bridge README](../OmniCore-SDR-Bridge/README.md)。

注意：桥接固件在最后一个 16 KiB 块之后不发送短包。主机若提交大于 16 KiB 的 USB 读请求，有限长度突发（定时 RX）的尾部会一直挂在未完成的请求里，
因此 OCH 在定时 RX 下使用 16 KiB 的 USB 传输。
