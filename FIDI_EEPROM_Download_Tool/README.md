# FT2232 JTAG EEPROM 配置工具

本目录保存 OmniCore SDR 项目使用的 FT2232 JTAG 接口 EEPROM 配置工具。
目录名沿用原始的 `FIDI_EEPROM_Download_Tool`；发布时提供同名 ZIP，方便直接下载。

该工具写入的是 FTDI 接口芯片的 EEPROM 配置。Zynq 的启动镜像仍使用 `BOOT.bin`，
CH32H417 的双核固件仍使用 `Merge.bin`，烧录方式见[项目 README](../README.md)。

## 文件

| 文件 | 说明 |
|---|---|
| `EEPROM.exe` | Windows .NET EEPROM 写入程序，文件版本 1.0.0.0。 |
| `eep.bin` | 程序读取的 EEPROM 镜像，256 字节。 |
| `eep_4232.bin` | 随原工具提供的另一份 256 字节镜像；文件名标注为 4232，具体适用硬件需由提供方确认。 |
| `FTD2XX_NET.dll` | FTDI D2XX 的 .NET 封装库。 |
| `FTD2XX_NET.xml` | 封装库 API 文档。 |
| `EEPROM.pdb` | 随原工具提供的调试符号。 |

## 使用

1. 在 Windows 上准备兼容的 .NET 运行环境及 FTDI D2XX 驱动/本机库。`FTD2XX_NET.dll` 是封装库，
   仍依赖本机的 `FTD2XX.DLL`；本目录没有附带驱动安装包。
2. 完整解压工具包，保留程序、DLL 和镜像的相对位置；确认 `eep.bin` 与目标硬件匹配。
3. 连接待配置的 FT2232 设备，在工具目录中启动 `EEPROM.exe`，根据程序输出确认设备和写入结果。
   程序包含 EEPROM 擦除/写入操作，运行前应保存原配置，并断开无关 FTDI 设备。
4. 配置完成后，重新连接设备，再使用项目对应的 Xilinx 工具验证 JTAG 连接。

程序通过相对文件名 `eep.bin` 读取镜像，请从本目录运行。不要将 `BOOT.bin` 或 `Merge.bin` 改名为 `eep.bin`。
本次发布仅整理现有工具、文档和校验值，未执行 EEPROM 写入或硬件 JTAG 验证。

## 来源与维护

本目录按项目已有文件原样收录，未修改程序或镜像；当前仅有二进制与配套文档，没有应用程序源码。
FTDI 库及原工具保留各自原有版权声明。
