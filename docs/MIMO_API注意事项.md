# ⚠️ MIMO API 接入注意事项

## 当前状态

✅ **代码已完成** - VAD + 语音唤醒服务已实现  
⚠️ **API 未验证** - MIMO API 调用方式需要真实测试确认

---

## 关键问题汇总

### 🔴 问题 1: 音频格式（ASR）

**当前代码**: 直接上传 raw PCM 文件

**潜在问题**: MIMO ASR 可能不支持 raw PCM，需要 WAV 格式

**验证方法**:
```bash
# 在 PC 上测试
./tools/test_mimo_api.sh your-api-key
```

**如果需要 WAV**:
- 修改 `voice_wakeup_service.c` 中的 `save_audio_file()` 函数
- 添加 WAV 文件头（44 字节）
- 详见 `docs/MIMO_API验证指南.md`

---

### 🟡 问题 2: Chat API 响应格式

**当前代码**: 直接查找 `"content"`

**潜在问题**: 
- 如果 MIMO 使用 OpenAI 兼容格式 `choices[0].message.content`
- 当前解析逻辑可能找到错误位置

**验证方法**: 查看测试脚本输出的响应格式

**如果是嵌套格式**:
- 修改 `mimo_integration.c` 中 `mimo_llm_chat()` 函数
- 先查找 `"choices"` → `"message"` → `"content"`

---

### 🟡 问题 3: 模型版本号

**当前代码**:
- ASR: `mimo-v2.6`
- LLM: `mimo-v2.6`
- TTS: `mimo-v2.6-tts`

**潜在问题**: 模型名称可能不正确

**可能的正确名称**:
- 查看 https://mimo.mi.com/models
- 或从测试脚本的错误信息中获取

**如果模型不存在**:
- 修改 `mimo_integration.c` 中的默认模型名称

---

### 🟡 问题 4: 请求头格式

**当前代码**: 使用 `-H 'api-key: xxx'`

**潜在问题**: 可能需要 `-H 'Authorization: Bearer xxx'`

**验证方法**: 测试脚本会显示是否成功

**如果需要修改**:
- 在 `mimo_integration.c` 中所有 curl 命令
- 将 `api-key:` 改为 `Authorization: Bearer`

---

## 必须在真机测试前完成的验证

### ✅ 步骤 1: PC 端 API 测试

```bash
# 1. 获取 MIMO API Key
# 访问 https://mimo.mi.com

# 2. 运行测试脚本
cd /home/gfc/桌面/openvela/contest2026_113_veladuxingke/tools
./test_mimo_api.sh tp-your-api-key-here

# 3. 查看测试结果
# 重点关注:
#   - Chat API 响应格式
#   - ASR 是否支持 PCM
#   - TTS 输出是否正常
#   - 模型版本是否有效
```

### ✅ 步骤 2: 根据测试结果修改代码

**参考文档**: `docs/MIMO_API验证指南.md`

**需要修改的文件**:
- `app/routine_manager/mimo_integration.c` - API 调用和解析
- `app/routine_manager/voice_wakeup_service.c` - 音频保存格式

### ✅ 步骤 3: 手动验证关键 API

```bash
# Chat API
curl -X POST 'https://token-plan-cn.xiaomimimo.com/v1/chat/completions' \
  -H 'api-key: your-key' \
  -H 'Content-Type: application/json' \
  -d '{"model":"mimo-v2.6","messages":[{"role":"user","content":"你好"}]}'

# 查看响应格式，确认 content 的位置
```

---

## 代码修改优先级

### 🔴 高优先级（必须修改）

1. **如果 ASR 不支持 PCM**: 添加 WAV 头
2. **如果 Chat 使用嵌套格式**: 修改解析逻辑
3. **如果模型名称错误**: 更新模型版本号

### 🟡 中优先级（可能需要修改）

4. **请求头格式**: api-key vs Authorization
5. **错误处理**: 解析 API 错误响应
6. **超时设置**: curl 添加 --max-time

### 🟢 低优先级（优化）

7. **JSON 解析**: 使用 cJSON 库代替手动解析
8. **异步处理**: 避免阻塞主线程
9. **缓存机制**: 减少重复 API 调用

---

## 快速修复模板

### 修复 1: 添加 WAV 头

文件: `voice_wakeup_service.c`

位置: `save_audio_file()` 函数

