# BOARD_CTRL_AXI：WS2812 RGB 与风扇 PWM

适用于 Vivado 2018.3、XC7Z020 的独立 AXI4-Lite IP。已经封装，可直接添加到 IP Catalog；不需要先运行打包脚本。没有修改现有 CH32H417 IP 或 AD9361 主工程。

## 实现

- 一个 32 位 AXI4-Lite 从接口，4 KB 地址空间，4 个寄存器。
- 一个时钟域，计数器产生时序，不产生新的逻辑时钟。
- 一个 WS2812C-2020 RGB 灯：24 位颜色，硬件自动循环刷新，软件只写颜色。
- 一路高有效风扇 PWM：周期、高电平时间均以 AXI 时钟周期为单位。
- 无中断、DMA、FIFO、BRAM 或 DSP；复位后颜色寄存器为黑色，PWM 输出为低。
- 默认时钟 100 MHz，与当前主工程 FCLK_CLK0 配置一致；默认 PWM 周期 4000，即 25 kHz，初始高电平时间为 0。

IP 的 VLNV 为 `user.org:user:BOARD_CTRL_AXI:1.0`，显示名称为 **Board RGB and Fan AXI-Lite**，RTL 顶层为 `board_ctrl_axi`。

## 加入你的主工程

1. 在 Vivado **Settings → IP → Repository** 添加或刷新 `CH32H417_IP/ip_repo`。
2. 在 Block Design 的 Add IP 中搜索 `BOARD_CTRL_AXI`，添加 **Board RGB and Fan AXI-Lite**。
3. `S_AXI` 接 PS 的 AXI GP0 外设互连；可使用 Connection Automation。
4. `s_axi_aclk` 接该 AXI 互连的 100 MHz 时钟，例如 `sys_ps7/FCLK_CLK0`。
5. `s_axi_aresetn` 接同一时钟域的 `sys_rstgen/peripheral_aresetn`。这是低有效、同步采样复位。
6. 双击 IP，确认 `CLK_FREQ_HZ=100000000`。此值用于时序常量计算，必须等于实际输入时钟；不会改变物理时钟频率。
7. 将 `rgb_data` 和 `fan_pwm` 引出为外部端口，分别重命名为 `PL_RGB` 和 `PL_FAN_CONTRL`。
8. 在 Address Editor 分配一个空闲的 4 KB 地址段，记录基地址。
9. 将 `constraints/board_pins_example.xdc` 中的约束加入主工程，然后 Validate Design、生成输出、生成 bitstream 并导出到 SDK。
10. 把 `software/board_ctrl.h` 加入应用的头文件搜索路径，用分配的基地址调用示例。它是头文件形式的 MMIO 辅助函数，不依赖安装额外的 BSP 驱动。

主工程使用其他时钟时，可以设置 `CLK_FREQ_HZ` 为 50 MHz 到 200 MHz。该范围表示计数器参数和协议波形的支持范围，最高可用时钟仍以集成后的时序报告为准。已做物理时序检查的配置是 100 MHz。

## 管脚

已放大核对 `AD9361_MIX(3).pdf` 第 1 页，供电见第 3 页：

| 外部端口 | IP 输出 | FPGA 管脚 | 电平 | 原理图连接 |
|---|---|---|---|---|
| `PL_RGB` | `rgb_data` | **E19** | LVCMOS33 | D4 的 DI，第 3 脚 |
| `PL_FAN_CONTRL` | `fan_pwm` | **D19** | LVCMOS33 | J8 的第 2 脚，R14 上拉至 3.3 V |

两者均在 Bank35。风扇是 **D19**。约束示例独立提供，没有随 IP 自动加载，便于你控制主工程的顶层端口命名和物理绑定。

```tcl
set_property PACKAGE_PIN E19 [get_ports PL_RGB]
set_property IOSTANDARD LVCMOS33 [get_ports PL_RGB]
set_property PACKAGE_PIN D19 [get_ports PL_FAN_CONTRL]
set_property IOSTANDARD LVCMOS33 [get_ports PL_FAN_CONTRL]
```

两个输出都是寄存器驱动的 3.3 V 推挽控制信号；风扇的 5 V 电源仍来自原理图的 J8 第 1 脚。PWM 占空比表示控制脚高电平比例，实际转速和停转行为由风扇决定。

## 寄存器

以下为相对于 IP 基地址的字节偏移。使用 32 位对齐访问。

| 偏移 | 名称 | 访问 | 复位值（100 MHz） | 含义 |
|---|---|---|---|---|
| `0x00` | RGB | RW | `0x00000000` | `[23:16]` 红、`[15:8]` 绿、`[7:0]` 蓝；`[31:24]` 读零、写忽略 |
| `0x04` | FAN_PERIOD | RW | `4000` | 每个 PWM 周期包含的时钟数 |
| `0x08` | FAN_HIGH | RW | `0` | 每个 PWM 周期的高电平时钟数 |
| `0x0C` | CLOCK_HZ | RO | `100000000` | IP 参数配置的时钟频率，不是实测频率 |

关系如下：

```text
有效周期 P = max(FAN_PERIOD, 1)
PWM 频率 = CLK_FREQ_HZ / P
高电平占空比 = min(FAN_HIGH, P) / P
```

