# Vela 智能桌面管家 ai_agent 硬件验收记录

## 1. 场景说明

办公室或家庭书桌旁的用户希望用一句话知道当前桌面环境和今天的提醒，不必打开手机上的多个应用，也希望通过同一个助手直接调整固定日程。Vela R528S3-DshanPi 采集 SHT40、BH1750、SGP30 的温度、湿度、光照、eCO2 和 TVOC，`routine_mgr` 将实时状态和日程原子发布到 `/data/routine/status.json`；`ai_agent` 通过 LLM 理解中文请求，读取状态，并把明确的日程修改写回 routine 模板。设备语音播报由 `routine_mgr` 和 `voice_tts` 独立完成，不提供在线音乐能力。

典型用户故事：

> 用户坐在书桌前，通过串口 CLI 或局域网 WebSocket 发送“读取当前桌面环境，并告诉我今天未完成的提醒”。Agent 读取时间和状态文件，区分实时、预热、过期、离线数据，再返回传感器数值和未完成日程。

## 2. 要求对照

| 创新要求 | 当前实现 | 证据或验收方式 |
| --- | --- | --- |
| Agent 在硬件上运行 | `ai_agent` 已加入 R528S3-DshanPi defconfig，程序名为 `ai_agent` | 编译生成 `nuttx.bin`；烧录后在 NSH 执行 `ai_agent` |
| 编译 openvela + ai_agent 固件 | 已完成 | `tools/apply_vendor_patches.sh`、`build.sh` 和 `pack` 命令见第 5 节 |
| 配置 LLM 后端并进行基础对话 | 本项目验收默认使用小米 MiMo；CLI 支持 `set_llm mimo` 和 `ask`，配置即时写入 `/data/agent/config/config.json` | 烧录后执行第 4 节命令；API Key 只在设备运行时输入，不写入仓库 |
| 至少一个交互渠道 | CLI 已启用；WebSocket 服务监听 `28789` | `ai_agent` 启动后出现 `vela>` 提示符；局域网客户端连接 `ws://<IP>:28789` |
| 至少一个自定义 Skill | `vela-desk.md` 已加入源码，并注册为版本化内置 Skill（v3）；首次启动写入，保留旧数据分区时自动升级并清理已下线的音乐 Skill | 启动日志出现 `Installed built-in skill`、`Upgrading built-in skill` 或 `Removed retired built-in skill`；在 NSH 用 `cat /data/agent/skills/vela-desk.md` 检查 `v3` 标记，并确认已无 `music-dj.md` |
| Skill 使用演示 | `vela-desk` 读取状态并调用日程模板更新工具；语音播报仍由设备本地音频链路负责 | 执行第 4 节的环境查询和日程修改示例 |
| 主动+执行场景 | `routine_mgr` 到期主动弹窗/播报；Agent 可读取环境并执行每日模板修改 | 观察到期提醒，并完成第 4 节两条 `ask` 验收 |
| 完整应用场景、功能清单、技术实现 | 本文第 1、3、4 节 | 可随提交材料复核 |

软件交付项已落地；本工作区当前没有检测到 `/dev/ttyUSB*`、`/dev/ttyACM*` 或 ADB 设备，因此本轮不能把该镜像的真实烧录、联网 LLM 对话和 Skill 输出写成已完成的真机结果。连接目标板后按第 4 节执行即可完成最后一项硬件验收。

## 3. 功能清单

- `routine_mgr` 的环境状态文件只读查询：温度、湿度、光照、eCO2、TVOC。
- 通过 `live_mask`、`warmup_mask`、`stale_mask`、`offline_mask` 说明数据有效性，不把无效值描述为当前值。
- 读取 `clock_valid` 和 `schedule`，列出当天未完成的提醒。
- `routine_schedule_update` 原子修改 ID 0-2 的时间和文本，写入后由 `routine_mgr` 每日重载。
- CLI 对话、会话管理、内存和定时任务能力。
- `set_wifi` 保存 Wi-Fi 配置并立即尝试连接；网络线程不会阻塞本地 Agent 启动。
- `set_llm` 支持 `kimi`、`qwen`、`deepseek`、`glm`、`openai`、`openrouter`、`mimo` 预设，也支持自定义 OpenAI 兼容端点。
- WebSocket 网关用于局域网客户端，默认 TCP 端口 `28789`。
- Skill 文件热加载；设备端可通过 `install_skill <name> <https-url>` 扩展其他 Markdown Skill。
- `run_shell` 仅允许安全命令和 `/data/agent/`、`/data/routine/`、`/proc/`、`/tmp/` 路径；`vela-desk` 对 routine 数据只执行 `cat`。

