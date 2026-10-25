OmniCore AD9361 SDK 源码导航（Zynq，单片 AD9361）
================================================

1. 从哪里开始看

main.c
    程序入口：开启缓存、初始化控制台和板级外设、启动 RF、循环处理命令。
    不再包含默认参数大表、多平台选择、DMA 演示或不可达的退出代码。

board_config.c / board_config.h
    AD9361 默认初始化参数和 RX/TX 默认 FIR 系数。
    参数使用 .字段名 = 值，修改时不必数结构体成员的位置。
    默认：40 MHz 参考、30.72 MSPS、2R2T LVDS、内部 RX/TX LO。
    GPIO100 仅保留 ADI 驱动兼容接口；实际硬复位走 FCLK_RESET0_N。

console_commands/command.c / command.h
    原有 AD9361 命令（频率、增益、带宽、DDS 等）。
    command_execute() 处理 @id 协议封装；执行前先交给扩展命令表。

console_commands/sdr_commands.c / sdr_commands.h
    新增普通命令的统一入口。文件顶部 commands[] 可以直接查找：
    命令字符串、具名回调、参数个数、最大值、说明、示例。
    例如 rx_dc_correction= 对应 sdr_cmd_rx_dc_correction()。
    每个回调再调用下面对应模块，不再依赖隐式包含的 .inc 文件。

2. 新功能分别放在哪里

命令                                      实现文件（console_commands/）
rx_dc_correction? / =0或1                 sdr_correction.c / .h
rx_iq_correction? / =0或1                 sdr_correction.c / .h
clock_ref_source? / =0或1                 sdr_reference.c / .h
adf4002_status? / adf4002_self_test!       sdr_reference.c / .h
rx_lo_source? / =0或1                     sdr_external_lo.c / .h
tx_lo_source? / =0或1                     sdr_external_lo.c / .h
pps_input_en? / =0或1 / pps_status?       sdr_pps.c / .h
rx_fir_config? / tx_fir_config? / =0      sdr_fir.c / .h
rx_fir_begin= / tx_fir_begin=             sdr_fir.c / .h
fir_data= / fir_commit= / fir_abort=      sdr_fir.c / .h
rx_fir_read= / tx_fir_read= / fir_enable=  sdr_fir.c / .h
clock_begin= / clock_rx= / clock_tx=      sdr_clock.c / .h
clock_commit= / clock_abort=             sdr_clock.c / .h
clock_sample_rate=                       sdr_clock.c → sdr_fir.c
clock_chain?                             sdr_commands.c → sdr_control.c

FIR 和时钟的分段上传需要 token、CRC 或完整性校验，因此有各自的
事务解析器。sdr_commands_execute() 先调用这两个解析器，再查普通命令表。
时钟的实际应用和回滚与 FIR 共用 sdr_fir.c，避免两套恢复逻辑。

console_commands/sdr_control.c / sdr_control.h
    RF 启动/恢复、TX 门控、USB 通道控制、流状态、故障事件与状态查询。
    没有字符串命令分发表。中断对象和故障历史仅由此模块维护。

console_commands/sdr_runtime.h
    模块内部的共享状态与操作接口。sdr_state 只在主循环访问，
    IRQ 不得直接改写。外部应用通常只需包含 sdr_control.h。

platform_xilinx/adf4002.c / .h
    ADF4002 GPIO 串行驱动、参考源切换、D5 自检。
platform_xilinx/pps_monitor.c / .h
    PPS 寄存器、中断、计数和超时检测。
platform_xilinx/ch32h417_irq.c / .h
    Zynq 侧 USB 桥中断服务；不是 CH32H417 MCU 固件工程。

3. 如何增加普通命令

① 在 sdr_commands.h 声明 sdr_cmd_xxx(const double *values, unsigned count)。
② 在 sdr_commands.c 的 commands[] 登记命令名、回调、数量/范围、帮助和示例。
③ 实现回调，调用对应功能模块；复杂寄存器操作留在功能模块内。
   普通多参数命令用空格分隔（如 usb_data_en=1 0），FIR/clock 事务参数
   用逗号分隔；不要混淆两种既有语法。
