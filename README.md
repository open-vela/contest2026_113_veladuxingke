# Vela 智能桌面管家

## 一、作品简介

Vela 智能桌面管家是一套基于 openvela 和百问网 R528S3-DshanPi 的离线智能桌面终端。它通过 SHT40、BH1750 和 SGP30 采集温度、湿度、光照、eCO₂ 与 TVOC，在 480×320 触摸屏上实时展示，并提供离线固定指令识别、中文语音播报、高低温告警、今日日程提醒、智能照明和局域网管理。

主要特点：

- 点击屏幕“开始语音”后，可识别“现在温度多少”、“现在湿度多少”、“现在光照多少”和“现在空气质量怎么样”，并播报实时传感器数据。
- 采用全 INT8 TFLite Micro 语音指令分类模型，识别和推理均在 R528 本地完成，不依赖云端语音服务。当前模型是面向已采集说话人的固定指令原型，不是通用中文 ASR，且本版未启用唤醒词。
- 温度越界时自动播报高温或低温提醒；阈值可在浏览器中修改。
- 可在浏览器中编辑 3 条今日日程，到时在屏幕弹窗并播报当前时分秒和目标到期提醒。
- 连接 Wi-Fi 后自动通过 NTP 校时；断网后依靠系统时钟继续走时。
- 屏幕可扫描并配置 Wi-Fi，连接成功后显示设备 IP、网页地址和 4 位访问令牌，脱离串口也可独立使用。
- 网页可查看实时值、日期时间表格、运行曲线和每个采样点，支持 5 秒至 1 小时记录间隔，并可将历史数据导出到 FAT32 microSD 卡。
- `wifi_manager`、`lan_panel` 和 `routine_mgr` 均在开机时自动启动。

## 二、选题方向

**AI 硬件产品创新。**

作品将 R528 端侧 AI 语音推理与环境传感器、触摸显示、音频播报、Wi-Fi、NTP、局域网网页和 SD 卡数据管理整合为一台可独立运行的硬件产品，解决桌面环境查询、异常提醒和日程管理需要多个设备的问题。

## 三、目录结构

- `app/routine_manager/` — LVGL 桌面应用，包含环境采集、日程与告警模型、语音录制/识别/播报、历史记录和触摸 UI。
- `app/wifi_manager/` — Wi-Fi 配置、自动连接、DHCP、断线重试、状态发布与 NTP 校时服务。
- `app/lan_panel/` — 设备端低资源 HTTP 服务和内置网页，用于环境数据、曲线、历史、日程、温度阈值、智能照明和 SD 导出管理。
- `app/st7796_test/` — ST7796U2 屏幕色块、方向和帧缓冲调试程序。
- `board/r528s3-dshanpi/` — R528S3-DshanPi 板级配置、ST7796U2/FT5x06 初始化、启动脚本、中文字体和语音 PCM 资源。
- `patches/` — 对 NuttX、NxPlayer 和 Allwinner R528 音频、DMIC、TWI3 及 GPIO 的可复现补丁。
- `source_assets/routine_voice/` — 中文语音 WAV 母版及生成元数据；API Key 不在仓库中。
- `tools/` — 补丁应用、语音资源处理、KWS 语料导入/训练/模型转换与主机端合同测试工具。
- `docs/` — 硬件接线、编译、触摸、显示、传感器、音频、语音和网络的开发与验收记录；另有 [`快速上手与自定义语音模型`](docs/快速上手与自定义语音模型.md) 供首次使用者和自定义说话人模型使用。
- `logs/` — 按日期归档的完整 AI Coding 对话日志及清单。
- `contest2026_113_veladuxingke.xml` — 参赛仓库 manifest 和应用映射配置。

## 四、运行方式

### 1. 硬件准备

