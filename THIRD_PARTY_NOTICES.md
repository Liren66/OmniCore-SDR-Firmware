# 第三方许可说明

根目录 [LICENSE](LICENSE) 保存 GNU GPL v3.0 全文。本项目对可自主授权、且未另行注明许可的
原创代码与文档使用 `GPL-3.0-only`，不包含“或任何后续版本”的额外授权。

第三方材料不因收录于本仓库而自动改用 GPL-3.0。请保留文件中的版权、许可及免责声明；
对含有第三方代码的修改版本，原有许可条件仍然适用。以下为主要组件的定位说明，
具体授权范围以各文件及上游随附条款为准。

| 组件 | 仓库位置 | 现有许可声明及范围 |
|---|---|---|
| ADI HDL/IP | `ip_repo/adi/` | 各模块可能使用不同条款。例如 `axi_ad9361/axi_ad9361.v` 声明可选择 GPL v2 或 ADI 专用 BSD 条款；本次未将这些模块重新授权为 GPL v3。 |
| ADI no-OS / AD9361 驱动 | `OmniCore-SDR-Firmware.sdk/OmniCore-SDR-no-OS/src/` 中 ADI 来源文件 | 例如 `ad9361.c` 保留 ADI 版权、源码/二进制再分发条件及免责声明，并要求软件在 ADI 器件上运行或直接连接 ADI 器件。不能仅将这些条款概括为无附加限制的 BSD。 |
| Xilinx FSBL、BSP 与平台代码 | `OmniCore-SDR-Firmware.sdk/OmniCore-SDR-FSBL/`、各 `*_bsp/`、硬件平台目录 | 保留各文件中的 Xilinx 版权和许可声明；例如 FSBL 的 `src/main.c` 包含再分发时保留版权与许可通知的条件。 |
| 沁恒 CH32H417 EVT、外设库、启动代码及库文件 | `OmniCore-SDR-Bridge/SRC/`、`Common/` 及各核工程中的沁恒来源文件 | 文件头包含沁恒版权声明，部分声明软件及二进制（无论是否修改）用于沁恒生产的微控制器。`Common/libUHSIF.a` 为随工程提供的二进制库，不能凭根目录 LICENSE 推定其采用 GPL。 |
| FTDI EEPROM 工具与配套文件 | `FIDI_EEPROM_Download_Tool/` 中的 EXE、DLL、PDB、XML 及 EEPROM 镜像 | 保留原有版权声明。当前没有应用程序源码，也没有在此为这些第三方二进制新增 GPL 授权。使用和再分发权限以原提供方授权为准。 |

芯片使用限制等条款可能影响与 GPL 代码组合后的分发条件。本文件只是记录实际许可边界，
不为 GPL 添加例外，也不宣称上述限制与 GPL 兼容。

`BOOT.bin`、`Merge.bin`、FPGA 位流及工具 ZIP 中含有的第三方材料仍受对应条款约束。
添加根目录许可证不意味着这些组合产物或既有 Release 的全部内容已经转换为 GPL-3.0。