## 4. 设备端演示

以下命令在烧录后的 NSH 中执行。尖括号内容替换为实际值，API Key 不要粘贴到日志或提交文件。
Cron 降噪和缓存修复只存在于第 5 节所列的新镜像；旧镜像即使重启
`ai_agent` 仍会每 10 秒打印 `Check job[...]`，必须重新烧录。

```text
nsh> ai_agent
vela> config_show
vela> set_wifi <SSID> <PASSWORD>
vela> set_llm mimo <MIMO_API_KEY>
vela> ask 读取当前桌面环境，并告诉我今天未完成的提醒
```

`mimo` 预设使用 `mimo-v2.5` 和 `/v1/chat/completions`。`tp-` Key 自动选择
`token-plan-cn.xiaomimimo.com:443`，其他 Key 选择 `api.xiaomimimo.com:443`。设备按 MiMo 官方协议发送 `api-key` 请求头，文本/Skill 请求显式设置
`thinking=disabled`、`temperature=1.0`、`top_p=0.95` 和
`max_completion_tokens=32768`。需要切换 Pro 模型时执行
`router_model 0 mimo-v2.5-pro`；不要把密钥写进串口截图、日志或仓库。

2026-06-30 下线的 `mimo-v2-flash`、`mimo-v2-omni` 和
`mimo-v2-pro` 不再用于本项目预设。如果升级时保留了 `/data/agent/config/`，
固件会仅针对官方 `xiaomimimo.com` 端点自动迁移主 LLM、Router slot 0-3 和视觉模型的旧模型名，并依据 Key 是否以 `tp-` 开头修正持久化 Base URL；API Key 保持不变。日志应出现 `Migrated retired MiMo model`、`Migrated backend ... MiMo model` 或 `Selected ... MiMo endpoint`。

预期流程：

1. 出现 `vela>` 提示符，日志包含 `AI Agent ready` 和 `agent_loop_start (rc=0)`。
2. 首次运行创建 `/data/agent/skills/`，并安装或升级带 `v3` 标记的 `vela-desk.md`；若旧分区存在 `music-dj.md`、`sleep-music.md` 或 `tts-speak.md`，启动日志应显示清理结果且目录中不再有这些文件。
3. `ask` 触发 `get_current_time` 和 `run_shell`，后者读取 `/data/routine/status.json`。
4. 回复列出带有效性标签的传感器值和 `completed=0` 的当天日程。若 `clock_valid=0` 或传感器处于预热/离线，回复应明确提示。

每日模板修改演示：

```text
vela> ask 把喝水改到15:30，事项改成休息
```

正常日志顺序应先出现读取 `/data/routine/status.json`，再出现
`Tool call: routine_schedule_update`。随后检查：

```text
vela> quit
nsh> cat /data/routine/config.json
nsh> cat /data/routine/status.json
```

`config.json` 中 ID 0 的 `hour=15`、`minute=30`、`text="休息"` 且
`generation` 增加；约一个 `routine_mgr` 刷新周期后，`status.json` 同步显示新模板。
该修改对每天重复生效，只能更新现有三个槽位，不会新增或删除事项。

运行时回归测试以串口日志为准，不以 Agent 对自身行为的描述为准：

```text
vela> ask 请使用 cron_list 查询当前定时任务，并原样列出
vela> ask 请使用 cron_list 查询当前定时任务，并原样列出
```

两次请求都应分别出现 `Tool call: cron_list`，均不应出现
`Cache hit, skipping LLM call`。`ask 需要`、`ask ok`、`ask 好的` 等依赖上一轮
语境的确认词也不得命中缓存；只有纯问候、致谢或告别允许命中，例如连续两次
`ask 你好` 时第二次可以出现 Cache hit。等待至少 30 秒，串口不应再出现
`[cron] Check job[...]`；实际到期时的 `Cron job firing` 日志仍会保留。

