# 语音小智 - VAD + MIMO 在线语音唤醒系统

## 功能概述

本系统实现了**无需本地 KWS 模型训练**的语音唤醒功能，使用：

- **VAD (Voice Activity Detection)** - 简单的能量检测，识别人声
- **MIMO 2.6 API** - 小米多模态大模型
  - ASR: 语音转文字
  - LLM: 对话理解与生成
  - TTS: 文字转语音

### 工作流程

```
持续监听麦克风
  ↓
VAD 检测到人声活动
  ↓
录音 (最长 7 秒)
  ↓
语音结束后上传 MIMO ASR
  ↓
识别文本中是否包含 "你好Vela"
  ↓ 是
MIMO LLM 生成回复
  ↓
MIMO TTS 合成语音
  ↓
播放回复
```

## 新增文件

### 核心模块

1. **vad_detector.c/h** - VAD 语音活动检测
   - 基于 RMS 能量和峰值检测
   - 可配置阈值和帧数

2. **voice_wakeup_service.c/h** - 语音唤醒服务
   - 后台线程持续监听
   - 自动录音、识别、回复
   - 完整的状态管理

3. **voice_wakeup_demo.c** - 独立演示程序
   - 命令行工具，可单独测试

4. **mimo_integration.c** - 已更新到 MIMO 2.6

### UI 集成

5. **routine_ui.c** - 已添加
   - "在线/离线"切换按钮
   - 语音状态实时显示
   - 唤醒服务状态可视化

## 屏幕界面

### 语音面板布局

```
┌─────────────────────────────────────────────────┐
│  监听中...                       [开始语音]     │
│  [离线]                                         │
└─────────────────────────────────────────────────┘
```

- **左上**: 语音状态（实时更新）
  - 监听中...
  - 检测到人声
  - 识别中 (ASR)
  - 唤醒词确认
  - 处理指令 (LLM)
  - 回复中 (TTS)

- **左下**: 模式切换按钮
  - 点击切换 "在线" / "离线"
  - 绿色 = 在线模式（MIMO）
  - 灰色 = 离线模式（本地 KWS，需要训练）

- **右侧**: 开始/停止按钮
  - 蓝色 = 开始语音（手动触发）
  - 橙色 = 停止（正在工作时）

## 编译配置

### 1. 更新 Makefile 和 CMakeLists.txt

已完成，新增文件：
- `vad_detector.c`
- `voice_wakeup_service.c`

### 2. 配置选项

确保以下配置已启用：

```kconfig
CONFIG_ROUTINE_MANAGER_VOICE=y
CONFIG_ROUTINE_MANAGER_VOICE_CAPTURE_DEVICE="/dev/audio/pcm1c"
CONFIG_ROUTINE_MANAGER_VOICE_PLAYBACK_DEVICE="/dev/audio/pcm0p"
```

## 使用方法

### 方式 1: 独立演示程序

```bash
# 在 NSH 中运行
voice_wakeup_demo -k tp-your-mimo-api-key

# 说 "你好Vela" 后跟指令
# 例如: "你好Vela，现在温度多少？"
```

### 方式 2: 集成到 routine_manager

在 `routine_manager_main.c` 中集成语音唤醒服务：

```c
#include "voice_wakeup_service.h"

struct voice_wakeup_service_s wakeup_service;

void voice_wakeup_callback(void *arg, const char *user_text,
                           const char *response_text)
{
  syslog(LOG_INFO, "User: %s\n", user_text);
  syslog(LOG_INFO, "Response: %s\n", response_text);
  
  /* 更新 UI 显示 */
  // ...
}

int main(void)
{
  /* 配置语音唤醒服务 */
  struct voice_wakeup_config_s config = {0};
  
  config.capture_device = "/dev/audio/pcm1c";
  config.playback_device = "/dev/audio/pcm0p";
  config.temp_audio_dir = "/data/voice_wakeup";
  
  /* VAD 配置 */
  config.vad_config.sample_rate = 16000;
  config.vad_config.frame_samples = 320;
  config.vad_config.minimum_rms = 350;
  config.vad_config.minimum_peak = 900;
  
  /* MIMO 配置 */
  strncpy(config.mimo_config.api_key, "your-api-key", MIMO_API_KEY_MAX - 1);
  strncpy(config.mimo_config.model, "mimo-v2.6", MIMO_MODEL_NAME_MAX - 1);
  strncpy(config.mimo_config.tts_voice, "冰糖", MIMO_TTS_VOICE_MAX - 1);
  config.mimo_config.enabled = true;
  
  config.callback = voice_wakeup_callback;
  
  /* 初始化并启动 */
  voice_wakeup_service_init(&wakeup_service, &config);
  voice_wakeup_service_start(&wakeup_service);
  
  /* 主循环 */
  while (running) {
    /* 检查 UI 模式切换 */
    bool online_mode = routine_ui_get_voice_online_mode();
    
    /* 如果切换到离线模式，停止唤醒服务 */
    if (!online_mode) {
      voice_wakeup_service_stop(&wakeup_service);
    } else if (!wakeup_service.running) {
      voice_wakeup_service_start(&wakeup_service);
    }
    
    /* 更新状态显示 */
    enum voice_wakeup_state_e state;
    state = voice_wakeup_service_get_state(&wakeup_service);
    
    // 更新 UI 状态显示
    // ...
  }
  
  /* 清理 */
  voice_wakeup_service_shutdown(&wakeup_service);
  
  return 0;
}
```