- `FAN_HIGH=0`：持续低电平。
- `FAN_HIGH>=P`：持续高电平，支持精确 100%。
- `FAN_PERIOD=0`：硬件按 1 处理，避免下溢；寄存器读回仍为写入的 0。
- 每个周期开始时锁存周期和高电平时间，周期中途写入不会截断正在输出的脉冲。读回值是软件请求值，可能尚未作用于当前周期。
- 两个 PWM 寄存器分别写入，不提供成对原子提交；同时改变频率和占空比时，边界附近可能出现一个使用中间配置的周期。只调速时调用 `BoardCtrl_SetFanDuty()`，保持周期不变。
- 更新最长等待当前旧周期结束；设置非常大的周期会相应延长等待时间。
- 支持 `WSTRB` 字节写；对合法 RW 地址，`WSTRB=0` 不修改数据。
- 写只读寄存器、未映射地址或未对齐地址返回 `SLVERR`。未映射读返回零和 `SLVERR`。不支持 burst、IRQ 或读写保护区分；`AWPROT/ARPROT` 被忽略。
- AW/W 可以同时到达，也可任意先后到达；读写响应在主机反压期间保持稳定。单次读与同周期提交的同地址写可能读到旧值；需要写后读确认时，先完成写响应。

## C 使用例子

```c
#include "board_ctrl.h"

/* 用 Address Editor / 导出硬件中实际分配的地址替换。
 * 下面的地址只是示例，IP 不会固定占用它。
 */
#define BOARD_CTRL_BASE ((UINTPTR)0x43C20000U)

void board_lights_and_fan_init(void)
{
    BoardCtrl_SetRgb(BOARD_CTRL_BASE, 16, 0, 0);      /* 低亮度红色 */
    BoardCtrl_SetFan(BOARD_CTRL_BASE, 25000, 500);   /* 25 kHz，50% */
}

void example_update(void)
{
    BoardCtrl_SetRgb(BOARD_CTRL_BASE, 0, 16, 0);     /* 低亮度绿色 */
    BoardCtrl_SetFanDuty(BOARD_CTRL_BASE, 800);      /* 80% */
}
```

也可以直接读写寄存器。100 MHz 时，以下两次写入设置 25 kHz、50%：

```c
Xil_Out32(BOARD_CTRL_BASE + 0x04U, 4000U);
Xil_Out32(BOARD_CTRL_BASE + 0x08U, 2000U);
```

## WS2812 时序

按原理图的 **单颗 WS2812C-2020** 实现。软件用 `0x00RRGGBB`，硬件自动转为 GRB、每个字节高位先发；不要在软件中再交换 R/G。

| 参数 | 100 MHz 实际输出 |
|---|---|
| 单 bit 周期 | 1.25 us，即 125 个时钟 |
| 0 的高 / 低时间 | 300 ns / 950 ns |
| 1 的高 / 低时间 | 650 ns / 600 ns |
| 帧间复位低电平 | 至少 300 us |
| 单次颜色数据 | 24 bit，30 us |
| 连续刷新周期 | 约 330 us |

时序参考 [Worldsemi WS2812C-2020 数据手册，第 3–4 页](https://cdn.sparkfun.com/assets/e/1/0/f/b/WS2812C-2020_V1.2_EN_19112716191654.pdf)。计数阈值均为编译期常量。

颜色在每帧起点锁存，正在发送时修改 RGB 不会混合两种颜色；新值在下一帧发送，不需要 START 位、busy 轮询或中断。连续快速写入时，以帧起点看到的最后一次写入为准，不保证每个中间颜色都显示。需要等待显示更新时，可在软件写入后留出约 1 ms。

复位会将 RGB 寄存器恢复为零，并把数据脚拉低；解除复位后自动发送黑色帧。WS2812 自己保存已锁存的颜色，所以仅把数据脚拉低不等同于立即熄灯。

## 文件与验证

| 文件 | 用途 |
|---|---|
| `component.xml`、`xgui/` | 可直接添加的 Vivado IP 封装 |
| `hdl/board_ctrl_axi.v` | AXI4-Lite 寄存器和顶层 |
| `hdl/board_ctrl_ws2812.v` | RGB 串行发送计数器 |
| `hdl/board_ctrl_pwm.v` | 风扇 PWM 计数器 |
| `software/board_ctrl.h`、`software/example.c` | SDK C 辅助函数和例子 |
| `constraints/board_pins_example.xdc` | E19 / D19 管脚约束示例 |
| `tb/tb_board_ctrl_axi.sv` | AXI、PWM、RGB 自动检查仿真 |
| `scripts/package_ip.tcl` | 修改 RTL 后重新封装 |
| `scripts/check_ip.tcl` | 独立 BD 校验、综合和布局布线 |
| `scripts/run_sim.ps1` | 运行四种时钟频率的仿真 |
| `VALIDATION.md` | 本次验证结果和范围 |

在配置好 Vivado 2018.3 命令路径的 PowerShell 中，从本目录运行：

```powershell
./scripts/run_sim.ps1
vivado -mode batch -source scripts/package_ip.tcl
vivado -mode batch -source scripts/check_ip.tcl
```

`build/` 是可重新生成的验证输出。打包脚本只更新本 IP；检查脚本使用独立工程，不会自动添加或修改你的 AD9361 主工程。