需要删除任务时使用完整、无歧义的请求：

```text
vela> ask 请先使用 cron_list 查询，再删除名称为每日喝水提醒的任务；找不到就明确说没有删除
```

正确日志顺序是先 `Tool call: cron_list`，再使用本轮返回的 8 位 ID 调用
`cron_remove`。若 ID 不存在，最终回复必须以 `ERROR:` 开头并包含
`no job was removed`，不能声称删除成功。

局域网 WebSocket 纯文本可直接在浏览器开发者控制台验证：

```javascript
const ws = new WebSocket("ws://<DEVICE_IP>:28789");
ws.onmessage = (event) => console.log(event.data);
ws.onopen = () => ws.send("读取当前桌面环境，并告诉我今天未完成的提醒");
```

服务端应打印 `WS plain-text frame` 并返回 JSON 响应，不应再打印
`Invalid JSON from fd`。也可把发送内容换成 JSON 字符串或
`{"type":"message","content":"需要","chat_id":"desktop"}` 信封。

若 `tp-` Key 的日志显示请求了 `api.xiaomimimo.com` 并返回 `401`，说明固件仍在使用错误的按量 Base URL；升级新镜像，或在旧镜像上执行 `set_llm token-plan-cn.xiaomimimo.com mimo-v2.5 <TP_KEY>`。若已请求 `token-plan-cn.xiaomimimo.com` 仍返回 `401 Invalid API Key`，则凭证本身未通过 MiMo 验证。在 `vela>` 中不能直接输入“你好”，必须使用 `ask 你好`，否则会被 NSH 当作未知命令。

MiMo 云端 Web Search 是控制台需单独开通的联网插件，不是本项目 `vela-desk` Markdown Skill 的组成部分。当前验收通过 MiMo Function Calling 调用本地 `get_current_time` 和 `run_shell`，无需开通云端搜索。

也可以从同一局域网的 WebSocket 客户端连接 `ws://<DEVICE_IP>:28789`，发送 Agent 支持的文本消息；服务端同时接受纯文本帧、JSON 字符串和
`{"type":"message","content":"...","chat_id":"..."}` 信封，回复通过同一连接返回。CLI 是本版本的最小必需渠道，WebSocket 用于后续网页或手机端接入。

## 5. 构建、打包与烧录

在 openvela 工作区根目录执行：

```bash
./contest2026_113_veladuxingke/tools/apply_vendor_patches.sh
source build/envsetup.sh
./build.sh contest2026_113_veladuxingke/board/r528s3-dshanpi/configs/nsh/ -j8

cd vendor/allwinnertech/lichee
source envsetup.sh
lunch_nuttx r528s3-dshanpi
pack
```

只有打包日志同时出现 `Dragon execute image.cfg SUCCESS !` 和 `pack finish`，才把以下镜像视为有效：

```text
vendor/allwinnertech/lichee/out/r528s3/dshanpi_nand/rtos_nuttx_r528s3-dshanpi_uart0_256Mnand.img
```

PhoenixSuit 烧录步骤：选择上述镜像；开发板断电，按住 FEL 键并连接 USB、上电；开始升级；完成后断电重启。串口调试参数为 `1500000 8N1`，具体插座以板卡丝印为准。

本轮最终候选主机验证（2026-08-29 22:32，已移除 Agent 在线音乐能力）：`build.sh` 返回 0，`nuttx/nuttx.bin` 大小为 10,605,288 字节，SHA-256 为 `8a27c63a03793ca2a12d5595e84601d1c6bea54b95fc6cf255986f45ecfeb76b`。随后重新执行 `pack`，日志同时包含 `Dragon execute image.cfg SUCCESS !` 和 `pack finish`；生成镜像大小为 26,739,712 字节，SHA-256 为 `9a2b672e402e5ce2a7c30228c74453dcd7acf422f076b021b5b1cd3890cb8b06`。这两个哈希是本次提交候选的构建身份；设备本地提醒与 TTS 的 NxPlayer 音频链路仍保留。

