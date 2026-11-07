# OC_SYNC_AXI：多板同步 IP

适用于 Vivado 2018.3、XC7Z020。VLNV 为 `user.org:user:OC_SYNC_AXI:1.0`，RTL 顶层为 `oc_sync_axi`。
在主工程 Block Design 中的实例名是 `OC_SYNC_AXI_0`，地址 `0x43C30000`（64 KB 段，IP 只解码 4 KB）。

## 功能

| 功能 | 说明 |
|---|---|
| REF_OUT | 本板 40 MHz VCTCXO（CLK_FB_FPGA）用计数器四分频得到 10 MHz，占空比 50%，经 ODDR 输出到 J13，给下一块板的 10M_EXT（J3） |
| TRIG | 同步脉冲输出到 J9。三种触发方式：命令立即触发、按周期触发、在下一个 PPS_EXT 上升沿触发。脉宽可设，可选在 40 MHz 上升沿或下降沿发出 |
| SYNC / PPS 计数 | 对 AD9361_SYNC（J1，同时接 AD9361 SYNC_IN）和 PPS_EXT 计上升沿数，并测量最近两个沿之间的本地 40 MHz 周期数 |
| PPS 选择 | 送给时间基准（CH32H417_IP `pps_ext`）的 PPS 二选一：PPS_EXT 引脚或 AD9361_SYNC；组合逻辑，不增加延迟 |
| 回环 | 单板测试用：SYNC 计数器和 PPS 选择改用内部 TRIG，不需要 J9 到 J1 的线缆 |

间隔计数就是锁定检测：同一个参考下，每块板测到的间隔都正好等于发生器的周期（±1）。ADF4002 的锁定信号只接到 D5 指示灯，CPU 读不到，这个计数可以替代它。

## 寄存器

32 位对齐访问，偏移相对 IP 基地址。

| 偏移 | 名称 | 访问 | 复位值 | 含义 |
|---|---|---|---|---|
| 0x00 | ID | RO | `0x4F435359` | "OCSY" |
| 0x04 | VERSION | RO | `0x00010000` | 1.0 |
| 0x08 | CTRL | RW | 0 | bit0 REF_OUT 使能，bit1 TRIG 使能，bit2 下降沿发出，bit3 周期触发，bit4 时间基准 PPS 改用 SYNC，bit5 回环 |
| 0x0C | TRIG_WIDTH | RW | 80 | 脉宽，单位 40 MHz 周期；0 按 1 处理 |
| 0x10 | TRIG_PERIOD | RW | 40000000 | 周期，单位 40 MHz 周期；小于 1000 按 1000 处理 |
| 0x14 | CMD | WO | – | bit0 立即触发，bit1 在下一个 PPS 触发，bit2 取消等待，bit3 计数清零 |
| 0x18 | STATUS | RO | – | bit0 配置未生效，bit1 检测到 40 MHz，bit2 正在等待 PPS，bit3 TRIG 电平，bit4 SYNC 电平，bit5 PPS 电平 |
| 0x1C | REF40_HZ | RO | – | 用 AXI 时钟测得的 40 MHz 频率，1 s 闸门，分辨率 16 Hz |
| 0x20 | SYNC_COUNT | RO | – | SYNC 上升沿数 |
| 0x24 | SYNC_INTERVAL | RO | – | 最近两个 SYNC 沿之间的 40 MHz 周期数 |
| 0x28 | PPS_COUNT | RO | – | PPS_EXT 上升沿数 |
| 0x2C | PPS_INTERVAL | RO | – | 最近两个 PPS 沿之间的 40 MHz 周期数 |
| 0x30 | TRIG_COUNT | RO | – | 已发出的 TRIG 脉冲数 |

写 CTRL、TRIG_WIDTH、TRIG_PERIOD、CMD 后，配置通过一次跨时钟握手送到 40 MHz 域。STATUS.bit0 清零表示已经生效；40 MHz 不存在时会一直为 1。
上一个脉冲还没结束时到来的触发请求会被丢弃，不产生新沿，也不计入 TRIG_COUNT。因此脉宽要小于周期。

## 时序

- REF_OUT 和 TRIG 都由 IOB 里的 ODDR 输出，发出时刻对齐本板 40 MHz 的边沿。下降沿模式把 TRIG 推迟半个周期（12.5 ns），用来调整 AD9361 SYNC_IN 的建立和保持余量。
- PPS 到 TRIG 的延迟固定为 3 到 4 个 40 MHz 周期，同步器亚稳态可能带来 1 个周期的抖动。
- CLK_FB_FPGA 在 H17，是 IO_L13N_T2_MRCC_35。同一对的 P 脚 H16 是 UHSIF 时钟，所以 H17 走普通布线，需要 `CLOCK_DEDICATED_ROUTE FALSE`。

## 文件与验证

| 文件 | 用途 |
|---|---|
| `hdl/oc_sync_axi.v` | AXI4-Lite 寄存器、跨时钟域、频率测量、PPS 选择 |
| `hdl/oc_sync_core.v` | 40 MHz 域：分频、TRIG 发生器、沿计数、ODDR |
| `hdl/ocs_hold_xfer.v` | 跨时钟域握手（与 CH32H417_IP 的 `oc_hold_xfer` 相同，改名以免冲突） |
| `tb/tb_oc_sync_axi.sv` | 自检测试平台，最后一行打印 `TB RESULT: PASS` 或 `FAIL` |
| `scripts/run_sim.sh` | 用 xsim 运行测试平台 |
| `scripts/package_ip.tcl` | 修改 RTL 后重新封装 |

```bash
bash ip_repo/user/OC_SYNC_AXI_1.0/scripts/run_sim.sh
vivado -mode batch -source ip_repo/user/OC_SYNC_AXI_1.0/scripts/package_ip.tcl
```

测试平台覆盖：ID 与复位值；10 MHz 周期；频率测量；单次触发的脉宽和发出边沿（上升沿 / 下降沿）；使能与触发合并为一次传输；经线缆的周期触发与 SYNC 间隔；最小周期限制；内部回环；PPS 间隔与在 PPS 时触发；PPS 选择；关闭后输出为低。
