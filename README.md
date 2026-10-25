# OmniCore-SDR-Firmware

OmniCore SDR 的 Zynq 固件工程：Zynq-7020（`xc7z020clg400-2`）+ AD9361，
包含 PL 逻辑（Vivado）和 PS 端裸机软件（Xilinx SDK，no-OS + FSBL）。
工具版本为 **Vivado / SDK 2018.3**。

## 目录

```
OmniCore-SDR-Firmware.xpr          Vivado 工程
OmniCore-SDR-Firmware.srcs/        block design（system.bd）、顶层 wrapper、约束
ip_repo/adi/                       ADI hdl_2018_r2 中用到的 IP（axi_ad9361、axi_dmac 等）
ip_repo/user/                      自研 IP：CH32H417_IP（UHSIF 接口）、BOARD_CTRL_AXI
OmniCore-SDR-Firmware.sdk/
  system_wrapper_hw_platform_0/    硬件平台（hdf、bitstream、ps7_init）
  OmniCore-SDR-no-OS/              应用程序（AD9361 驱动 + SDK 命令协议）
  OmniCore-SDR-FSBL/               FSBL
  *_bsp/                           两个 BSP
  COMMAND_PROTOCOL_V1.txt          SDK 命令协议说明
```

## 重新编译

**FPGA**：用 Vivado 2018.3 打开 `OmniCore-SDR-Firmware.xpr`，IP 库路径已设为 `$PPRDIR/ip_repo/adi` 和 `$PPRDIR/ip_repo/user`。
block design 的输出产物不在仓库中，直接 Generate Bitstream 时会自动重新生成。

**SDK**：以 `OmniCore-SDR-Firmware.sdk` 为工作区启动 SDK，File → Import → Existing Projects，导入四个工程
（两个 BSP、FSBL、no-OS）后编译。

**BOOT.bin**：`OmniCore-SDR-no-OS/bootimage/output.bif` 依次打包 FSBL、`system_wrapper.bit` 和应用程序。
bif 中是**绝对路径**，clone 到其他位置后需要先修改路径，再用 Create Boot Image 或：

```
bootgen -image OmniCore-SDR-no-OS/bootimage/output.bif -arch zynq -o BOOT.bin -w on
```

## 版本管理说明

仓库只保存源文件和配置。`.runs`、`.cache`、`.ip_user_files`、block design 输出产物、SDK 的 `.metadata`、
`Debug/` 和 `BOOT.bin` 都可以重新生成，已在 `.gitignore` 中排除。
`system_wrapper_hw_platform_0/system_wrapper.bit` 例外：它是 SDK 打包 BOOT.bin 的输入，保留在仓库中。

## 第三方代码

- `ip_repo/adi/` 来自 [ADI hdl](https://github.com/analogdevicesinc/hdl) `hdl_2018_r2`，遵循其原许可证。
- 应用程序中的 AD9361 驱动（`ad9361*.c/h` 等）基于 ADI no-OS `2018_R2` 修改，保留并遵循其原版权声明。
