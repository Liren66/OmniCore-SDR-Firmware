# 多板同步

固件 SDK_CMD_1.2（OC_SYNC_AXI 1.0）起，多块 OmniCore SDR 可以共用一个频率参考、一个采样时间基准，并对齐 AD9361 的采样时钟。
支持两种接法：

| 方式 | 适用 | 频率参考 | 时间 / 同步脉冲 |
|---|---|---|---|
| 菊花链（`chain`） | 没有外部参考 | 主板自身的 40 MHz，或主板 J3 上的 10 MHz；每块板把 REF_OUT（J13）接到下一块板的 10M_EXT（J3） | 主板 TRIG（J9）经 1 分 N 接到每块板的 AD9361_SYNC（J1），主板自己也接 |
| 公共参考（`common`） | 有多路输出的原子钟、GPSDO 等 | 每块板 J3 接同一个 10 MHz | 每块板 PPS_EXT（J6）接同一个 PPS；做 MCS 时每块板用一根短线把自己的 J9 接到自己的 J1 |

主机端用 `och sync` 一条命令完成同步，见 OmniCore-SDR-Host 的 `docs/multi-board-sync.md`。下面说明硬件、FPGA 和 SDK 部分。

## 1. 原理图上的信号（Rev V2.8）

| 信号 | FPGA 管脚 | Bank 电压 | 连接 |
|---|---|---|---|
| CLK_FB_FPGA | H17（IO_L13N_T2_MRCC_35） | 3.3 V | LMK1C1104 的一路输出，与 AD9361 XTALN 同源的 40 MHz |
| REF_OUT | T9（Bank 13） | 1.8 V | 直接接 J13（IPEX） |
| TRIG | Y6（Bank 13） | 1.8 V | 经 R143（220 Ω）接 J9（IPEX） |
| AD9361_SYNC | U20（Bank 34） | 1.8 V | J1（IPEX）→ AD9361 SYNC_IN（H5）和 FPGA，R62 4.7 kΩ 下拉 |
| PPS_EXT | Y7（Bank 13） | 1.8 V | J6，经 BAT54S 钳位 |

- CLK_FB_FPGA 和 UHSIF_CLK（H16）同在一对 MRCC 管脚上。H16 占用了专用时钟路由，所以 H17 走普通布线，约束里设置了 `CLOCK_DEDICATED_ROUTE FALSE`，和现有的 AD9361_CLK_OUT（U10）一样处理。对 UHSIF 没有影响。
- REF_OUT 取自 CLK_FB_FPGA，不取自 AD9361 CLK_OUT。这样它上电就有，不依赖 AD9361 初始化；主板重新配置 AD9361 时，从板的参考也不会中断。
- 从板 J3 先交流耦合，再经 49.9 Ω 端接到地。1.8 V 的 REF_OUT 打到这个 50 Ω 负载上，幅度大约 0.8–0.9 Vpp，接近 ADF4002 REFIN 灵敏度的下限。如果从板锁不上，先用示波器测 J3 处的幅度；只在从板上拆掉 R114，可以得到完整的 1.8 V 摆幅。

## 2. FPGA

新增 IP `OC_SYNC_AXI_0`（`ip_repo/user/OC_SYNC_AXI_1.0`，寄存器见该目录的 README），地址 `0x43C30000`，接在 `axi_cpu_interconnect/M01_AXI`。
CH32H417_IP 不变。原来 PPS_EXT 直接接到 CH32H417_IP 的 `pps_ext`，现在先经过新 IP 的 PPS 选择：默认仍然是 PPS_EXT，`time_source=sync` 时改为 AD9361_SYNC。

```bash
vivado -mode batch -source scripts/build_multi_sync_bitstream.tcl -tclargs <输出目录> [并行任务数]
```

脚本是幂等的：加端口、加 IP、连线、分配地址、生成位流，并导出 `system_wrapper.bit` 和 `system_wrapper.hdf`。

## 3. SDK 命令

