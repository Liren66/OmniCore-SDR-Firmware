# OmniCore-SDR-Firmware

OmniCore SDR 的板端固件与下载工具仓库，包含 Zynq-7020 / AD9361 FPGA 与裸机固件、
CH32H417 USB 3.0 双核桥接固件，以及 FT2232 JTAG 接口的 EEPROM 配置工具。

- **Zynq-7020 + AD9361**：PL 逻辑（Vivado）及 PS 裸机软件（Xilinx SDK，no-OS + FSBL）。器件为 `xc7z020clg400-2`，工具版本为 **Vivado / SDK 2018.3**。
- **CH32H417 USB 3.0 桥接**：双核 RISC-V（V3F + V5F）固件，通过 UHSIF 连接 Zynq，使用 **MounRiver Studio 2 / RISC-V Embedded GCC15**。
- **FT2232 JTAG 配置工具**：提供 Windows EEPROM 写入程序、FTDI .NET 库和 EEPROM 镜像，见 [工具说明](FIDI_EEPROM_Download_Tool/README.md)。

原 OmniCore-SDR-Bridge 工程已移入本仓库的 `OmniCore-SDR-Bridge/`，作为普通源码目录维护，
不包含独立 Git 元数据，也不是子模块。Zynq 和 CH32 固件从本仓库统一发布。

## 下载与烧录