- 百问网 R528S3-DshanPi（256 MiB NAND 版）、3.5 寸 ST7796U2 触摸屏和可用的板载 DMIC/音频输出。
- SHT40（`0x44`）、GY-302/BH1750（默认 `0x23`）、SGP30（`0x58`）各 1 个，并联到 J3 的 TWI3：PE6/SCL=第 11 脚，PE7/SDA=第 10 脚，3.3 V=第 1 或 17 脚，GND=第 9、14 或 20 脚。
- 可选 FAT32 microSD 卡，用于导出历史数据。
- Ubuntu 20.04 或更高版本的编译主机。如未搭建 openvela 环境，先按 [`board/r528s3-dshanpi/README_zh-cn.md`](board/r528s3-dshanpi/README_zh-cn.md) 安装依赖和 `repo`。

### 2. 拉取工程

```bash
mkdir openvela-workspace
cd openvela-workspace

repo init -u https://github.com/open-vela/contest2026_113_veladuxingke \
  -b dev-ai-contest-2026 -m contest2026_113_veladuxingke.xml
repo sync -c -j8
```

同步后，本仓位于 `contest2026_113_veladuxingke/`，其余 openvela 子仓位于当前工作区根目录。

如果这是从旧模板升级的已有工作区，先清理旧模板留下的软链接和生成文件，再开始编译：

```bash
cd /path/to/openvela
if [ -L packages/demos/contest2026_113_hello_app ]; then
  unlink packages/demos/contest2026_113_hello_app
fi
if [ -L packages/demos/contest2026_113_hello_world ]; then
  unlink packages/demos/contest2026_113_hello_world
fi
rm -f packages/demos/Kconfig packages/demos/.kconfig
```

这两个 `hello_*` 链接属于旧模板，不能手动改成普通目录；`Kconfig` 和
`.kconfig` 会在下一次应用预配置时按当前四个应用重新生成。

### 3. 应用必需补丁

在 openvela 工作区根目录执行：

```bash
./contest2026_113_veladuxingke/tools/apply_vendor_patches.sh
```

该脚本可重复执行：已应用的补丁会被检测并跳过，补丁不匹配时会停止并报错。

### 4. 编译 NuttX

仍在 openvela 工作区根目录执行：

```bash
source build/envsetup.sh
./build.sh contest2026_113_veladuxingke/board/r528s3-dshanpi/configs/nsh/ -j8
```

切换过板级配置或需要全量重建时，先执行：

```bash
./build.sh contest2026_113_veladuxingke/board/r528s3-dshanpi/configs/nsh/ -j8 distclean
```

然后重新执行上述编译命令。

### 5. 生成 NAND 烧录镜像

```bash
# 必须在 openvela 工作区根目录先加载通用环境；它会加入
# prebuilts/build-tools/linux-x86_64/bin，供 lichee 的打包脚本使用。
cd /path/to/openvela
source build/envsetup.sh

cd vendor/allwinnertech/lichee
source envsetup.sh
lunch_nuttx r528s3-dshanpi
pack
```

不要在 `source envsetup.sh` 后再输入 `bash`。`lunch_nuttx` 和 `pack` 是当前
shell 中定义的函数，进入子 shell 会丢失它们。若 lichee 环境提示找不到
`prebuilts/kconfig-frontends`，不要继续执行 `lunch_nuttx`；回到工作区根目录，
按上面的顺序重新加载 `build/envsetup.sh`。

生成文件：

```text
vendor/allwinnertech/lichee/out/r528s3/dshanpi_nand/rtos_nuttx_r528s3-dshanpi_uart0_256Mnand.img
```

当前打包脚本可能因 DshanPi 专用 `data/res` 缺失并回退到公共资源而返回 1。只有日志中同时出现以下两行，且上述镜像的修改时间为本次打包时间，才视为生成成功：

```text
Dragon execute image.cfg SUCCESS !
pack finish
```

### 6. 烧录

1. 在 Windows 上打开 PhoenixSuit，选择上述 `.img` 镜像。
2. 开发板断电，按住 FEL 键后通过 USB 连接并上电，进入烧录模式。
3. 在 PhoenixSuit 中开始升级，等待烧录完成后断电重启。

