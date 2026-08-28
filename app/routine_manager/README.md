# routine_manager

`routine_mgr` 是比赛项目的 LVGL 桌面应用。它使用 R528 的 `/dev/fb0` 显示 480×320 界面，通过 `/dev/input0` 接收触摸，并从 TWI3 获取实时环境数据。语音链路从板载 DMIC `/dev/audio/pcm1c` 录音、经内部 Codec `/dev/audio/pcm0p` 播放；当 `CONFIG_ROUTINE_MANAGER_KWS=y` 时，正常查询路径使用全 INT8 TFLite Micro KWS。当前产品支持温度、湿度、光照和空气质量四类查询；模型仍是面向已采集说话人的个人原型，不是通用中文 ASR。

## 模块

- `routine_manager_main.c`：参数解析、LVGL/NuttX 初始化、480×320 检查、事件循环和清理。
- `routine_model.c`：固定大小、无动态内存的环境数据和可操作日程模型。
- `routine_ui.c`：状态卡、24 小时折线、日程操作、传感器详情和点击开始/停止语音视图。
- `audio_service.c`：后台录音、取消、回复拼接和播放状态机，不阻塞 LVGL。
- `voice_recognizer_kws.cc` / `kws_frontend.cc`：录音对齐、固定点特征、TFLite Micro 推理和四类环境查询判定。
- `voice_recognizer_demo.c`：仅作为未启用 KWS 配置时的 PCM 有效人声回退检测。
- `kws_capture.c`：独立早期退出的 R528 DMIC 中文 KWS 语料采集模式；不进入 LVGL 或正常识别路径，录音后通过 Codec 自动回放供人工试听。
- `voice_reply.c`：按温度、湿度、光照、eCO₂ 和 TVOC 数值拼接中文播报片段，并生成高低温告警和日程提醒。
- `Kconfig`、`Make.defs`、`Makefile`、`CMakeLists.txt`：OpenVela 构建集成。

## 数据接口

UI 只读取 `struct routine_snapshot_s`。后续接入 SHT30/SHTC3、SCD40/SCD41、BH1750 或 GPADC 时，应新增传感器数据提供层并填充同一快照，不把 I2C/GPADC 访问写进 `routine_ui.c`。

温度采用 0.1°C 定点数，例如 `246` 表示 `24.6°C`。趋势固定为 24 点，日程最多 3 条，避免小屏和堆内存压力。

## 字体

中文是默认界面，使用板级 ROMFS 中的：

```text
/etc/fonts/font_puhui_20_4.bin
```

LVGL POSIX 文件系统会把它转换为类似 `/:/etc/fonts/...` 的驱动路径。中文模式下字体加载失败会明确报错并退出，避免显示方框或悄悄切换语言。英文模式使用 LVGL 内置 Montserrat 字体，不访问外部中文字体。

## 运行参数

```text
routine_mgr [--demo] [--chinese | --english | --ascii]
            [--font PATH] [--rate 1..60]
            [--duration 1..86400] [--selftest] [--help]
```

- 不指定语言参数：默认使用中文界面。
- `--chinese`：显式选择中文界面。
- `--english`：选择英文界面和内置字体。
- `--ascii`：`--english` 的兼容别名，不会修改 UTF-8 编码配置。
- `--font PATH`：覆盖中文模式使用的 LVGL 二进制字体路径。
- `--rate N`：N 个真实秒代表一个模拟小时。
- `--duration N`：用于自动退出、重复启动和内存检查。
- `--selftest`：不初始化 LVGL，也不访问 `/dev/fb0`；验证 operation/reminder 调度契约，但不能代替真机声学验收。

互相冲突的语言参数会被拒绝，例如 `--chinese --english`。

## 中文 KWS 语料采集

当前开发阶段新增仓库外真实语料采集入口：