从 [GitHub Releases](https://github.com/Liren66/OmniCore-SDR-Firmware/releases) 下载同一版本的两个镜像。
当前版本为 [v2026.10.29.1](https://github.com/Liren66/OmniCore-SDR-Firmware/releases/tag/v2026.10.29.1)，
新增 FT2232 工具包，并将双核固件附件名称统一为 **`Merge.bin`**。

| 文件 | 对应器件 | 内容与使用方式 |
|---|---|---|
| `BOOT.bin` | Zynq-7020 | FSBL + FPGA 位流 + no-OS 应用。SD 启动时放在启动分区根目录，文件名保持 `BOOT.bin`；使用板上 Flash 启动时，通过 Xilinx SDK 的 Flash 编程流程写入。 |
| `Merge.bin` | CH32H417 | V3F + V5F 双核合并固件。使用 MounRiver Studio / WCH-Link 烧录，Flash 起始地址为 `0x08000000`。镜像内 V3F 偏移为 `0x0`、V5F 偏移为 `0x20000`，间隙填充 `0xFF`。 |
| `FIDI_EEPROM_Download_Tool.zip` | FT2232 JTAG 接口 | Windows EEPROM 配置工具及配套文件，解压后按[工具说明](FIDI_EEPROM_Download_Tool/README.md)使用。 |
| `SHA256SUMS.txt` | 校验文件 | 两个固件镜像与工具压缩包的 SHA-256，用于确认下载文件完整。 |

两种镜像分别用于对应芯片，不能互换。建议配套更新同一 Release 的两个镜像。
Windows 下可用 `Get-FileHash .\BOOT.bin, .\Merge.bin, .\FIDI_EEPROM_Download_Tool.zip -Algorithm SHA256` 与校验文件比对。

USB 设备标识为 `1a86:5537`：EP1 IN/OUT 传输 IQ 数据，EP2 IN/OUT 承载 USART1 控制通道，
EP3 IN 输出诊断信息。桥接固件详情见 [Bridge README](OmniCore-SDR-Bridge/README.md)。

## 目录

```text
OmniCore-SDR-Firmware.xpr          Vivado 工程
OmniCore-SDR-Firmware.srcs/        block design（system.bd）、顶层 wrapper、约束
ip_repo/adi/                     ADI hdl_2018_r2 IP（axi_ad9361、axi_dmac 等）
ip_repo/user/                    自研 CH32H417_IP（UHSIF 接口）、BOARD_CTRL_AXI
OmniCore-SDR-Firmware.sdk/
  system_wrapper_hw_platform_0/  硬件平台（hdf、bitstream、ps7_init）
  OmniCore-SDR-no-OS/             AD9361 驱动、SDK 命令协议和 bootimage/
  OmniCore-SDR-FSBL/              FSBL
  *_bsp/                         两个 BSP
  COMMAND_PROTOCOL_V1.txt        SDK 命令协议说明
OmniCore-SDR-Bridge/
  UHSIF_DEVICE.wvsln             MounRiver Studio 2 解决方案
  V3F/                          V3F 核工程
  V5F/                          V5F 核工程、USB 协议栈
  Common/                       共用代码、链接脚本、libUHSIF.a
  SRC/                          CH32H417 外设库、内核和启动文件
FIDI_EEPROM_Download_Tool/       FT2232 JTAG EEPROM 配置程序、FTDI 库与镜像
```

## 重新编译

### Zynq：BOOT.bin

1. 用 Vivado 2018.3 打开 `OmniCore-SDR-Firmware.xpr`。IP 库路径为 `$PPRDIR/ip_repo/adi` 和 `$PPRDIR/ip_repo/user`。
   block design 的生成产物不在仓库中，Generate Bitstream 时会重新生成。
2. 修改 PL 后，将新的硬件平台和位流导出到 SDK。以 `OmniCore-SDR-Firmware.sdk` 为工作区启动 SDK，
   通过 File → Import → Existing Projects 导入两个 BSP、FSBL、no-OS 共四个工程后编译。
3. `OmniCore-SDR-Firmware.sdk/OmniCore-SDR-no-OS/bootimage/output.bif` 依次打包 FSBL、
   `system_wrapper.bit` 和 no-OS 应用。BIF 中是绝对路径，clone 到其他位置后需先更新。
4. 在 SDK 中使用 Create Boot Image，或从仓库根目录在已配置 Xilinx 工具环境的终端中运行：

```text
bootgen -image OmniCore-SDR-Firmware.sdk/OmniCore-SDR-no-OS/bootimage/output.bif -arch zynq -o BOOT.bin -w on
```

SDK 图形界面生成的镜像通常位于 `OmniCore-SDR-Firmware.sdk/OmniCore-SDR-no-OS/bootimage/BOOT.bin`。
打包和发布时请核对该镜像使用的三个输入文件，避免误用 SDK 工作区根目录中的旧副本。

### CH32H417：Merge.bin

1. 用 MounRiver Studio 2 打开 `OmniCore-SDR-Bridge/UHSIF_DEVICE.wvsln`，选择 RISC-V Embedded GCC15。
2. 检查 `V3F/UHSIF_DEVICE_V3F.wvproj`、`V5F/UHSIF_DEVICE_V5F.wvproj` 及各核 `.kernel` 的 `mate` 路径，
   分别指向当前检出目录下的另一个核工程。迁移后的本机配置已经更新；换机器或路径后仍需调整。
3. 先编译 V3F，再编译 V5F，使用 V5F 工程已启用的双核合并配置生成 `OmniCore-SDR-Bridge/V5F/obj/Merge.Bin`。
4. 发布时将合并镜像复制并命名为 `Merge.bin`，保持二进制内容不变。

## 版本管理与发布

仓库保存源码和工程配置，Firmware 原有提交历史继续保留；Bridge 的历史没有合并到 Firmware 的 Git 历史中。
`.runs`、`.cache`、`.ip_user_files`、block design 生成产物、SDK `.metadata`、`Debug/`、
Bridge 的 `obj/`、`.mrs/` 和本地 `dist/` 发布目录均被忽略。
`system_wrapper_hw_platform_0/system_wrapper.bit` 是打包输入，保留在仓库中。

预编译的 `BOOT.bin`、`Merge.bin`、`FIDI_EEPROM_Download_Tool.zip` 及 `SHA256SUMS.txt` 通过 GitHub Release 附件分发。
Release 标签对应统一源码版本；发布说明记录镜像来源及验证范围。

FT2232 工具以已有二进制及配套文件纳入 Git，本仓库尚未包含其应用程序源码。

## 第三方代码

- `ip_repo/adi/` 来自 [ADI hdl](https://github.com/analogdevicesinc/hdl) `hdl_2018_r2`，遵循其原许可证。
- 应用程序中的 AD9361 驱动（`ad9361*.c/h` 等）基于 ADI no-OS `2018_R2` 修改，保留并遵循其原版权声明。
- `OmniCore-SDR-Bridge/SRC/`、`Common/` 中部分代码及 `libUHSIF.a` 来自沁恒 CH32H417 EVT 例程，按其声明仅可用于沁恒生产的微控制器。
