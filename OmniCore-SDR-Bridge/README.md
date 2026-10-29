# OmniCore-SDR-Bridge

本目录已并入 [OmniCore-SDR-Firmware](../README.md)，由 Firmware 仓库统一管理源码和发布版本，
不再作为独立 Git 仓库或子模块。预编译双核镜像以 `merge.bin` 为名随 Firmware 的
[GitHub Releases](https://github.com/Liren66/OmniCore-SDR-Firmware/releases) 发布。

OmniCore SDR 的 USB 3.0 桥接固件，运行在沁恒 CH32H417（双核 RISC-V：V3F + V5F）上。
它通过 UHSIF 接口连接 Zynq（AD9361）一侧，向主机提供 USB 设备 `1a86:5537`：

| 端点 | 用途 |
|---|---|
| EP1 IN/OUT | IQ 数据流 |
| EP2 IN/OUT | USART1 控制通道（SDK 命令协议） |
| EP3 IN | 诊断输出 |

USB 字符串：厂商 `OmniCore`，产品 `OmniCore SDR`，序列号 `OmniCore` + 芯片 UID 的 CRC-32（8 位十六进制）。

## 目录

```
UHSIF_DEVICE.wvsln   MounRiver Studio 2 解决方案
V3F/                 V3F 核工程（UHSIF_DEVICE_V3F）
V5F/                 V5F 核工程（UHSIF_DEVICE_V5F），USB 协议栈在这里
Common/              两核共用代码、链接脚本、libUHSIF.a
SRC/                 CH32H417 外设库、内核与启动文件
```

## 编译

用 MounRiver Studio 2 打开 `UHSIF_DEVICE.wvsln`，工具链为 RISC-V Embedded GCC15。
编译后在 `V5F/obj/Merge.Bin` 生成双核合并镜像（V3F 在 0x0，V5F 在 0x20000，空白填 0xFF），下载时使用该文件。
发布文件 `merge.bin` 与该合并镜像内容一致。使用 MounRiver Studio / WCH-Link 下载到 CH32H417，
工程配置中的 Flash 起始地址为 `0x08000000`；不要将它作为 Zynq 的启动镜像。

## 注意

- `V3F/UHSIF_DEVICE_V3F.wvproj`、`V5F/UHSIF_DEVICE_V5F.wvproj` 和两个 `.kernel` 里的 `mate` 字段是另一个核工程的**绝对路径**。
  本机路径已更新到 `OmniCore-SDR-Firmware/OmniCore-SDR-Bridge/` 下；clone 到其他位置后，
  需要把它改成对应核在本机的路径，否则双核联调找不到对方。
- `obj/` 和 `.mrs/` 不纳入版本管理；调试配置 `.mrs/launch.json` 由 MounRiver 在本机生成。
- `SRC/`、`Common/` 中的部分代码和 `libUHSIF.a` 来自沁恒 CH32H417 EVT 例程，按其声明仅可用于沁恒生产的微控制器。