当前板上 `CONFIG_NSH_LINELEN=64`，不能用 `\` 做多行续写，长命令还会被截断。使用 59 字符的单行短命令：

```text
routine_mgr -K -l query_temperature -p s001 -s smoke_a -n 1
```

`-K/-l/-p/-s/-i/-n/-m` 分别对应采集、label、speaker、session、device、count 和 record-ms；默认 device=`r528a`、count=20、record-ms=3500，可省略默认项。该模式在 `lv_init()` 前分流，只使用 `/dev/audio/pcm1c`，等待 FAT32 micro-SD 挂载到 `/sdcard` 后，把 16 kHz/16-bit/mono PCM 和 JSON sidecar 保存到 `/sdcard/kws_corpus/<session>/`。没有 SD 卡或挂载未就绪时会限时失败，不会回退到 NAND 的 `/data`。由于板端 `CONFIG_NAME_MAX=32`，文件名采用 `k<16位上下文哈希>_<qt/qh/ql/qa/wv/us/bg>_<4位take>.pcm/.json`，完整 device/speaker/session/label 仍在 sidecar；不再使用会超过 NAME_MAX 的长字段拼接名。每条录音完成并计算指标后，会从 `/dev/audio/pcm0p` 自动回放临时 PCM；用户试听后再选择保留、重录或退出。正式目标存在时会拒绝发布；临时 PCM 与 JSON 通过同一 FAT 文件系统内 rename 发布，JSON 失败时回滚 PCM。完成采集后先执行 `sync`，再卸载或正常关机，取出 SD 卡即可用 Windows 读卡器直接复制 `.pcm/.json`；不要在录音、回放或保存时拔卡，也不要导入隐藏的 `.p*`/`.j*` 临时文件。然后运行 `tools/import_kws_corpus.py` 验证并生成 WAV、声学指标和确定性 manifest。

MiMo-V2.5-ASR 仅作为用户明确选择录音后的可选主机转写基线，不进入固件；R528 正常路径已使用小型全 INT8 TFLite Micro KWS。完整状态、隐私要求和真机步骤见 [`docs/中文离线语音识别开发与验收记录.md`](../../docs/中文离线语音识别开发与验收记录.md)。

## UI 约束

- 目标分辨率固定为 480×320 RGB565 横屏；检测到其他分辨率会输出诊断并退出。
- 打开 `/dev/input0` 接收触摸输入；核心数据仍在同一页面持续显示。
- 点击日程可选择完成、延后 10 分钟或取消；完成态只在本次进程中保存，应用重启后恢复默认日程。
- 点击顶部状态、温湿度/空气质量卡或光照区域可查看各传感器的实时、演示、预热和过期详情。
- 顶部“曲线”按钮弹出运行曲线操作，可查看温度趋势、切换5秒、10秒、1分钟、5分钟、10分钟、30分钟或60分钟记录间隔，或导出全部历史。
- 导出会自动尝试把 FAT32 `/dev/mmcsd1` 挂载到 `/sdcard`，不需要先通过串口执行 `mount`。
- 点击 Wi-Fi 页面的连接后留在当前页面；取得 IPv4 后弹出 SSID、IP、网页地址和4位令牌，以后重新进入 Wi-Fi 页面也可再次查看。
- 趋势图为单系列、单轴、2 px 折线，只有末端值直接标注。
- badge 始终明确显示演示、实时、混合、预热或过期状态。

## TWI3 环境传感器

三块外接模块并联到 `/dev/i2c3`：

| 模块 | 地址 | 数据 |
|---|---:|---|
| SHT40 | `0x44` | 温度×100、湿度×100，双 CRC8 |
| GY-302/BH1750 | `0x23`，回退 `0x5c` | 照度×10 |
| SGP30 | `0x58` | eCO2 ppm、TVOC ppb，双 CRC8 |

J3 接线：PE6/SCL=11 脚，PE7/SDA=10 脚，3.3V=1 或 17 脚，GND=9、14 或 20 脚。PE6/PE7 由 R528 TWI HAL 配置为 mux 4，不直接写 `PE_CFG0`。TWI3 实际工作频率为 100 kHz；TWI2 继续以 400 kHz 服务 FT5x06。

默认运行和 `--demo` 都会启动环境采集服务；`--demo` 只加速日程时钟，不禁用真实传感器。界面状态包括演示、实时、混合、预热和过期。传感器瞬时失败保留最后一次有效值，单个模块断开不会停止其他模块或 UI。

## 语音交互

- 点击“开始语音”后以 16 kHz、16-bit、单声道从 `/dev/audio/pcm1c` 录制约 3.5 秒；活动期间按钮变为“停止语音”。
- 录音、分析、中文片段拼接和 `/dev/audio/pcm0p` 播放都在独立线程执行；环境卡和触摸主循环继续刷新。
- 启用 KWS 时，温度、湿度、光照和空气质量类均要求成为输出 argmax 且 raw INT8 分数达到各自阈值，随后读取对应传感器并播放中文回复。
- 当前模型还保留“你好 Vela”唤醒类别用于语料和评估；最终产品流程仍采用点击“开始语音”触发一次录音，不自动启用唤醒词，也不会把唤醒类别发送到查询路由。
- 录音、识别和播报每次均创建独立的一次性操作并在完成后释放资源，支持连续点击查询。
- ROMFS 资源目录为 `/etc/routine_voice`。`tools/generate_mimo_routine_voice.sh` 使用 Xiaomi MiMo `mimo-v2.5-tts` 和“冰糖”音色生成固定母版，`tools/prepare_voice_assets.py` 负责 WAV 校验、mastering v2、16 kHz dual-mono PCM 输出及来源/处理/哈希清单；API key 只在生成时安全输入，不进入仓库或固件。资源缺失或损坏时应用会明确显示“播报资源不可用”。

## 真机验收状态

截至 2026-08-11 已验证：

- 默认命令 `routine_mgr --duration 30` 可显示完整中文界面；
- `/dev/fb0` 为 480×320 RGB565 横屏；
- `/dev/input0` 可正常打开，触摸 DOWN/MOVE/UP 与四角坐标方向正确；
- `--english` 和兼容参数 `--ascii` 可选择英文界面；
- SHT40 `0x44`、BH1750 `0x23`、SGP30 `0x58` 均在 TWI3 应答；
- 屏幕实时值可动态变化；单模块拔出后对应值变“过期”，其他模块继续更新，重新插入后无需重启即可恢复；
- 相同故障不会持续刷日志；
- 已完成 3 次 `routine_mgr --duration 30` 冒烟测试，前后任务列表一致，`nused` 均为 918，未发现短周期残留或持续内存增长；
- 2026-08-11 用户确认最终 MiMo/mastering v2 镜像的 `routine_mgr` 默认 100% 温度播报正常；“点”“五”以及从“`三，七，九`”提取后重新打包的“七”发音正确；
- lifecycle 镜像（NAND `6df8abca...64e8f`）和首版 W1C-safe 镜像（`1a51094f...60aa8f`）均已烧录并复现连续无声；最终状态确认镜像（`3344c2a2...61fcd5`）加入显式 rise/fall W1C acknowledgement 和 non-A `RMCEN/RDEN` 时序后，用户确认传感器 → reminder → 延后 reminder → 后续传感器均有声，缺陷已修复。

TWI3 在软件中配置为 100 kHz，SGP30 使用约 1 Hz 的软件调度；由于没有逻辑分析仪，二者没有物理波形结论。10 次冷启动、50 次连续语音交互、20 轮音频压力切换和 2–8 小时长时间压力测试未在本次确认范围内。2026-08-10 坐标修复固件已完成真机交互验收；2026-08-12 连续播放无声缺陷已完成实际复现路径验收。中文 KWS 语料采集、导入、训练和全 INT8 TFLite Micro 集成均已完成。2026-08-27 的起始时刻鲁棒温度模型已通过 149 项主机测试并打包为 NAND `af499236...776404`，但该镜像尚未烧录；编译、打包和主机时序测试通过不等同于真机重复识别已验收。