2026-08-30 主机端完整合同测试使用仓库外的临时 `numpy`（`/tmp/r528-kws-site`）复跑：
`PYTHONPATH=/tmp/r528-kws-site python3 -m unittest discover -s tools -p 'test_*.py'`，共 196 项，其中 190 项通过、6 项按环境条件跳过，结果为 `OK`。`ai_agent` 专项合同测试单独执行为 19/19；Agent、音频生命周期和启动时间三组合计 33/33。覆盖内容包括 MiMo v2.5/Token Plan、WebSocket 纯文本、`vela-desk` 产品 Skill、日程模板工具、移除 Agent 在线音乐工具、Cron 检查日志默认关闭、动态请求禁止复用响应缓存，以及删除不存在 Cron 任务时返回权威错误。`vela-desk-runtime.patch` 已验证可在当前 ai_agent 工作树反向检查，并可在干净 `HEAD` 上正向应用。

本轮运行时安全修复还包括：Cron 每 10 秒的 `Check job[...]` 轮询日志改为调试配置项，默认不打印；只有打开 `CONFIG_AI_AGENT_CRON_VERBOSE_CHECK=y` 才会输出。响应缓存仅允许纯问候、致谢和告别；确认词、环境、提醒、文件、搜索和所有工具相关请求始终重新进入 Agent/tool loop。`cron_remove` 必须使用本轮 `cron_list` 返回的精确 ID，找不到任务时直接返回错误，不会让模型声称删除成功。

## 6. 技术实现

- 构建层：板级 `configs/nsh/defconfig` 打开 `CONFIG_EXAMPLES_AI_AGENT_VELA`，数据目录为 `/data/agent`；关闭未使用的 Feishu、WeChat、MQTT、Node、MCP 和 LVGL Agent UI，保持 CLI/WebSocket 最小运行时。
- 启动层：`agent_main.c` 先启动本地 Agent loop、WebSocket 和 CLI，再由后台线程异步连接 Wi-Fi、启动云端通道；无网络时仍可输入 `set_wifi`、`set_llm`，不需要重启。
- Skill 层：`skill_loader` 将 `vela-desk` 注册到内置表，并在 `/data/agent/skills/` 缺失或版本过旧时写出带 `v3` 标记的 Markdown 文件；升级时删除历史镜像遗留的 `music-dj.md`、`sleep-music.md` 和 `tts-speak.md`，系统提示通过目录枚举和热加载发现剩余 Skill。其它用户自定义 Skill 不会被启动升级逻辑覆盖。摘要空间不足时优先保留 `vela-desk`，避免被其它内置 Skill 截断。
- 工具层：不再编译或注册 `music_search`、`music_play` 等音乐工具，也不允许通过 `run_shell` 调用 `nxplayer`；`routine_schedule_update` 校验槽位、时间、UTF-8 文本和 `expected_text`，用临时文件加原子 `rename` 更新每日模板。设备提醒和 TTS 继续使用 `routine_mgr`/`voice_tts` 的独立音频链路，`tool_shell` 仅增加 `/data/routine/` 只读前缀，不开放写入或危险命令。
- 数据层：`routine_storage_publish_status` 以临时文件加原子 `rename` 发布 JSON，包含传感器原始缩放值、有效性掩码、时钟状态和日程完成状态，Agent 不直接访问 I2C。
- 网络层：`set_llm` 将预设或自定义 OpenAI 兼容端点写入运行时配置；TLS/HTTP 由 ai_agent 现有 `llm_proxy` 和 `vela_tls` 处理。MiMo 分支使用官方 `api-key` 请求头和 v2.5 显式推理参数，其他 LLM 后端行为不变，密钥不进入固件源码。

## 7. 快速补齐最后验收

若评审前仍未完成真机项，所需改动不超过现有代码：连接一块 R528S3-DshanPi，按第 5 节烧录，按第 4 节输入一次 Wi-Fi、MiMo LLM，完成环境查询和日程模板修改，观察一次主动提醒播报，并确认旧音乐 Skill 被清理、未知 `music_play` 工具被拒绝即可。无需切换 routine_mgr 的固定本地意图路由，也无需启用其他云端渠道。
