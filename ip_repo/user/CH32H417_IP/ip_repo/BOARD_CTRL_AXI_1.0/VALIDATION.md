# 验证记录

日期：2026-10-24。工具：Vivado / XSim / SDK 2018.3，器件 XC7Z020-2CLG400。

## 已完成

| 检查 | 结果 |
|---|---|
| IP 封装完整性 | `ipx::check_integrity` 通过；重复运行打包脚本通过 |
| IP Catalog / BD | 可发现 `user.org:user:BOARD_CTRL_AXI:1.0`；100 MHz 和 125 MHz BD 校验通过 |
| 封装后输出生成 | 125 MHz 配置生成成功；生成的 HDL 使用 `.CLK_FREQ_HZ(125000000)`，AXI/时钟接口元数据一致 |
| RTL 自动仿真 | 50、100、125、200 MHz 全部输出 `ALL_TESTS_PASSED` |
| 独立综合 | 182 个 Slice LUT、317 个触发器；0 latch、0 DSP、0 BRAM |
| 独立布局布线 | 100 MHz，WNS +3.939 ns、WHS +0.108 ns，TNS/THS 为 0 |
| C 示例编译 | ARM Cortex-A9，`-std=c99 -Wall -Wextra -Werror`，使用现有 SDK BSP 头文件，编译通过 |

仿真检查的行为包括：

- 复位默认值、AW/W 同时到达和不同先后顺序。
- 读写响应反压、读写通道并行、读响应保持稳定。
- 随机字节写、保留字节、空 WSTRB、只读/未映射/未对齐地址错误响应。
- 未配对 AW、未完成读写响应在复位时清除。
- PWM 周期及高电平宽度、周期中途更新、0%/50%/100%、超范围高电平值、周期为 0/1。
- 24 bit GRB 顺序、0/1 高低电平宽度、bit 周期、至少 300 us 帧间低电平。
- RGB 帧中途改色在下一帧生效、全零/全一数据、发送中复位后重新开始。

## 证据

- `package.log`：`Integrity check passed`、`BOARD_CTRL_PACKAGE_OK`。
- `catalog.log`：`BOARD_CTRL_BD_OK`、`BOARD_CTRL_CATALOG_GENERATE_OK_125MHZ`。
- `build/sim/sim_50000000.log`、`sim_100000000.log`、`sim_125000000.log`、`sim_200000000.log`：各自的 `ALL_TESTS_PASSED`。
- `build/check/utilization.rpt`：综合资源。
- `build/check/timing.rpt`：100 MHz 独立布局布线后的时序。
- `build/check/drc.rpt`：无 Error；仅有独立 Zynq 外设设计不包含 PS7 的 `ZPS7-1` Warning。
- `build/example.o`：C 示例编译产物。

## 验证范围

本次验证对象是独立 IP，没有修改或重新实现 AD9361 主工程，也没有下载到板卡验证灯色、风扇转速和电气波形。集成后的系统时序、管脚和硬件表现仍需在主工程中检查。

独立布局布线是 out-of-context 模式，未约束主工程 AXI 输入/输出路径；其时序结果仅用于评估 IP 内部路径，不等同于主工程时序签核。该模式出现接口缺少 `HD.PARTPIN_LOCS` 的路由提示是预期现象。`AWPROT/ARPROT` 不参与功能，综合会提示未使用。

独立 BD 的外部 AXI 地址空间为 4 KB，Vivado 2018.3 的地址自动分配会提示将默认 64 KB 缩减为 4 KB；实际映射和 IP 声明均为 4 KB。未 suppress 工具警告。