串口不是正常使用的必需条件。需要调试时按板级配置连接调试串口，参数为
`1500000 8N1`（配置文件中的 UART3 控制台；具体插座以板卡丝印为准）。

### 7. 首次使用与验收

第一次拿到板子或需要为自己的声音训练模型时，先阅读
[`快速上手与自定义语音模型`](docs/快速上手与自定义语音模型.md)。

1. 开机后等待桌面界面出现；无需在 NSH 中手动启动程序。
2. 确认 SHT40、BH1750 和 SGP30 状态转为“实时”。SGP30 上电后需要短暂预热，预热期间空气质量语音查询会提示数据暂不可用。
3. 点击顶部 Wi-Fi 图标，扫描网络、选择 SSID、输入密码并点击“连接”。界面保留在当前页，联网成功后弹窗显示 IP、`http://<IP>:8080/` 和 4 位令牌，时间会自动同步。
4. 在同一局域网的电脑或手机打开弹窗中的网址，输入 4 位令牌。可修改高低温阈值和日程、调整记录间隔、加载历史、查看每个曲线点或导出数据。
5. 点击屏幕“开始语音”，在录音时间窗内清晰说出下列任一指令：

   ```text
   现在温度多少
   现在湿度多少
   现在光照多少
   现在空气质量怎么样
   ```

   每次查询前都需要再点击一次“开始语音”，本版本不启用唤醒词。

6. 要验证历史导出，插入 FAT32 microSD 卡，在网页“运行曲线”弹窗中选择“导出到 SD 卡”。文件保存到：

   ```text
   /sdcard/routine_history/routine-history-<epoch>.jsonl
   ```

如烧录新版本后浏览器仍显示旧页面，请清理该设备地址的缓存，或用无痕窗口重新打开。

### 8. 提交前验证范围

- 主机端合同测试：177 项通过，6 项按环境条件跳过。测试依赖位于仓库外的
  `/tmp/r528-kws-site`，不会被打包进提交内容。
- `contest2026_113_veladuxingke.xml` 可解析，manifest 中列出的日志路径均已核对存在，JSONL 日志和 manifest 均可解析。
- 本轮提交整理未重新执行完整 NuttX 编译、NAND 打包或烧录；`docs/` 和 `logs/` 中的构建、烧录及真机结果是开发期间的历史记录，不等同于本轮重新验收。

## 五、AI Coding 使用说明

本作品在需求拆解、架构设计、编码、日志分析、测试和文档整理中持续使用 AI Coding：

- **需求与架构：** AI 将“传感器 + 语音 + 触摸屏 + 日程 + 网页”拆分为环境服务、数据模型、LVGL UI、音频状态机、Wi-Fi/NTP 服务和 HTTP 面板，帮助明确模块边界、错误状态与并发约束。
- **编码实现：** AI 协助完成 R528 TWI3 传感器访问、ST7796U2/FT5x06 适配、本地 KWS 特征与 TFLite Micro 集成、语音片段组合、安全配置写入、局域网 API 和可视化页面。
- **调试定位：** 通过串口日志与源码对照，AI 协助定位过触摸坐标方向、DMIC 引脚、音频工作线程生命周期、Codec RAMP W1C 时序、语音只能识别一次、Wi-Fi 字体图标和浏览器偶发 `NetworkError` 等跨层问题。
- **质量保证：** AI 根据缺陷补充边界检查、原子文件更新、HTTP 超时/重试/并发处理和主机端合同测试；开发期间的构建、打包与真机结果按日期归档，便于复核。
- **文档与可追溯性：** AI 将接线、构建、真机现象、修复过程和已知边界持续归档到 `docs/`，便于审查和复现。

AI 显著减少了跨驱动、音频、LVGL、网络和模型工具链问题的往返定位时间，并通过可重复测试和边界检查提高了实现质量。AI 输出的修改均经过源码审查和相应的主机或硬件验证；本轮提交前的实际验证范围以“提交前验证范围”一节为准，不以 AI 推断代替硬件验收。完整对话日志见 [`logs/`](logs/)。