④ 回调返回 0 成功、-1 失败；失败时通过 sdr_command_error() 输出 ERR。
   公共解析器已检查参数数量、非负整数和上限；额外条件由回调检查。
   无参数命令使用 count=0，查询回调不要读取未赋值的 values[0]。
⑤ 若增加新的 .c，SDK 重新扫描后会自动编译。命令行提前构建时，
   按工程根目录 makefile.defs 的去重写法补充对象文件。

新增 AD9361 原有风格命令也可沿用 command.c / command.h 的 cmd_list。
不要绕过已有的 RF 就绪、TX/USB 停止检查和寄存器回读/失败回滚。

4. 行为约定

- 命令名、参数、返回字段及 @id 协议保持兼容，现有 OCH 可直接使用。
- help? 会列出命令说明、参数范围及示例。
- TX/USB 默认关闭；PPS 默认关闭，仅作 DATA_CLK 频率监控，不调节时钟。
- 当前没有 PPS 输入时，开启后应报告 waiting/no_signal，频率无效。
- 外部 LO 默认关闭，输入频率为 RF LO 的两倍；寄存器回读不证明信号存在。
- ADF4002 没有 CPU 锁定反馈，D5 由芯片硬件锁定检测控制。
- 时钟/FIR/参考源等切换需要停止相关流，切换期间丢样属于预期行为。
- 保留 ADI 芯片驱动的校准、增益、FDD/TDD 和物理接口处理，不将芯片
  的有用功能误当作开发板演示删除。

5. 构建与整理范围

使用现有 Xilinx SDK 2018.3 工程构建；makefile.defs 支持 SDK 尚未刷新
源文件列表时的命令行构建，刷新后也不会重复链接同一对象。
src/.clang-format 定义应用代码格式；ADI 厂商驱动保留原有主体风格和许可证。
已清理其他平台/开发板分支、第二片 RF 核分支和未使用的 DDR DMA 演示。
DATA_SEL_DMA 在本板表示 USB 桥输入，因此保留。
CH32H417 固件工程及 Vivado 工程不在本次整理范围内。


2026-10-24：RGB 状态灯、风扇自动温控与 Zynq 核心温度
-------------------------------------------------
硬件：XPAR_BOARD_CTRL_AXI_0_BASEADDR（当前 0x43C20000），RGB=E19，风扇=D19。
board_monitor.c/h：XADC 温度读取、RGB 状态/闪烁、PWM 分档/回差/启动加速。
sdr_board.c/h：映射 RF/USB 状态，输出板级诊断；命令仍统一登记于 sdr_commands.c。
主循环服务约 20ms 一次，温度读取周期 1s；平台延时与串口等待继续服务温控。

默认：RGB 开启、通道最大 38/255（约 15%）；风扇温控关闭、PWM=0%，频率 25kHz。
命令：rgb_enable=0/1、rgb_enable?、rgb_status?；
      fan_enable=0/1、fan_enable?、fan_status?；core_temp?。
help?、system_info?、system_status? 已同步扩展，能力为 board_rgb/fan_control/core_temp。
core_temp 使用 Zynq XADC，原有 temperature 行加 source=ad9361。
温度单位 milli_celsius/temp_mC 为毫摄氏度；valid=0 时输出 unknown，禁止当作 0℃。

升温档位：<40℃=0%，40/50/60/70℃分别进入 30/50/70/100%。
降档回差 2℃；从停止启动时输出 100% 约 500ms，随后进入目标档位。
温控开启且采样失败/超过 2.5s 未更新时输出 100%；关闭时始终输出 0%。
高温告警 80℃触发、低于 75℃解除，不自动覆盖温控开关。
RGB：初始化黄，待机绿，RX 上传蓝，TX 紫，双向青，恢复橙色 1Hz，故障红，高温红色 4Hz。
优先级：关闭 > 高温 > 故障 > 恢复 > 初始化 > 双向 > TX > RX > 待机。
重新运行 ELF 恢复默认；rf_reinit! 保留 RGB/风扇开关。
阈值和颜色具名常量集中于 board_monitor.c，供后续调整；0% 停转行为取决于实际风扇。
OCH 0.3.5 的 docs/board-control.md 包含完整协议与 C/C++ 使用说明。