| 命令 | 作用 |
|---|---|
| `sync_status?` | 一行汇总：40 MHz 频率、输出状态、SYNC/PPS 计数与间隔、TRIG 计数、MCS 状态 |
| `ref_out=0\|1` | J13 输出 10 MHz |
| `trig_out=0\|1` | J9 输出 TRIG；0 时保持低电平 |
| `trig_edge=rising\|falling` | TRIG 在 40 MHz 上升沿或下降沿发出 |
| `trig_width=<周期数>` | 脉宽，默认 80（2 µs） |
| `trig_period=<周期数>` | 0 关闭；否则按周期发送，至少 1000，40000000 即 1 Hz |
| `trig_pulse!` | 立即发一个脉冲 |
| `trig_at_pps!` / `trig_disarm!` | 在下一个 PPS_EXT 上升沿发一个脉冲 / 取消 |
| `sync_loopback=0\|1` | SYNC 计数和时间基准改用内部 TRIG（单板无线缆测试用） |
| `sync_clear!` | SYNC、PPS、TRIG 计数清零 |
| `mcs_step=0..5` | AD9361 多片同步的一步；第 0 步把 ENSM 切到 ALERT，第 5 步恢复。第 2、4 步之后需要一个 SYNC 脉冲 |
| `mcs_run!` | 单板一次做完 0–5 步，第 2、4 步由本板 TRIG 发脉冲（需要 J9→J1 线缆） |
| `time_source=sync` | 时间基准在 AD9361_SYNC 的上升沿装载 `time_next_pps=` 的值 |

收到 SYNC 沿时打印 `EVT tseq=<n> type=sync_in src=sync count=<累计> new=<新增> interval=<周期数>`，最多每 250 ms 一行。
`system_info?` 增加一行 `sync_capabilities sync=1 ref_out=1 trig=1 mcs=1 time_source_sync=1 version=0x00010000`。

上电默认：REF_OUT 关，TRIG 保持低电平，时间基准 PPS 仍为 PPS_EXT。所以不用同步功能时，原有行为不变。

## 4. 同步过程

### 菊花链

1. 主板 `clock_ref_source=0`（自由运行），也可以设为 1 来跟随主板 J3 上的 10 MHz；从板 `clock_ref_source=1`。所有板 `ref_out=1`，只有主板 `trig_out=1`。
2. 锁定检查：主板每 100 ms 发一个 TRIG。每块板测到的 SYNC 间隔连续 3 次等于 4000000 ± 1 个本地 40 MHz 周期，就说明它的 40 MHz 已经锁到主板上。
3. AD9361 MCS：每块板依次执行 `mcs_step=0` 到 `5`，第 2、4 步之后由主板 `trig_pulse!` 发脉冲；每块板的 SYNC 计数都要加 1。
4. 时间：所有板 `time_source=sync` 并执行 `time_next_pps=<T>`，然后主板发一个 TRIG，所有板在同一个沿装载 T。
5. 可选：保留周期 TRIG（例如 1 Hz）作为公共 PPS，`sync_status?` 的间隔持续反映锁定状态。

### 公共参考

1. 所有板 `clock_ref_source=1`。
2. 锁定检查：每块板测到的 PPS 间隔等于 40000000 ± 1。
3. MCS（可选）：每块板执行 `mcs_step`；第 2、4 步在刚过一个 PPS 后，所有板执行 `trig_at_pps!`，在同一个 PPS 沿各自发出 TRIG，经本板的 J9→J1 线缆到达 AD9361。
4. 时间：刚过一个 PPS 后，所有板 `time_source=external` 并执行 `time_next_pps=<T>`，在下一个 PPS 沿一起装载。

## 5. 限制

- MCS 对齐的是采样时钟和数据时钟。各板 AD9361 本振的相位每次调谐都是随机的，需要用公共信号校准，或者通过 TXLO_EXT / RXLO_EXT 共享外部本振。
- TRIG 经 220 Ω 串联电阻和分路器到达各板，上升时间约十几 ns，和 40 MHz 的周期是同一量级。发出边沿做成了可选（`trig_edge`），多板实测时用重复 MCS 来确认余量。
- 改变采样率或重新初始化 AD9361 后，MCS 和时间都要重新同步。
- 单板只能验证 FPGA 和 SDK 的流程（`och sync --loopback --no-mcs`）。REF_OUT 的波形、从板锁定、线缆上的 SYNC 和 MCS 的实际效果，需要两块以上的板子才能验证。
