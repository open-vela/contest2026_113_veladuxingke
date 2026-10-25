# MIMO API 接入验证指南

## 目的

在真机测试前，先在 PC 上验证 MIMO API 的接入方式是否正确，避免烧录后发现 API 调用失败。

---

## 步骤 1: 获取 MIMO API Key

1. 访问 https://mimo.mi.com
2. 注册/登录小米账号
3. 进入"控制台" → "API 密钥"
4. 创建新的 API Key
   - **Token Plan** (推荐): 以 `tp-` 开头
   - **标准计费**: 普通格式

**记下你的 API Key！**

---

## 步骤 2: 在 PC 上测试 API

```bash
cd /home/gfc/桌面/openvela/contest2026_113_veladuxingke/tools

# 运行测试脚本（替换为你的 API Key）
./test_mimo_api.sh tp-your-api-key-here
```

### 测试内容

脚本会自动测试：

1. **Chat Completions API** (文本对话)
   - 验证 API 端点是否正确
   - 检查响应格式（OpenAI 兼容 vs 简化格式）

2. **ASR API** (语音转文字)
   - 测试是否支持 raw PCM 格式
   - 或是否需要 WAV 格式

3. **TTS API** (文字转语音)
   - 验证音频生成是否正常
   - 检查输出格式

---

## 步骤 3: 根据测试结果调整代码

### 问题 1: Chat API 响应格式

**测试输出显示:**
```json
{
  "choices": [
    {
      "message": {
        "content": "你好"
      }
    }
  ]
}
```

**如果是这种格式** (OpenAI 兼容)，需要修改 `mimo_integration.c`:

当前代码直接查找 `"content"`，这在嵌套结构中可能找到错误的位置。

**修复方法:**

```c
/* 在 mimo_llm_chat() 函数中，修改解析逻辑 */

/* 先查找 choices */
char *choices_start = strstr(buffer, "\"choices\":");
if (choices_start != NULL)
{
  /* 在 choices 内查找 message */
  char *message_start = strstr(choices_start, "\"message\":");
  if (message_start != NULL)
  {
    /* 在 message 内查找 content */
    char *content_start = strstr(message_start, "\"content\":");
    /* ... 后续解析逻辑 */
  }
}
```

---

### 问题 2: ASR 音频格式

**测试输出显示:**
```
✗ ASR 可能需要 WAV 格式
```

**如果不支持 raw PCM**，需要在保存录音时添加 WAV 头。

**修复方法:**

在 `voice_wakeup_service.c` 中修改 `save_audio_file()` 函数：

```c
static int save_audio_file(FAR const char *path,
                          FAR const int16_t *buffer,
                          size_t samples)
{
  FILE *fp;
  size_t written;
  
  /* WAV 文件头结构 */
  struct wav_header_s {
    char riff[4];           /* "RIFF" */
    uint32_t file_size;     /* 文件大小 - 8 */
    char wave[4];           /* "WAVE" */
    char fmt[4];            /* "fmt " */
    uint32_t fmt_size;      /* 16 */
    uint16_t format;        /* 1 = PCM */
    uint16_t channels;      /* 1 */
    uint32_t sample_rate;   /* 16000 */
    uint32_t byte_rate;     /* 16000 * 1 * 2 */
    uint16_t block_align;   /* 1 * 2 */
    uint16_t bits;          /* 16 */
    char data[4];           /* "data" */
    uint32_t data_size;     /* 样本数 * 2 */
  } __attribute__((packed));
  
  struct wav_header_s header;
  uint32_t data_size = samples * sizeof(int16_t);
  
  /* 构建 WAV 头 */
  memcpy(header.riff, "RIFF", 4);
  header.file_size = 36 + data_size;
  memcpy(header.wave, "WAVE", 4);
  memcpy(header.fmt, "fmt ", 4);
  header.fmt_size = 16;
  header.format = 1;
  header.channels = 1;
  header.sample_rate = 16000;
  header.byte_rate = 32000;
  header.block_align = 2;
  header.bits = 16;
  memcpy(header.data, "data", 4);
  header.data_size = data_size;
  
  fp = fopen(path, "wb");
  if (fp == NULL)
  {
    return -errno;
  }
  
  /* 写入 WAV 头 */
  fwrite(&header, sizeof(header), 1, fp);
  
  /* 写入音频数据 */
  written = fwrite(buffer, sizeof(int16_t), samples, fp);
  fclose(fp);
  
  if (written != samples)
  {
    return -EIO;
  }
  
  return OK;
}
```

同时修改文件扩展名：

```c
/* 在 process_voice_interaction() 中 */
snprintf(audio_path, sizeof(audio_path), "%s/wakeup_%lu.wav",  /* 改为 .wav */
         service->config.temp_audio_dir,
         (unsigned long)service->wakeup_count);
```

