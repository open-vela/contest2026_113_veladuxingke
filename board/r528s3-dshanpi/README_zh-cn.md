# R528S3-DshanPi 板级支持

[ [English](README.md) | 简体中文 ]

本目录是“Vela 智能桌面管家”参赛版本使用的板级支持包，目标硬件为百问网
R528S3-DshanPi（全志 R528、双核 Cortex-A7）。它包含 ST7796U2 屏幕、FT5x06
触摸、板载 DMIC/音频 Codec、Realtek Wi-Fi，以及外接 TWI3 环境传感器所需的
初始化和资源。

## 包含内容

- R528 自定义板级初始化和 480x320 RGB565 ST7796U2 帧缓冲；
- `/dev/input0` FT5x06 触摸输入；
- ROMFS 启动脚本、中文字体、Wi-Fi/蓝牙资源和桌面管家语音 PCM 资源；
- 最终镜像使用的 `nsh` 配置，已启用 `R528_ST7796`、TWI3、音频/DMIC、
  Micro-TF 和四个参赛应用；
- `build/` 下的打包辅助脚本。

参赛 manifest 会把本仓库的四个应用软链接到 openvela 编译树；板级配置通过
`CONFIG_ARCH_BOARD_CUSTOM_DIR` 直接引用本目录，不需要手动复制或修改 vendor
板级目录。

## 硬件连接

| 功能 | 连接 |
| --- | --- |
| ST7796U2 SPI1 | CS=PD10、SCK=PD11、MOSI=PD12、DC=PD14、RESET=PD15、背光=PD16 |
| FT5x06 触摸 | TWI2，地址 `0x38`，SDA=PE13、SCL=PE12、RESET=PE0、INT=PE1 |
| TWI3 传感器总线 | SCL=PE6（J3 第 11 脚），SDA=PE7（J3 第 10 脚），3.3 V 接第 1/17 脚，GND 接第 9/14/20 脚 |
| 传感器地址 | SHT40 `0x44`，BH1750 `0x23`（回退 `0x5c`），SGP30 `0x58` |
| DMIC | 使用 R528 板载 DMIC 引脚，vendor 补丁包含 DshanPi 的 PD18 数据线修正 |

板级配置将 TWI3 设置为 100 kHz，TWI2 保持 FT5x06 兼容速率。外接模块信号均为
3.3 V，接线前请以手头板卡丝印和原理图为准。

## 复现编译

编译主机使用 Ubuntu 20.04 或更高版本，并准备 openvela 编译依赖、`repo` 和
ARM 工具链。完整依赖列表见仓库根目录文档。

新建工作区并同步源码：

```bash
mkdir openvela-workspace
cd openvela-workspace
repo init -u https://github.com/open-vela/contest2026_113_veladuxingke \
  -b dev-ai-contest-2026 -m contest2026_113_veladuxingke.xml
repo sync -c -j8
```

同步后，`contest2026_113_veladuxingke/`、`nuttx/`、`apps/` 和 `vendor/` 位于
同一层级。从工作区根目录执行仓内补丁脚本：

```bash
./contest2026_113_veladuxingke/tools/apply_vendor_patches.sh
```

如果是从旧模板升级的已有工作区，首次编译前清理旧模板软链接和生成的应用
Kconfig：

```bash
if [ -L packages/demos/contest2026_113_hello_app ]; then
  unlink packages/demos/contest2026_113_hello_app
fi
if [ -L packages/demos/contest2026_113_hello_world ]; then
  unlink packages/demos/contest2026_113_hello_world
fi
rm -f packages/demos/Kconfig packages/demos/.kconfig
```

脚本会先验证 vendor 的 Micro-TF 源码链，再以可重复方式应用 NuttX、NxPlayer、
TWI3、DMIC、GPIO、音频和 Codec 补丁。脚本可重复执行；版本不匹配时会停止并
保留现场。

编译最终 `nsh` 配置：

```bash
source build/envsetup.sh
./build.sh contest2026_113_veladuxingke/board/r528s3-dshanpi/configs/nsh/ -j8
```

切换配置或需要全量重建时：

```bash
./build.sh contest2026_113_veladuxingke/board/r528s3-dshanpi/configs/nsh/ -j8 distclean
./build.sh contest2026_113_veladuxingke/board/r528s3-dshanpi/configs/nsh/ -j8
```

## 打包与烧录

编译成功后生成 NAND 镜像：

```bash
# 必须先在 openvela 根目录加载通用环境。
cd /path/to/openvela
source build/envsetup.sh

cd vendor/allwinnertech/lichee
source envsetup.sh
lunch_nuttx r528s3-dshanpi
pack
```

不要在 `source envsetup.sh` 后再输入 `bash`；`lunch_nuttx` 和 `pack` 是当前
Bash 中定义的函数。若看到 `prebuilts/kconfig-frontends` 不存在，说明加载顺序
不对：回到工作区根目录，先执行 `source build/envsetup.sh`，再执行 lichee 的
`source envsetup.sh`，并用 `type lunch_nuttx`、`type pack` 确认两者存在。

目标文件为：

```text
vendor/allwinnertech/lichee/out/r528s3/dshanpi_nand/rtos_nuttx_r528s3-dshanpi_uart0_256Mnand.img
```

只有在打包日志同时出现 `Dragon execute image.cfg SUCCESS !` 和 `pack finish`，
且镜像时间戳、SHA-256 与本次构建一致时，才视为打包成功。镜像是工作区输出，
不属于本参赛源码仓提交内容。

烧录步骤：在 PhoenixSuit 中选择 `.img`；开发板断电，按住 FEL 键并通过 USB
连接、上电；开始升级，完成后断电重启。首次启动时 ROMFS 会自动启动
`wifi_manager`、`lan_panel` 和 `routine_mgr`，正常使用不需要在 NSH 输入命令。

调试串口参数为 **1500000 8N1**，配置文件中的控制台为 `UART3`；实际插座请按
板卡版本丝印确认。

## 相关文件

- [`configs/nsh/defconfig`](configs/nsh/defconfig) — 最终功能和串口配置；
- [`src/etc/init.d/rcS`](src/etc/init.d/rcS) — ROMFS 启动脚本和服务顺序；
- [`../../tools/apply_vendor_patches.sh`](../../tools/apply_vendor_patches.sh) —
  补丁应用入口；
- [`../../docs/编译指南.md`](../../docs/编译指南.md) — 中文编译与验收清单。

本目录文件遵循各文件头部声明的许可协议。