```c
// 在函数开头添加 WAV 头结构定义
struct wav_header_s {
  char riff[4];           /* "RIFF" */
  uint32_t file_size;     /* 文件大小 - 8 */
  char wave[4];           /* "WAVE" */
  char fmt[4];            /* "fmt " */
  uint32_t fmt_size;      /* 16 */
  uint16_t format;        /* 1 = PCM */
  uint16_t channels;      /* 1 */
  uint32_t sample_rate;   /* 16000 */
  uint32_t byte_rate;     /* 32000 */
  uint16_t block_align;   /* 2 */
  uint16_t bits;          /* 16 */
  char data[4];           /* "data" */
  uint32_t data_size;     /* 样本数 * 2 */
} __attribute__((packed));

// 在写入文件前先写入 WAV 头
```

### 修复 2: 修改 Chat 响应解析

文件: `mimo_integration.c`

位置: `mimo_llm_chat()` 函数中的响应解析部分

```c
// 替换原有的 strstr(buffer, "\"content\":")
// 改为:
char *choices_start = strstr(buffer, "\"choices\":");
if (choices_start != NULL) {
  char *message_start = strstr(choices_start, "\"message\":");
  if (message_start != NULL) {
    char *content_start = strstr(message_start, "\"content\":");
    // ... 继续原有的解析逻辑
  }
}
```

### 修复 3: 更新模型名称

文件: `mimo_integration.c`

位置: `mimo_service_init()` 函数

```c
// 根据官方文档或测试结果修改
strncpy(service->config.model, "正确的模型名称", MIMO_MODEL_NAME_MAX - 1);
```

---

## 测试检查清单

### 在编译前

- [ ] 已运行 `./tools/test_mimo_api.sh`
- [ ] 已确认 Chat API 响应格式
- [ ] 已确认 ASR 音频格式要求
- [ ] 已确认 TTS 输出正常
- [ ] 已确认模型版本号正确
- [ ] 已根据测试结果修改代码

### 在烧录前

- [ ] 代码编译无错误
- [ ] 构建配置已更新
- [ ] 镜像打包成功

### 在真机测试前

- [ ] MIMO API Key 已准备
- [ ] 网络连接正常
- [ ] SD 卡已插入
- [ ] 知道如何配置 API Key

---

## 真机测试失败的调试方法

### 如果 ASR 识别失败

```bash
# 1. 检查录音文件
ls -lh /data/voice_wakeup/
# 应该看到 wakeup_*.pcm 或 wakeup_*.wav

# 2. 手动上传测试
curl -X POST 'https://token-plan-cn.xiaomimimo.com/v1/audio/transcriptions' \
  -H 'api-key: your-key' \
  -F 'file=@/data/voice_wakeup/wakeup_0.pcm' \
  -F 'model=mimo-v2.6'

# 3. 查看返回结果
```

### 如果 Chat 没有回复

```bash
# 1. 检查 MIMO 调用日志
dmesg | grep -i "mimo\|llm"

# 2. 查看临时文件
cat /data/mimo_tmp/llm_result.txt

# 3. 手动测试
curl -X POST 'https://token-plan-cn.xiaomimimo.com/v1/chat/completions' \
  -H 'api-key: your-key' \
  -H 'Content-Type: application/json' \
  -d '{"model":"mimo-v2.6","messages":[{"role":"user","content":"你好"}]}'
```

### 如果 TTS 没有声音

```bash
# 1. 检查 TTS 输出文件
ls -lh /data/mimo_tmp/tts_output.pcm
file /data/mimo_tmp/tts_output.pcm

# 2. 测试播放
# (如果板上有 aplay 或其他播放工具)
```

---

## 紧急回退方案

如果 MIMO API 始终无法工作：

### 方案 A: 使用按键触发代替自动唤醒

- 保留 MIMO ASR/LLM/TTS
- 移除 VAD 持续监听
- 用户按键后开始录音
- 录音完成后调用 MIMO

### 方案 B: 使用离线 TTS

- 保留 MIMO ASR/LLM
- 仅 TTS 使用本地预录语音
- 牺牲灵活性，但保证核心功能

---

## 总结

⚠️ **关键点**: 

1. **先在 PC 测试 API** - 避免板上调试
2. **确认响应格式** - 解析逻辑必须匹配
3. **验证音频格式** - PCM vs WAV
4. **检查模型版本** - 使用官方支持的版本

📋 **行动项**:

1. 运行 `./tools/test_mimo_api.sh`
2. 阅读 `docs/MIMO_API验证指南.md`
3. 根据测试结果修改代码
4. 重新编译和测试

**只有 API 验证通过后，才建议烧录到板子！**