---

### 问题 3: 模型版本错误

**测试输出显示:**
```
✗ 模型版本可能不正确
```

**可能的正确模型名称:**
- ASR: `mimo-v2.6` 或 `mimo-audio` 或 `whisper-large-v3`
- LLM: `mimo-v2.6` 或 `mimo-chat`
- TTS: `mimo-v2.6-tts` 或 `mimo-tts`

**修复方法:**

根据测试结果或官方文档，在 `mimo_integration.c` 中修改默认模型：

```c
int mimo_service_init(FAR struct mimo_service_s *service,
                      FAR const struct mimo_config_s *config)
{
  /* ... */
  
  /* 设置默认模型和音色 */
  if (strlen(service->config.model) == 0)
  {
    /* 根据官方文档修改 */
    strncpy(service->config.model, "正确的模型名称", MIMO_MODEL_NAME_MAX - 1);
  }
  
  /* ... */
}
```

---

## 步骤 4: 手动验证 API 调用

如果测试脚本不够详细，可以手动测试：

### Chat API

```bash
curl -X POST 'https://token-plan-cn.xiaomimimo.com/v1/chat/completions' \
  -H 'api-key: your-key' \
  -H 'Content-Type: application/json' \
  -d '{
    "model": "mimo-v2.6",
    "messages": [
      {"role": "user", "content": "你好"}
    ]
  }'
```

### ASR API (PCM)

```bash
# 生成 1 秒测试音频
dd if=/dev/zero of=/tmp/test.pcm bs=32000 count=1

curl -X POST 'https://token-plan-cn.xiaomimimo.com/v1/audio/transcriptions' \
  -H 'api-key: your-key' \
  -F 'file=@/tmp/test.pcm' \
  -F 'model=mimo-v2.6'
```

### TTS API

```bash
curl -X POST 'https://token-plan-cn.xiaomimimo.com/v1/audio/speech' \
  -H 'api-key: your-key' \
  -H 'Content-Type: application/json' \
  -d '{
    "model": "mimo-v2.6-tts",
    "input": "测试",
    "voice": "冰糖",
    "response_format": "pcm"
  }' \
  --output /tmp/tts_output.pcm
```

---

## 步骤 5: 验证响应格式

### Chat API 响应

**格式 A (OpenAI 兼容):**
```json
{
  "id": "chatcmpl-xxx",
  "object": "chat.completion",
  "choices": [
    {
      "index": 0,
      "message": {
        "role": "assistant",
        "content": "你好！"
      },
      "finish_reason": "stop"
    }
  ]
}
```

**格式 B (简化格式):**
```json
{
  "content": "你好！"
}
```

**如果是格式 A**，代码需要修改解析逻辑（见问题 1）。

### ASR API 响应

**标准格式:**
```json
{
  "text": "识别的文字"
}
```

如果返回的是其他格式，需要修改 `mimo_asr()` 中的解析逻辑。

---

## 常见错误排查

### 错误 1: Unauthorized

```json
{
  "error": {
    "message": "Incorrect API key provided",
    "type": "invalid_request_error"
  }
}
```

**原因**: API Key 错误或过期  
**解决**: 检查 API Key 是否正确复制

### 错误 2: Invalid audio format

```json
{
  "error": {
    "message": "Unsupported audio format"
  }
}
```

**原因**: 音频格式不支持  
**解决**: 使用 WAV 格式（见问题 2）

### 错误 3: Model not found

```json
{
  "error": {
    "message": "The model 'mimo-v2.6' does not exist"
  }
}
```

**原因**: 模型名称错误  
**解决**: 查看官方文档确认正确的模型名称

---

## 最终检查清单

在真机测试前，确保：

- [ ] Chat API 测试成功
- [ ] ASR API 测试成功
- [ ] TTS API 测试成功
- [ ] 响应格式已确认
- [ ] 代码中的解析逻辑已匹配响应格式
- [ ] 音频格式已确认（PCM 或 WAV）
- [ ] 模型版本号正确

---

## 下一步

验证通过后：

1. 根据测试结果修改代码
2. 重新编译
3. 烧录到板子
4. 配置 API Key
5. 真机测试

---

## 参考资料

- MIMO 官方文档: https://mimo.mi.com/docs
- API 参考: https://mimo.mi.com/docs/zh-CN/api-reference
- 模型列表: https://mimo.mi.com/models

---

**重要提示**: 

⚠️ 如果无法访问 MIMO 官方文档，建议：
1. 先在 PC 上运行测试脚本
2. 查看实际的 API 响应
3. 根据响应格式调整代码
4. 避免在板上调试 API 问题（效率低）