## API 说明

### VAD 检测器

```c
/* 初始化 */
struct vad_detector_s detector;
struct vad_config_s vad_config = {
  .sample_rate = 16000,
  .frame_samples = 320,      /* 20ms @ 16kHz */
  .minimum_rms = 350,         /* RMS 能量阈值 */
  .minimum_peak = 900,        /* 峰值阈值 */
  .speech_frames = 3,         /* 连续 3 帧判定为语音开始 */
  .silence_frames = 20,       /* 连续 20 帧判定为语音结束 */
};
vad_detector_init(&detector, &vad_config);

/* 处理音频帧 */
bool is_speech = vad_detector_process_frame(&detector, samples, count);
```

### 语音唤醒服务

```c
/* 初始化 */
struct voice_wakeup_service_s service;
struct voice_wakeup_config_s config;
voice_wakeup_service_init(&service, &config);

/* 启动后台监听线程 */
voice_wakeup_service_start(&service);

/* 获取状态 */
enum voice_wakeup_state_e state = voice_wakeup_service_get_state(&service);

/* 更新 MIMO 配置 */
voice_wakeup_service_update_mimo(&service, &new_mimo_config);

/* 停止 */
voice_wakeup_service_stop(&service);
voice_wakeup_service_shutdown(&service);
```

### UI 接口

```c
/* 获取当前在线/离线模式 */
bool online_mode = routine_ui_get_voice_online_mode();

/* 用户点击模式切换按钮会自动切换 */
```

## MIMO API Key 获取

1. 访问 https://mimo.mi.com/
2. 注册/登录小米账号
3. 在控制台创建 API Key
4. 选择 Token Plan (tp-前缀) 或标准计费

## 真机测试流程

### 1. 烧录最新镜像

```bash
# 在 PC 上打包
cd /home/gfc/桌面/openvela/contest2026_113_veladuxingke
./build.sh

# 烧录镜像
# 镜像路径: vendor/allwinnertech/lichee/out/r528s3/dshanpi_nand/rtos_nuttx_r528s3-dshanpi_uart0_256Mnand.img
```

### 2. 板上测试

```bash
# 1. 确认 SD 卡挂载
mount
df /sdcard

# 2. 创建临时目录
mkdir -p /data/voice_wakeup

# 3. 设置 MIMO API Key (通过 LAN Panel 或直接修改代码)

# 4. 启动 routine_manager
routine_mgr

# 5. 在屏幕上点击 "离线" 按钮切换到 "在线" 模式（绿色）

# 6. 对着麦克风说: "你好Vela，现在温度多少？"

# 7. 观察状态变化:
#    监听中... → 检测到人声 → 识别中 (ASR) → 唤醒词确认 → 
#    处理指令 (LLM) → 回复中 (TTS) → 播放回复
```

### 3. 调试

```bash
# 查看日志
dmesg | grep -i "vad\|wakeup\|mimo"

# 检查录音文件
ls -lh /data/voice_wakeup/

# 测试 MIMO API (curl)
curl -X POST 'https://api.xiaomimimo.com/v1/audio/transcriptions' \
  -H 'api-key: your-key' \
  -F 'file=@/data/voice_wakeup/wakeup_0.pcm' \
  -F 'model=mimo-v2.6'
```

## 性能优化

### 参数调优

1. **VAD 灵敏度**
   - `minimum_rms`: 越小越灵敏，但容易误触发
   - `minimum_peak`: 越小越灵敏
   - 建议值: rms=350, peak=900

2. **录音时长**
   - `max_recording_ms`: 最长录音时间（默认 7000ms）
   - `post_speech_ms`: 语音结束后继续录音（默认 500ms）

3. **响应速度**
   - MIMO ASR: ~1-2 秒
   - MIMO LLM: ~1-3 秒
   - MIMO TTS: ~1-2 秒
   - 总延迟: ~3-7 秒

## 常见问题

### Q1: 一直显示"监听中..."，说话没反应？
A: 检查麦克风是否正常工作，调低 VAD 阈值。

### Q2: 识别不出"你好Vela"？
A: 检查 MIMO API Key 是否正确，网络是否连接。

### Q3: 点击"在线"按钮没反应？
A: 确保 MIMO API Key 已配置，网络已连接。

### Q4: TTS 播放没声音？
A: 检查播放设备路径，音量设置。

### Q5: 成本如何？
A: Token Plan 模式下，每次交互约消耗:
- ASR: ~10 tokens
- LLM: ~50-100 tokens  
- TTS: ~10 tokens
- 总计: ~100-150 tokens/次

## 下一步改进

1. **添加本地唤醒词检测** - 用简单的模板匹配过滤，减少 MIMO 调用
2. **上下文对话** - 记住上一轮对话，实现多轮交互
3. **自定义唤醒词** - 允许用户设置自己的唤醒词
4. **离线回退** - 在线识别失败时回退到离线 KWS
5. **语音打断** - 播报时可以打断重新输入

## 总结

本方案**完全避免了 KWS 模型训练**，直接使用 MIMO 云端大模型进行语音识别和对话，大大简化了开发流程。只需要：

✅ 简单的 VAD 检测人声  
✅ MIMO API 处理所有 AI 任务  
✅ 屏幕 UI 可视化状态  
✅ 一键切换在线/离线模式  

适合快速原型开发和比赛演示！
