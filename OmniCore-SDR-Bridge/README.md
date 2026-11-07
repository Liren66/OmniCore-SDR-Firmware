# OmniCore-SDR-Bridge

本目录已并入 [OmniCore-SDR-Firmware](../README.md)，由 Firmware 仓库统一管理源码和发布版本，
不再作为独立 Git 仓库或子模块。预编译镜像随 Firmware 的
[GitHub Releases](https://github.com/Liren66/OmniCore-SDR-Firmware/releases) 发布：`Merge.bin`（完整镜像，WCH-Link 烧录）
和 `OmniCore-Bridge-<版本>.ocfw`（USB 升级包）。

OmniCore SDR 的 USB 3.0 桥接固件，运行在沁恒 CH32H417（双核 RISC-V：V3F + V5F）上。
它通过 UHSIF 接口连接 Zynq（AD9361）一侧，向主机提供 USB 设备 `1a86:5537`：

| 端点 | 用途 |
|---|---|
| EP0 | 标准请求；厂商请求 `0xD0` 诊断，`0xB0`–`0xB5` 固件升级 |
| EP1 IN/OUT | IQ 数据流 |
| EP2 IN/OUT | USART1 控制通道（SDK 命令协议） |
| EP3 IN | 诊断输出（启动日志、查询回复） |
| EP3 OUT | 诊断查询（应用 1.0.1 起）：一行文本，回复在 EP3 IN，见下文 |

USB 字符串：厂商 `OmniCore`，产品 `OmniCore SDR`（Bootloader 为 `OmniCore SDR Bootloader`），
序列号 `OmniCore` + 芯片 UID 的 CRC-32（8 位十六进制）。

## 版本

版本号定义在 [`Common/fw_version.h`](Common/fw_version.h)：桥接应用 `OMNICORE_BRIDGE_VERSION_*`，
Bootloader `OMNICORE_BOOT_VERSION_*`。当前应用为 **1.0.1**，Bootloader 为 **1.0.0**。版本出现在：

- USB 设备描述符 `bcdDevice`（1.0.0 → `0x0100`，Windows 硬件 ID 中的 `REV_0100`）；
- 厂商请求 `0xB1`（`och firmware info` / `och info` 显示）；
- `.ocfw` 升级包头；
- EP3 启动日志 `OmniCore bridge 1.0.1 (<构建号>)`。

## EP3 诊断查询（应用 1.0.1 起）

主机向 EP3 OUT（0x03）写一行文本，桥接固件在主循环里执行（不在中断里），回复写进 EP3 IN（0x83）的文本流：

| 查询 | 回复 |
|---|---|
| `typec?` | 现场读取 HD3SS3220 寄存器，回复 `HD3SS3220: 08=xx 09=xx 0A=xx`（与上电日志同格式）；失败为 `HD3SS3220: ERR <原因>` |
| 其他 | `EP3: ERR unknown <查询>` |

- 只在查询时做一次 I2C 读取（约 1 ms），平时不轮询。一次只处理一个查询，答复前 EP3 OUT 保持 NAK。
- EP3 IN 同时还会有其他日志文字，主机应先读空旧数据再发查询，并只解析之后收到的行（OmniCore Host 0.7.1 即如此）。
- Bootloader 的描述符不含 EP3 OUT，保持不变。

每次发布 CH32 应用请递增应用版本号。构建号由 Makefile 取 `git describe`，MRS 构建显示 `dev`。

## Flash 布局与启动流程

```
0x00000  V3F stage-0     上电先运行，决定 V5F 从哪里启动；只能用 WCH-Link 更新
0x20000  V5F 应用         最大 0x1FF00 字节
0x3FF00  应用头           ocfw_header_t（版本、长度、CRC），由 Bootloader 最后写入
0x40000  V5F Bootloader  56 KiB；只能用 WCH-Link 更新
0x4E000  升级请求标志     8 KiB 扇区
```

stage-0（[`V3F/User/main.c`](V3F/User/main.c)）在以下情况把 V5F 唤醒到 Bootloader，否则唤醒到应用：

1. 应用收到升级请求（厂商请求 `0xB0`）后写入了标志并复位；
2. 应用第 0 页为空（新芯片或升级中断）。Bootloader 在整包 CRC 校验通过后才写第 0 页，
   所以中途断电、拔线的设备会停在 Bootloader，重新执行升级即可。

保护措施：

- Bootloader 在独立看门狗下运行；若卡死，芯片约 3 秒后复位，stage-0 回到应用。
- Bootloader 被主机配置后清除标志：进入 Bootloader 后不升级、直接断电，下次仍启动应用。
- 10 秒内没有主机配置且应用存在时，Bootloader 自行清除标志回到应用。
- Bootloader 只擦写应用区和标志扇区，不会改动 stage-0 和自身。

## USB 升级

Bootloader 复用应用的 USB 协议栈：优先 USB3 SuperSpeed，失败时退到 USB2 High-Speed。设备 VID/PID、
序列号和端点布局与应用相同，Windows 下沿用 libusbK 驱动，Linux 下沿用 udev 规则。

主机端使用 OmniCore Host 0.5.0 及以上：

```bash
och firmware update OmniCore-Bridge-1.0.0.ocfw
```

依次进入 Bootloader、擦除、写入、校验、重启并确认新版本，约 5 秒。`och firmware info` 查看版本和模式，
`och firmware boot` 离开 Bootloader，`och firmware enter` 手动进入 Bootloader。

协议（[`Common/ocfw.h`](Common/ocfw.h)，主机实现见 OmniCore-SDR-Host `src/core/firmware.cpp`）：

| 请求 | 方向 | 作用 |
|---|---|---|
| `0xB0` ENTER_BOOT，wValue=`0x5AA5` | OUT | 应用：写标志并复位进入 Bootloader |
| `0xB1` INFO | IN，64 字节 | 应用 / Bootloader：模式、版本、应用状态、操作状态 |
| `0xB2` ERASE | OUT | 擦除应用区 |
| `0xB3` WRITE，偏移 = wIndex:wValue | OUT，≤512 字节，256 对齐 | 写入一块 |
| `0xB4` COMMIT | OUT，64 字节头 | 校验整包 CRC，写第 0 页和应用头 |
| `0xB5` REBOOT，wValue=`0x5AA5` | OUT | 复位，stage-0 启动应用 |

擦写在 Bootloader 主循环中执行，主机轮询 `0xB1` 的 state/error。`.ocfw` 文件 = 64 字节
`ocfw_header_t` + 应用镜像，头和镜像各带 CRC-32，硬件 ID 不符或文件损坏时主机直接拒绝。

## 编译

发布镜像在 Git Bash 中用命令行构建（使用 MounRiver Studio 2 自带的 RISC-V Embedded GCC15 和 Make，另需 Python 3）：

```bash
make
```

输出在 `build/`：`Merge.bin`、`OmniCore-Bridge-<版本>.ocfw` 及 `omnicore_v3f/app/boot` 的 ELF、BIN、MAP。
`make flash` 用 WCH-Link 烧录 `Merge.bin`。

MounRiver Studio 2 的 `UHSIF_DEVICE.wvsln`（V3F、V5F 两个工程）仍可用于调试应用。它生成的
`V5F/obj/Merge.Bin` 不含 Bootloader 和应用头，不要作为发布文件；Bootloader 只能用 Makefile 构建。

## 调试注意

- **PB8/PB9 既是 SWCLK/SWDIO，也是 USB2 的 D+/D-。** 应用启动 3 秒后会关闭 SWD
  （`Common/hardware.c` 中的 `GPIO_Remap_SWJ_Disable`），以便 USB2 可用；此后 WCH-Link 连不上。
  需要用 WCH-Link 烧录时，先运行 `och firmware enter`：Bootloader 走 USB3 时不占用这两个引脚。
  Bootloader 只有退到 USB2 时才会关闭 SWD。
- `V3F/UHSIF_DEVICE_V3F.wvproj`、`V5F/UHSIF_DEVICE_V5F.wvproj` 和两个 `.kernel` 里的 `mate` 字段是另一个核工程的**绝对路径**。
  本机路径已更新到 `OmniCore-SDR-Firmware/OmniCore-SDR-Bridge/` 下；clone 到其他位置后，
  需要把它改成对应核在本机的路径，否则双核联调找不到对方。
- `obj/`、`build/` 和 `.mrs/` 不纳入版本管理；调试配置 `.mrs/launch.json` 由 MounRiver 在本机生成。
- `SRC/`、`Common/` 中的部分代码和 `libUHSIF.a` 来自沁恒 CH32H417 EVT 例程，按其声明仅可用于沁恒生产的微控制器。
