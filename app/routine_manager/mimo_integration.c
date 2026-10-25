/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/mimo_integration.c
 *
 * MiMo 在线语音助手集成实现
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>
#include <math.h>
#include <netutils/cJSON.h>

#include "mimo_integration.h"
#include "routine_config.h"

/* 前向声明 ai_agent 的 LLM 接口（避免包含复杂头文件）*/
extern int llm_chat(const char* system_prompt, const char* messages_json,
                    char* response_buf, size_t buf_size);
extern int llm_set_all(const char* host, const char* path,
                       const char* port, const char* api_key,
                       const char* model);

/* 前向声明 vela_tls 接口 */
typedef struct { const char* name; const char* value; } vela_header_t;
extern int vela_https_request(const char* host, const char* port,
                               const char* method, const char* path,
                               const vela_header_t* headers,
                               const char* body, size_t body_len,
                               char* resp_buf, size_t resp_cap,
                               size_t* out_body_len);

#define MIMO_API_HOST "api.xiaomimimo.com"
#define MIMO_API_HOST_TP "token-plan-cn.xiaomimimo.com"
#define MIMO_TMP_DIR "/data/mimo_tmp"
#define MIMO_ASR_OUTPUT "/data/mimo_tmp/asr_result.txt"
#define MIMO_TTS_OUTPUT "/data/mimo_tmp/tts_output.pcm"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Base64 编码表 */
static const char base64_table[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Base64 编码函数 */
static int base64_encode(FAR const uint8_t *input, size_t input_len,
                        FAR char *output, size_t output_size)
{
  size_t i;
  size_t j = 0;
  uint32_t n;

  if (output_size < ((input_len + 2) / 3) * 4 + 1)
    {
      return -ENOMEM;
    }

  for (i = 0; i < input_len; i += 3)
    {
      n = ((uint32_t)input[i]) << 16;
      if (i + 1 < input_len)
        {
          n |= ((uint32_t)input[i + 1]) << 8;
        }
      if (i + 2 < input_len)
        {
          n |= input[i + 2];
        }

      output[j++] = base64_table[(n >> 18) & 0x3F];
      output[j++] = base64_table[(n >> 12) & 0x3F];
      output[j++] = (i + 1 < input_len) ? base64_table[(n >> 6) & 0x3F] : '=';
      output[j++] = (i + 2 < input_len) ? base64_table[n & 0x3F] : '=';
    }

  output[j] = '\0';
  return j;
}

/* Base64 解码函数 */
static int base64_decode(FAR const char *input, size_t input_len,
                        FAR uint8_t *output, size_t output_size)
{
  size_t i, j = 0;
  uint32_t n;
  int pad = 0;
  static const unsigned char decode_table[256] = {
    ['A'] = 0,  ['B'] = 1,  ['C'] = 2,  ['D'] = 3,  ['E'] = 4,  ['F'] = 5,
    ['G'] = 6,  ['H'] = 7,  ['I'] = 8,  ['J'] = 9,  ['K'] = 10, ['L'] = 11,
    ['M'] = 12, ['N'] = 13, ['O'] = 14, ['P'] = 15, ['Q'] = 16, ['R'] = 17,
    ['S'] = 18, ['T'] = 19, ['U'] = 20, ['V'] = 21, ['W'] = 22, ['X'] = 23,
    ['Y'] = 24, ['Z'] = 25, ['a'] = 26, ['b'] = 27, ['c'] = 28, ['d'] = 29,
    ['e'] = 30, ['f'] = 31, ['g'] = 32, ['h'] = 33, ['i'] = 34, ['j'] = 35,
    ['k'] = 36, ['l'] = 37, ['m'] = 38, ['n'] = 39, ['o'] = 40, ['p'] = 41,
    ['q'] = 42, ['r'] = 43, ['s'] = 44, ['t'] = 45, ['u'] = 46, ['v'] = 47,
    ['w'] = 48, ['x'] = 49, ['y'] = 50, ['z'] = 51, ['0'] = 52, ['1'] = 53,
    ['2'] = 54, ['3'] = 55, ['4'] = 56, ['5'] = 57, ['6'] = 58, ['7'] = 59,
    ['8'] = 60, ['9'] = 61, ['+'] = 62, ['/'] = 63
  };

  /* 计算填充字符数量 */
  if (input_len >= 2 && input[input_len - 1] == '=')
    {
      pad++;
      if (input[input_len - 2] == '=')
        pad++;
    }

  /* 检查输出缓冲区大小 */
  size_t expected_len = (input_len * 3) / 4 - pad;
  if (output_size < expected_len)
    {
      return -ENOMEM;
    }

  for (i = 0; i < input_len; i += 4)
    {
      if (input[i] == '=' || (i + 1 < input_len && input[i + 1] == '='))
        break;

      n = decode_table[(unsigned char)input[i]] << 18;
      if (i + 1 < input_len)
        n |= decode_table[(unsigned char)input[i + 1]] << 12;
      if (i + 2 < input_len && input[i + 2] != '=')
        n |= decode_table[(unsigned char)input[i + 2]] << 6;
      if (i + 3 < input_len && input[i + 3] != '=')
        n |= decode_table[(unsigned char)input[i + 3]];

      output[j++] = (n >> 16) & 0xFF;
      if (i + 2 < input_len && input[i + 2] != '=')
        output[j++] = (n >> 8) & 0xFF;
      if (i + 3 < input_len && input[i + 3] != '=')
        output[j++] = n & 0xFF;
    }

  return j;
}

static const char *mimo_get_host(const char *api_key)
{
  if (strncmp(api_key, "tp-", 3) == 0 || strncmp(api_key, "ttp-", 4) == 0)
    {
      return MIMO_API_HOST_TP;
    }
  return MIMO_API_HOST;
}

/****************************************************************************
 * Name: mimo_service_set_sensor_data
 *
 * Description:
 *   设置传感器数据指针，用于 Function Calling
 *
 ****************************************************************************/
void mimo_service_set_sensor_data(FAR struct mimo_service_s *service,
                                   FAR const struct mimo_sensor_data_s *sensor_data)
{
  if (service != NULL)
    {
      service->sensor_data = sensor_data;
    }
}

/****************************************************************************
 * Name: mimo_get_sensor_info
 *
 * Description:
 *   获取传感器信息的 Function Calling 函数
 *
 ****************************************************************************/
static int mimo_get_sensor_info(FAR struct mimo_service_s *service,
                                 char *result_json, size_t result_size)
{
  if (service == NULL || service->sensor_data == NULL || !service->sensor_data->valid)
    {
      snprintf(result_json, result_size, "{\"error\":\"传感器数据不可用\"}");
      return -ENODATA;
    }

  const struct mimo_sensor_data_s *data = service->sensor_data;

  snprintf(result_json, result_size,
           "{"
           "\"temperature\":%.2f,"
           "\"humidity\":%.2f,"
           "\"light\":%.1f,"
           "\"co2\":%u,"
           "\"tvoc\":%u"
           "}",
           data->temperature_x100 / 100.0,
           data->humidity_x100 / 100.0,
           data->light_x10 / 10.0,
           data->eco2_ppm,
           data->tvoc_ppb);

  return OK;
}

/****************************************************************************
 * Name: mimo_service_set_control_callbacks
 *
 * Description:
 *   设置控制回调函数
 *
 ****************************************************************************/
void mimo_service_set_control_callbacks(FAR struct mimo_service_s *service,
                                        mimo_set_threshold_callback_t set_threshold_cb,
                                        mimo_add_reminder_callback_t add_reminder_cb,
                                        FAR void *context)
{
  if (service != NULL)
    {
      /* 注意：需要在 mimo_service_s 中添加字段来存储这些回调 */
      /* 暂时保存在 control_context 中，使用结构体封装 */
      service->control_context = context;
      /* TODO: 完整实现需要在 mimo_service_s 中添加回调字段 */
    }
}

/****************************************************************************
 * Name: mimo_set_temperature_threshold
 *
 * Description:
 *   设置温度阈值的 Function Calling 函数
 *
 ****************************************************************************/
static int mimo_set_temperature_threshold(FAR struct mimo_service_s *service,
                                          float low, float high,
                                          char *result_json, size_t result_size)
{
  /* 加载当前配置 */
  struct routine_settings_s settings;
  int ret = routine_config_load(&settings);

  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to load config for threshold update: %d\n", ret);
      snprintf(result_json, result_size, "{\"error\":\"无法加载配置\"}");
      return ret;
    }

  /* 更新温度阈值 */
  settings.temperature_low_x100 = (int32_t)(low * 100);
  settings.temperature_high_x100 = (int32_t)(high * 100);

  /* 递增 generation 以触发系统重新加载配置 */
  settings.generation = settings.generation == UINT32_MAX ? 1 : settings.generation + 1;

  /* 保存配置 */
  ret = routine_config_save(&settings);

  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to save config with new thresholds: %d\n", ret);
      snprintf(result_json, result_size, "{\"error\":\"保存配置失败\"}");
      return ret;
    }

  syslog(LOG_INFO, "Temperature threshold updated: low=%.1f°C, high=%.1f°C\n", low, high);

  snprintf(result_json, result_size,
           "{\"success\":true,\"message\":\"温度阈值已设置：低温%.1f℃，高温%.1f℃\"}",
           low, high);

  return OK;
}

/****************************************************************************
 * Name: mimo_add_schedule_reminder
 *
 * Description:
 *   添加日程提醒的 Function Calling 函数
 *
 ****************************************************************************/
static int mimo_add_schedule_reminder(FAR struct mimo_service_s *service,
                                      int hour, int minute,
                                      const char *text,
                                      char *result_json, size_t result_size)
{
  /* 验证参数 */
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
    {
      syslog(LOG_ERR, "Invalid time: %02d:%02d\n", hour, minute);
      snprintf(result_json, result_size, "{\"error\":\"时间格式错误\"}");
      return -EINVAL;
    }

  /* 加载当前配置 */
  struct routine_settings_s settings;
  int ret = routine_config_load(&settings);

  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to load config for reminder: %d\n", ret);
      snprintf(result_json, result_size, "{\"error\":\"无法加载配置\"}");
      return ret;
    }

  /* 查找空闲的日程槽位（id == 0 表示未使用）*/
  int free_slot = -1;
  for (int i = 0; i < ROUTINE_SCHEDULE_MAX; i++)
    {
      if (settings.schedule[i].id == 0)
        {
          free_slot = i;
          break;
        }
    }

  /* 如果没有空槽位，查找时间最接近的槽位并替换 */
  if (free_slot < 0)
    {
      int target_minutes = hour * 60 + minute;
      int closest_slot = 0;
      int min_diff = 24 * 60;  /* 最大差值是一天 */

      for (int i = 0; i < ROUTINE_SCHEDULE_MAX; i++)
        {
          int slot_minutes = settings.schedule[i].hour * 60 + settings.schedule[i].minute;
          int diff = abs(target_minutes - slot_minutes);

          if (diff < min_diff)
            {
              min_diff = diff;
              closest_slot = i;
            }
        }

      free_slot = closest_slot;
      syslog(LOG_INFO, "No free slots, replacing closest slot %d (%02d:%02d)\n",
             free_slot, settings.schedule[free_slot].hour,
             settings.schedule[free_slot].minute);
    }

  /* 生成新的 ID（简单递增，实际应该更智能）*/
  uint8_t new_id = 1;
  for (int i = 0; i < ROUTINE_SCHEDULE_MAX; i++)
    {
      if (settings.schedule[i].id >= new_id)
        {
          new_id = settings.schedule[i].id + 1;
        }
    }

  /* 添加新日程 */
  settings.schedule[free_slot].id = new_id;
  settings.schedule[free_slot].hour = hour;
  settings.schedule[free_slot].minute = minute;
  strncpy(settings.schedule[free_slot].text, text,
          ROUTINE_SCHEDULE_TEXT_MAX);
  settings.schedule[free_slot].text[ROUTINE_SCHEDULE_TEXT_MAX] = '\0';

  /* 递增 generation 以触发系统重新加载配置 */
  settings.generation = settings.generation == UINT32_MAX ? 1 : settings.generation + 1;

  /* 保存配置 */
  ret = routine_config_save(&settings);

  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to save config with new reminder: %d\n", ret);
      snprintf(result_json, result_size, "{\"error\":\"保存日程失败\"}");
      return ret;
    }

  syslog(LOG_INFO, "Schedule reminder added: %02d:%02d - %s\n", hour, minute, text);

  snprintf(result_json, result_size,
           "{\"success\":true,\"message\":\"已添加%02d:%02d的提醒：%s\"}",
           hour, minute, text);

  return OK;
}

/****************************************************************************
 * Name: simple_resample_16bit
 *
 * Description:
 *   简单的线性插值重采样（16-bit PCM，单声道）
 *
 ****************************************************************************/
static int simple_resample_16bit(const int16_t* input, int input_samples,
                                 int input_rate, int output_rate,
                                 int16_t** output, int* output_samples)
{
  if (input_rate == output_rate)
    {
      /* 采样率相同，不需要重采样 */
      *output = (int16_t*)malloc(input_samples * sizeof(int16_t));
      if (!*output) return -ENOMEM;
      memcpy(*output, input, input_samples * sizeof(int16_t));
      *output_samples = input_samples;
      return OK;
    }

  /* 计算输出样本数 */
  *output_samples = (int)((long long)input_samples * output_rate / input_rate);

  *output = (int16_t*)malloc(*output_samples * sizeof(int16_t));
  if (!*output)
    {
      return -ENOMEM;
    }

  /* 线性插值重采样 */
  for (int i = 0; i < *output_samples; i++)
    {
      /* 计算对应的输入位置（浮点） */
      float src_pos = (float)i * input_rate / output_rate;
      int src_index = (int)src_pos;
      float frac = src_pos - src_index;

      if (src_index >= input_samples - 1)
        {
          (*output)[i] = input[input_samples - 1];
        }
      else
        {
          /* 线性插值 */
          int16_t sample1 = input[src_index];
          int16_t sample2 = input[src_index + 1];
          (*output)[i] = (int16_t)(sample1 + frac * (sample2 - sample1));
        }
    }

  return OK;
}

static int mimo_ensure_tmp_dir(void)
{
  struct stat st;

  if (stat(MIMO_TMP_DIR, &st) != 0)
    {
      if (mkdir(MIMO_TMP_DIR, 0755) != 0)
        {
          syslog(LOG_ERR, "Failed to create %s: %d\n", MIMO_TMP_DIR, errno);
          return -errno;
        }
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int mimo_service_init(FAR struct mimo_service_s *service,
                      FAR const struct mimo_config_s *config)
{
  if (service == NULL || config == NULL)
    {
      return -EINVAL;
    }

  memset(service, 0, sizeof(struct mimo_service_s));
  memcpy(&service->config, config, sizeof(struct mimo_config_s));

  /* 设置默认模型和音色 */
  if (strlen(service->config.model) == 0)
    {
      strncpy(service->config.model, "mimo-v2.6-pro", MIMO_MODEL_NAME_MAX - 1);
    }

  if (strlen(service->config.tts_voice) == 0)
    {
      strncpy(service->config.tts_voice, "冰糖", MIMO_TTS_VOICE_MAX - 1);
    }

  service->state = MIMO_STATE_IDLE;
  service->initialized = true;

  syslog(LOG_INFO, "MiMo service initialized with model: %s, voice: %s\n",
         service->config.model, service->config.tts_voice);

  return OK;
}

int mimo_asr(FAR struct mimo_service_s *service,
             FAR const char *pcm_path,
             FAR struct mimo_asr_result_s *result)
{
  if (service == NULL || pcm_path == NULL || result == NULL)
    {
      return -EINVAL;
    }

  if (!service->config.enabled || strlen(service->config.api_key) == 0)
    {
      syslog(LOG_WARNING, "MiMo service not enabled or API key not set\n");
      return -ENOTSUP;
    }

  memset(result, 0, sizeof(struct mimo_asr_result_s));
  service->state = MIMO_STATE_ASR_PROCESSING;

  /* 读取 PCM 音频文件 */
  FILE* fp = fopen(pcm_path, "rb");
  if (!fp)
    {
      syslog(LOG_ERR, "Failed to open PCM file: %s\n", pcm_path);
      service->state = MIMO_STATE_ERROR;
      return -errno;
    }

  fseek(fp, 0, SEEK_END);
  long pcm_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (pcm_size <= 0 || pcm_size > 10 * 1024 * 1024)
    {
      syslog(LOG_ERR, "Invalid PCM file size: %ld\n", pcm_size);
      fclose(fp);
      service->state = MIMO_STATE_ERROR;
      return -EINVAL;
    }

  /* 构建 WAV 文件（PCM + WAV 头）*/
  size_t wav_size = pcm_size + 44;  /* WAV header is 44 bytes */
  uint8_t* wav_data = malloc(wav_size);
  if (!wav_data)
    {
      syslog(LOG_ERR, "Failed to allocate WAV buffer\n");
      fclose(fp);
      service->state = MIMO_STATE_ERROR;
      return -ENOMEM;
    }

  /* 写入 WAV 头（16kHz, 16-bit, 单声道）*/
  uint8_t* p = wav_data;
  /* RIFF chunk */
  memcpy(p, "RIFF", 4); p += 4;
  *(uint32_t*)p = (uint32_t)(wav_size - 8); p += 4;  /* file size - 8 */
  memcpy(p, "WAVE", 4); p += 4;
  /* fmt chunk */
  memcpy(p, "fmt ", 4); p += 4;
  *(uint32_t*)p = 16; p += 4;  /* fmt chunk size */
  *(uint16_t*)p = 1; p += 2;   /* PCM format */
  *(uint16_t*)p = 1; p += 2;   /* mono */
  *(uint32_t*)p = 16000; p += 4;  /* sample rate */
  *(uint32_t*)p = 32000; p += 4;  /* byte rate (16000 * 1 * 2) */
  *(uint16_t*)p = 2; p += 2;   /* block align (1 * 2) */
  *(uint16_t*)p = 16; p += 2;  /* bits per sample */
  /* data chunk */
  memcpy(p, "data", 4); p += 4;
  *(uint32_t*)p = (uint32_t)pcm_size; p += 4;

  /* 读取 PCM 数据到 WAV buffer */
  size_t read_size = fread(p, 1, pcm_size, fp);
  fclose(fp);

  if (read_size != (size_t)pcm_size)
    {
      syslog(LOG_ERR, "Failed to read complete PCM file\n");
      free(wav_data);
      service->state = MIMO_STATE_ERROR;
      return -EIO;
    }

  /* Base64 编码 WAV 数据 */
  size_t base64_cap = ((wav_size + 2) / 3) * 4 + 1;
  char* base64_audio = malloc(base64_cap);
  if (!base64_audio)
    {
      syslog(LOG_ERR, "Failed to allocate base64 buffer\n");
      free(wav_data);
      service->state = MIMO_STATE_ERROR;
      return -ENOMEM;
    }

  int base64_len = base64_encode(wav_data, wav_size, base64_audio, base64_cap);
  free(wav_data);

  if (base64_len < 0)
    {
      syslog(LOG_ERR, "Failed to encode WAV to base64\n");
      free(base64_audio);
      service->state = MIMO_STATE_ERROR;
      return -EINVAL;
    }

  /* 构建 JSON 请求（使用 Data URL 格式）*/
  char* json_body = malloc(base64_len + 512);
  if (!json_body)
    {
      syslog(LOG_ERR, "Failed to allocate JSON buffer\n");
      free(base64_audio);
      service->state = MIMO_STATE_ERROR;
      return -ENOMEM;
    }

  snprintf(json_body, base64_len + 512,
           "{"
           "\"model\":\"mimo-v2.5-asr\","
           "\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"input_audio\",\"input_audio\":{\"data\":\"data:audio/wav;base64,%s\"}}]}],"
           "\"asr_options\":{\"language\":\"zh\"}"
           "}",
           base64_audio);

  free(base64_audio);

  /* 准备 HTTP 头 */
  const char* host = mimo_get_host(service->config.api_key);
  vela_header_t headers[] = {
    { "api-key", service->config.api_key },
    { "Content-Type", "application/json" },
    { NULL, NULL }
  };

  /* 分配响应缓冲区 */
  size_t resp_cap = 64 * 1024;
  char* resp_buf = malloc(resp_cap);
  if (!resp_buf)
    {
      syslog(LOG_ERR, "Failed to allocate ASR response buffer\n");
      free(json_body);
      service->state = MIMO_STATE_ERROR;
      return -ENOMEM;
    }

  size_t body_len = 0;

  syslog(LOG_INFO, "Calling MiMo ASR API...\n");

  /* 调用 ASR API */
  int ret = vela_https_request(host, "443", "POST", "/v1/chat/completions",
                               headers, json_body, strlen(json_body),
                               resp_buf, resp_cap, &body_len);

  free(json_body);

  if (ret != 200)
    {
      syslog(LOG_ERR, "ASR API failed: HTTP %d\n", ret);
      free(resp_buf);
      service->state = MIMO_STATE_ERROR;
      return -EIO;
    }

  /* 解析 JSON 响应 */
  cJSON* json = cJSON_Parse(resp_buf);
  free(resp_buf);

  if (!json)
    {
      syslog(LOG_ERR, "Failed to parse ASR JSON response\n");
      service->state = MIMO_STATE_ERROR;
      return -EINVAL;
    }

  /* 提取识别文本 */
  cJSON* choices = cJSON_GetObjectItem(json, "choices");
  if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0)
    {
      syslog(LOG_ERR, "ASR response missing choices array\n");
      cJSON_Delete(json);
      service->state = MIMO_STATE_ERROR;
      return -EINVAL;
    }

  cJSON* first_choice = cJSON_GetArrayItem(choices, 0);
  cJSON* message = cJSON_GetObjectItem(first_choice, "message");
  cJSON* content = cJSON_GetObjectItem(message, "content");

  if (!content || !cJSON_IsString(content))
    {
      syslog(LOG_ERR, "ASR response missing content field\n");
      cJSON_Delete(json);
      service->state = MIMO_STATE_ERROR;
      return -EINVAL;
    }

  /* 复制识别结果 */
  strncpy(result->text, content->valuestring, sizeof(result->text) - 1);
  result->success = true;
  cJSON_Delete(json);

  service->state = MIMO_STATE_IDLE;
  syslog(LOG_INFO, "MiMo ASR result: %s\n", result->text);

  return OK;
}

/****************************************************************************
 * Name: mimo_get_tools_json
 *
 * Description:
 *   返回工具定义 JSON（Function Calling）
 *
 ****************************************************************************/
static const char* mimo_get_tools_json(void)
{
  return "["
    "{"
      "\"type\":\"function\","
      "\"function\":{"
        "\"name\":\"get_sensor_info\","
        "\"description\":\"获取当前环境传感器数据，包括温度、湿度、光照、CO2和TVOC\","
        "\"parameters\":{"
          "\"type\":\"object\","
          "\"properties\":{}"
        "}"
      "}"
    "},"
    "{"
      "\"type\":\"function\","
      "\"function\":{"
        "\"name\":\"set_temperature_threshold\","
        "\"description\":\"设置温度报警阈值\","
        "\"parameters\":{"
          "\"type\":\"object\","
          "\"properties\":{"
            "\"low\":{\"type\":\"number\",\"description\":\"低温阈值（摄氏度）\"},"
            "\"high\":{\"type\":\"number\",\"description\":\"高温阈值（摄氏度）\"}"
          "},"
          "\"required\":[\"low\",\"high\"]"
        "}"
      "}"
    "},"
    "{"
      "\"type\":\"function\","
      "\"function\":{"
        "\"name\":\"add_reminder\","
        "\"description\":\"添加定时提醒\","
        "\"parameters\":{"
          "\"type\":\"object\","
          "\"properties\":{"
            "\"hour\":{\"type\":\"integer\",\"description\":\"小时(0-23)\"},"
            "\"minute\":{\"type\":\"integer\",\"description\":\"分钟(0-59)\"},"
            "\"text\":{\"type\":\"string\",\"description\":\"提醒内容\"}"
          "},"
          "\"required\":[\"hour\",\"minute\",\"text\"]"
        "}"
      "}"
    "}"
  "]";
}

/****************************************************************************
 * Name: mimo_execute_function
 *
 * Description:
 *   执行工具调用
 *
 ****************************************************************************/
static int mimo_execute_function(FAR struct mimo_service_s *service,
                                   const char* function_name,
                                   cJSON* arguments,
                                   char* result, size_t result_size)
{
  if (strcmp(function_name, "get_sensor_info") == 0)
    {
      return mimo_get_sensor_info(service, result, result_size);
    }
  else if (strcmp(function_name, "set_temperature_threshold") == 0)
    {
      cJSON* low_json = cJSON_GetObjectItem(arguments, "low");
      cJSON* high_json = cJSON_GetObjectItem(arguments, "high");

      if (!low_json || !high_json || !cJSON_IsNumber(low_json) || !cJSON_IsNumber(high_json))
        {
          snprintf(result, result_size, "{\"error\":\"参数错误\"}");
          return -EINVAL;
        }

      return mimo_set_temperature_threshold(service,
                                           (float)low_json->valuedouble,
                                           (float)high_json->valuedouble,
                                           result, result_size);
    }
  else if (strcmp(function_name, "add_reminder") == 0)
    {
      cJSON* hour_json = cJSON_GetObjectItem(arguments, "hour");
      cJSON* minute_json = cJSON_GetObjectItem(arguments, "minute");
      cJSON* text_json = cJSON_GetObjectItem(arguments, "text");

      if (!hour_json || !minute_json || !text_json ||
          !cJSON_IsNumber(hour_json) || !cJSON_IsNumber(minute_json) ||
          !cJSON_IsString(text_json))
        {
          snprintf(result, result_size, "{\"error\":\"参数错误\"}");
          return -EINVAL;
        }

      return mimo_add_schedule_reminder(service,
                                       hour_json->valueint,
                                       minute_json->valueint,
                                       text_json->valuestring,
                                       result, result_size);
    }

  snprintf(result, result_size, "{\"error\":\"未知函数: %s\"}", function_name);
  return -EINVAL;
}

int mimo_llm_chat(FAR struct mimo_service_s *service,
                  FAR const char *user_text,
                  FAR struct mimo_llm_response_s *response)
{
  char messages_json[512];
  char response_buf[2048];
  int ret;

  if (service == NULL || user_text == NULL || response == NULL)
    {
      return -EINVAL;
    }

  if (!service->config.enabled || strlen(service->config.api_key) == 0)
    {
      return -ENOTSUP;
    }

  memset(response, 0, sizeof(struct mimo_llm_response_s));
  service->state = MIMO_STATE_LLM_PROCESSING;

  /* 配置 ai_agent LLM backend 使用 MiMo */
  const char *host = mimo_get_host(service->config.api_key);
  syslog(LOG_INFO, "Configuring LLM backend: host=%s model=%s\n",
         host, service->config.model);

  ret = llm_set_all(host, "/v1/chat/completions", "443",
                    service->config.api_key, service->config.model);
  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to configure LLM backend: %d\n", ret);
      service->state = MIMO_STATE_ERROR;
      return ret;
    }

  /* 构建 messages JSON */
  snprintf(messages_json, sizeof(messages_json),
           "[{\"role\":\"user\",\"content\":\"%s\"}]",
           user_text);

  syslog(LOG_INFO, "Calling MiMo LLM via ai_agent for: %s\n", user_text);

  /* 构建 system prompt，包含传感器数据 */
  char system_prompt[512];
  if (service->sensor_data != NULL && service->sensor_data->valid)
    {
      snprintf(system_prompt, sizeof(system_prompt),
               "你是 OpenVela 智能桌面助手。当前环境数据：温度%.1f℃，湿度%.1f%%，"
               "光照%.0f lux，CO2浓度%u ppm，TVOC浓度%u ppb。回答要简洁友好。",
               service->sensor_data->temperature_x100 / 100.0,
               service->sensor_data->humidity_x100 / 100.0,
               service->sensor_data->light_x10 / 10.0,
               service->sensor_data->eco2_ppm,
               service->sensor_data->tvoc_ppb);
      syslog(LOG_INFO, "LLM system prompt with sensor data\n");
    }
  else
    {
      snprintf(system_prompt, sizeof(system_prompt),
               "你是 OpenVela 智能桌面助手，回答要简洁友好。");
    }

  /* 使用 ai_agent 的 llm_chat 接口 */
  ret = llm_chat(system_prompt, messages_json, response_buf, sizeof(response_buf));

  if (ret < 0)
    {
      syslog(LOG_ERR, "MiMo LLM failed: %d, response: %s\n", ret, response_buf);
      service->state = MIMO_STATE_ERROR;
      response->error_code = ret;
      return ret;
    }

  /* 复制响应内容 */
  if (response_buf[0] != '\0')
    {
      strncpy(response->text, response_buf, sizeof(response->text) - 1);
      response->text[sizeof(response->text) - 1] = '\0';
      response->success = true;
      syslog(LOG_INFO, "MiMo LLM response: %s\n", response->text);
    }
  else
    {
      response->success = false;
      syslog(LOG_WARNING, "MiMo LLM returned empty response\n");
    }

  service->state = MIMO_STATE_IDLE;
  return response->success ? OK : -ENODATA;
}

/****************************************************************************
 * Name: mimo_call_tts_api
 *
 * Description:
 *   调用 MiMo TTS API，返回二进制 PCM 数据并保存到文件
 *   注意：MiMo TTS 使用 /v1/chat/completions 端点，通过 audio 参数控制
 *
 ****************************************************************************/
static int mimo_call_tts_api(const char* host, const char* api_key,
                               const char* model, const char* text,
                               const char* voice, const char* output_path)
{
  char json_body[2048];
  char escaped_text[1024];
  size_t i, j = 0;
  int ret;
  FILE* fp;

  /* 转义文本中的引号和反斜杠 */
  for (i = 0; i < strlen(text) && j < sizeof(escaped_text) - 2; i++)
    {
      if (text[i] == '"' || text[i] == '\\')
        {
          escaped_text[j++] = '\\';
        }
      else if (text[i] == '\n')
        {
          escaped_text[j++] = '\\';
          escaped_text[j++] = 'n';
          continue;
        }
      escaped_text[j++] = text[i];
    }
  escaped_text[j] = '\0';

  /* 构建 JSON 请求体 - 使用 chat/completions 格式 */
  snprintf(json_body, sizeof(json_body),
           "{"
           "\"model\":\"%s\","
           "\"messages\":[{\"role\":\"assistant\",\"content\":\"%s\"}],"
           "\"audio\":{\"format\":\"wav\",\"voice\":\"%s\"}"
           "}",
           model, escaped_text, voice);

  /* 准备 HTTP 头 */
  vela_header_t headers[] = {
    { "api-key", api_key },
    { "Content-Type", "application/json" },
    { NULL, NULL }
  };

  /* 分配响应缓冲区（最大 2MB PCM 数据）*/
  size_t resp_cap = 2 * 1024 * 1024;
  char* resp_buf = malloc(resp_cap);
  if (!resp_buf)
    {
      syslog(LOG_ERR, "Failed to allocate TTS response buffer\n");
      return -ENOMEM;
    }

  size_t body_len = 0;

  /* 调用 HTTPS API - 使用 /v1/chat/completions */
  ret = vela_https_request(host, "443", "POST", "/v1/chat/completions",
                           headers, json_body, strlen(json_body),
                           resp_buf, resp_cap, &body_len);

  if (ret != 200)
    {
      syslog(LOG_ERR, "TTS API failed: HTTP %d\n", ret);
      free(resp_buf);
      return -EIO;
    }

  if (body_len == 0)
    {
      syslog(LOG_ERR, "TTS API returned empty response\n");
      free(resp_buf);
      return -ENODATA;
    }

  /* 解析 JSON 响应 */
  cJSON* json = cJSON_Parse(resp_buf);
  if (!json)
    {
      syslog(LOG_ERR, "Failed to parse TTS JSON response\n");
      free(resp_buf);
      return -EINVAL;
    }

  /* 提取 audio.data (base64 编码的 PCM 数据) */
  cJSON* choices = cJSON_GetObjectItem(json, "choices");
  if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0)
    {
      syslog(LOG_ERR, "TTS response missing choices array\n");
      cJSON_Delete(json);
      free(resp_buf);
      return -EINVAL;
    }

  cJSON* first_choice = cJSON_GetArrayItem(choices, 0);
  cJSON* message = cJSON_GetObjectItem(first_choice, "message");
  cJSON* audio = cJSON_GetObjectItem(message, "audio");
  cJSON* data = cJSON_GetObjectItem(audio, "data");

  if (!data || !cJSON_IsString(data))
    {
      syslog(LOG_ERR, "TTS response missing audio.data field\n");
      cJSON_Delete(json);
      free(resp_buf);
      return -EINVAL;
    }

  const char* base64_audio = data->valuestring;
  size_t base64_len = strlen(base64_audio);

  /* 分配解码缓冲区 */
  size_t pcm_cap = (base64_len * 3) / 4 + 4;
  uint8_t* pcm_buf = malloc(pcm_cap);
  if (!pcm_buf)
    {
      syslog(LOG_ERR, "Failed to allocate PCM decode buffer\n");
      cJSON_Delete(json);
      free(resp_buf);
      return -ENOMEM;
    }

  /* Base64 解码 */
  int decoded_len = base64_decode(base64_audio, base64_len, pcm_buf, pcm_cap);
  cJSON_Delete(json);
  free(resp_buf);

  if (decoded_len < 0)
    {
      syslog(LOG_ERR, "Failed to decode base64 audio data\n");
      free(pcm_buf);
      return -EINVAL;
    }

  /* 检查并处理 WAV 格式 */
  uint8_t* pcm_data = pcm_buf;
  int pcm_len = decoded_len;
  int source_sample_rate = 16000;  /* 默认 16kHz */
  bool need_free_resampled = false;  /* 标记是否需要释放重采样缓冲区 */

  if (decoded_len > 44 &&
      pcm_buf[0] == 'R' && pcm_buf[1] == 'I' &&
      pcm_buf[2] == 'F' && pcm_buf[3] == 'F')
    {
      /* 这是 WAV 文件，读取采样率 */
      source_sample_rate = *(uint32_t*)(pcm_buf + 24);

      /* 跳过 WAV 头部 */
      pcm_data = pcm_buf + 44;
      pcm_len = decoded_len - 44;

      syslog(LOG_INFO, "TTS: Detected WAV format, sample_rate=%d Hz, data_size=%d bytes\n",
             source_sample_rate, pcm_len);

      /* 如果采样率不是 16kHz，需要重采样 */
      if (source_sample_rate != 16000)
        {
          syslog(LOG_INFO, "TTS: Resampling from %d Hz to 16000 Hz\n", source_sample_rate);

          int16_t* resampled_data = NULL;
          int resampled_samples = 0;
          int input_samples = pcm_len / 2;  /* 16-bit = 2 bytes per sample */

          int ret_resample = simple_resample_16bit((int16_t*)pcm_data, input_samples,
                                                   source_sample_rate, 16000,
                                                   &resampled_data, &resampled_samples);

          if (ret_resample == OK && resampled_data != NULL)
            {
              /* 更新 PCM 数据指针和长度 */
              pcm_data = (uint8_t*)resampled_data;
              pcm_len = resampled_samples * 2;  /* bytes */
              need_free_resampled = true;

              syslog(LOG_INFO, "TTS: Resampling done, output=%d samples (%d bytes)\n",
                     resampled_samples, pcm_len);
            }
          else
            {
              syslog(LOG_ERR, "TTS: Resampling failed, audio will sound distorted\n");
            }
        }
    }

  /* 保存纯 PCM 数据到文件 */
  fp = fopen(output_path, "wb");
  if (!fp)
    {
      syslog(LOG_ERR, "Failed to open TTS output file: %s\n", output_path);
      if (need_free_resampled && pcm_data != pcm_buf) free(pcm_data);
      free(pcm_buf);
      return -errno;
    }

  size_t written = fwrite(pcm_data, 1, pcm_len, fp);
  fclose(fp);

  /* 释放内存 */
  if (need_free_resampled && pcm_data != pcm_buf)
    {
      free(pcm_data);  /* 释放重采样缓冲区 */
    }
  free(pcm_buf);  /* 释放原始缓冲区 */

  if (written != (size_t)pcm_len)
    {
      syslog(LOG_ERR, "Failed to write complete TTS data\n");
      return -EIO;
    }

  syslog(LOG_INFO, "TTS saved: %s (%d bytes PCM)\n", output_path, pcm_len);
  return OK;
}

/****************************************************************************
 * Name: mimo_detect_and_execute_intent
 *
 * Description:
 *   检测用户意图并执行相应操作（简化版 Function Calling）
 *
 ****************************************************************************/

/* 辅助函数：将中文数字转换为阿拉伯数字 */
static int parse_chinese_number(const char *text, int max_len)
{
  int result = 0;
  int shi = 0;  /* 十位 */
  int ge = 0;   /* 个位 */

  if (max_len <= 0) return 0;

  /* 处理"十"位 */
  if (strncmp(text, "五十", strlen("五十")) == 0)
    {
      shi = 50;
      text += strlen("五十");
      max_len -= strlen("五十");
    }
  else if (strncmp(text, "四十", strlen("四十")) == 0)
    {
      shi = 40;
      text += strlen("四十");
      max_len -= strlen("四十");
    }
  else if (strncmp(text, "三十", strlen("三十")) == 0)
    {
      shi = 30;
      text += strlen("三十");
      max_len -= strlen("三十");
    }
  else if (strncmp(text, "二十", strlen("二十")) == 0)
    {
      shi = 20;
      text += strlen("二十");
      max_len -= strlen("二十");
    }
  else if (strncmp(text, "十", strlen("十")) == 0)
    {
      shi = 10;
      text += strlen("十");
      max_len -= strlen("十");
    }

  /* 处理个位 */
  if (max_len > 0)
    {
      if (strncmp(text, "九", strlen("九")) == 0) ge = 9;
      else if (strncmp(text, "八", strlen("八")) == 0) ge = 8;
      else if (strncmp(text, "七", strlen("七")) == 0) ge = 7;
      else if (strncmp(text, "六", strlen("六")) == 0) ge = 6;
      else if (strncmp(text, "五", strlen("五")) == 0) ge = 5;
      else if (strncmp(text, "四", strlen("四")) == 0) ge = 4;
      else if (strncmp(text, "三", strlen("三")) == 0) ge = 3;
      else if (strncmp(text, "二", strlen("二")) == 0) ge = 2;
      else if (strncmp(text, "一", strlen("一")) == 0) ge = 1;
    }

  result = shi + ge;
  return result;
}

int mimo_detect_and_execute_intent(FAR struct mimo_service_s *service,
                                   const char *user_text,
                                   const char *llm_response,
                                   char *action_result,
                                   size_t result_size)
{
  if (service == NULL || user_text == NULL)
    {
      return -EINVAL;
    }

  syslog(LOG_INFO, "Intent detection checking: '%s'\n", user_text);

  /* 检测温度阈值设置意图 */
  if (strstr(user_text, "温度") || strstr(user_text, "阈值") ||
      strstr(user_text, "最低温") || strstr(user_text, "最高温"))
    {
      syslog(LOG_INFO, "Temperature keywords found, extracting numbers...\n");

      float low = 18.0, high = 26.0;  /* 默认值 */
      float numbers[10];  /* 存储找到的所有数字 */
      int found_count = 0;

      /* 提取阿拉伯数字 */
      const char *p = user_text;
      while (*p && found_count < 10)
        {
          if ((*p >= '0' && *p <= '9') || *p == '-')
            {
              numbers[found_count++] = atof(p);
              while (*p && ((*p >= '0' && *p <= '9') || *p == '.' || *p == '-'))
                p++;
            }
          else
            {
              p++;
            }
        }

      syslog(LOG_INFO, "Found %d Arabic numbers\n", found_count);

      /* 如果阿拉伯数字不足，尝试提取中文数字 */
      if (found_count < 2)
        {
          /* 查找关键词附近的中文数字 - 扩展搜索范围 */
          const char *keywords[] = {"最低温", "低温", "最高温", "高温", "调到", "设为", "改成", NULL};

          for (int i = 0; keywords[i] != NULL && found_count < 10; i++)
            {
              const char *kw_pos = strstr(user_text, keywords[i]);
              if (kw_pos)
                {
                  /* 在关键词后面查找中文数字，跳过空格和"阈值"等词 */
                  const char *num_start = kw_pos + strlen(keywords[i]);

                  /* 跳过"阈值"、"度"、空格等 */
                  while (*num_start && (strncmp(num_start, "阈值", strlen("阈值")) == 0 ||
                                       strncmp(num_start, "度", strlen("度")) == 0 ||
                                       *num_start == ' '))
                    {
                      if (strncmp(num_start, "阈值", strlen("阈值")) == 0)
                        num_start += strlen("阈值");
                      else if (strncmp(num_start, "度", strlen("度")) == 0)
                        num_start += strlen("度");
                      else
                        num_start++;
                    }

                  /* 尝试解析中文数字（扫描50个字节）*/
                  int cn_num = parse_chinese_number(num_start, 50);
                  if (cn_num >= 5 && cn_num <= 50)  /* 合理的温度范围 */
                    {
                      /* 避免重复添加相同的数字 */
                      bool duplicate = false;
                      for (int j = 0; j < found_count; j++)
                        {
                          if (fabs(numbers[j] - (float)cn_num) < 0.1)
                            {
                              duplicate = true;
                              break;
                            }
                        }

                      if (!duplicate)
                        {
                          numbers[found_count++] = (float)cn_num;
                          syslog(LOG_INFO, "Found Chinese number: %d after keyword '%s'\n",
                                 cn_num, keywords[i]);
                        }
                    }
                }
            }

          syslog(LOG_INFO, "Intent detection: found %d numbers total\n", found_count);
        }

      /* 分析找到的数字 */
      if (found_count >= 1)  /* 至少找到一个数字就尝试执行 */
        {
          /* 先加载当前配置获取现有阈值 */
          struct routine_settings_s settings;
          if (routine_config_load(&settings) == 0)
            {
              low = settings.temperature_low_x100 / 100.0;
              high = settings.temperature_high_x100 / 100.0;
            }

          syslog(LOG_INFO, "Current thresholds: low=%.1f high=%.1f\n", low, high);

          /* 根据找到的数字更新 */
          if (found_count == 1)
            {
              /* 只有一个数字，判断是设置低温还是高温 */
              if (strstr(user_text, "最低") || strstr(user_text, "低温"))
                {
                  low = numbers[0];
                  syslog(LOG_INFO, "Setting only LOW threshold to %.1f (high stays %.1f)\n", low, high);
                }
              else if (strstr(user_text, "最高") || strstr(user_text, "高温"))
                {
                  high = numbers[0];
                  syslog(LOG_INFO, "Setting only HIGH threshold to %.1f (low stays %.1f)\n", high, low);
                }
              else
                {
                  /* 不确定，假设是低温 */
                  low = numbers[0];
                  syslog(LOG_INFO, "Ambiguous, assuming LOW threshold: %.1f (high stays %.1f)\n", low, high);
                }
            }
          else if (found_count >= 2)
            {
              /* 两个或更多数字，选择前两个 */
              float num1 = numbers[0];
              float num2 = numbers[1];

              /* 根据关键词位置判断哪个是低温，哪个是高温 */
              const char *low_pos = NULL;
              const char *high_pos = NULL;

              if (strstr(user_text, "最低"))
                low_pos = strstr(user_text, "最低");
              else if (strstr(user_text, "低温"))
                low_pos = strstr(user_text, "低温");

              if (strstr(user_text, "最高"))
                high_pos = strstr(user_text, "最高");
              else if (strstr(user_text, "高温"))
                high_pos = strstr(user_text, "高温");

              /* 如果有明确的关键词，按位置判断 */
              if (low_pos && high_pos)
                {
                  if (low_pos < high_pos)
                    {
                      low = num1;
                      high = num2;
                    }
                  else
                    {
                      low = num2;
                      high = num1;
                    }
                }
              else
                {
                  /* 没有明确关键词，按数值大小判断 */
                  if (num1 < num2)
                    {
                      low = num1;
                      high = num2;
                    }
                  else
                    {
                      low = num2;
                      high = num1;
                    }
                }

              syslog(LOG_INFO, "Setting BOTH thresholds: low=%.1f high=%.1f\n", low, high);
            }

          /* 验证合理性：低温必须小于高温 */
          if (low >= high)
            {
              syslog(LOG_WARNING, "Invalid thresholds: low(%.1f) >= high(%.1f), swapping\n", low, high);
              float temp = low;
              low = high;
              high = temp;
            }

          /* 验证温度范围合理性 (0-50℃) */
          if (low < 0.0 || low > 50.0 || high < 0.0 || high > 50.0)
            {
              syslog(LOG_ERR, "Temperature out of range: low=%.1f high=%.1f\n", low, high);
              if (action_result != NULL)
                {
                  snprintf(action_result, result_size, "温度范围不合理（应在0-50℃）");
                }
              return -1;
            }

          char func_result[256];
          syslog(LOG_INFO, "Executing: set_temperature_threshold(low=%.1f, high=%.1f)\n", low, high);

          int ret = mimo_set_temperature_threshold(service, low, high,
                                                   func_result, sizeof(func_result));

          if (ret == OK && action_result != NULL)
            {
              snprintf(action_result, result_size, "已设置温度阈值：低温%.1f℃，高温%.1f℃", low, high);
              syslog(LOG_INFO, "Temperature threshold set successfully\n");
              return 1;  /* 表示执行了操作 */
            }
          else
            {
              syslog(LOG_ERR, "Failed to set temperature threshold: ret=%d\n", ret);
              if (action_result != NULL)
                {
                  snprintf(action_result, result_size, "设置阈值失败");
                }
              return -1;
            }
        }
      else
        {
          syslog(LOG_WARNING, "Intent detected but no valid numbers found\n");
        }
    }
  else
    {
      syslog(LOG_DEBUG, "No temperature keywords found\n");
    }

  /* 检测添加提醒意图 */
  if (strstr(user_text, "提醒") || strstr(user_text, "闹钟") || strstr(user_text, "定时"))
    {
      syslog(LOG_INFO, "Reminder keywords found, extracting time...\n");

      int hour = -1, minute = 0;

      /* 方法1: 尝试查找"X点"格式（包括中文数字）*/
      const char *dian_pos = strstr(user_text, "点");
      if (dian_pos && dian_pos > user_text)
        {
          /* 向前查找数字，最多扫描6个字节（两个中文字符）*/
          const char *start = dian_pos - 1;
          while (start > user_text && *start == ' ') start--;

          /* 尝试阿拉伯数字 */
          if (*start >= '0' && *start <= '9')
            {
              while (start > user_text && *(start-1) >= '0' && *(start-1) <= '9')
                start--;
              hour = atoi(start);
              syslog(LOG_INFO, "Found Arabic hour from '点': %d\n", hour);
            }
          else
            {
              /* 尝试中文数字，向前最多12字节 */
              int max_scan = 12;
              const char *scan_start = (dian_pos - max_scan) > user_text ? (dian_pos - max_scan) : user_text;
              int cn_hour = parse_chinese_number(scan_start, dian_pos - scan_start);
              if (cn_hour >= 1 && cn_hour <= 23)
                {
                  hour = cn_hour;
                  syslog(LOG_INFO, "Found Chinese hour from '点': %d\n", hour);
                }
            }

          /* 查找"半"表示30分钟 */
          if (strstr(dian_pos, "半"))
            {
              minute = 30;
              syslog(LOG_INFO, "Found '半', minute=30\n");
            }
        }

      /* 方法2: 如果还没找到，尝试 HH:MM 格式 */
      if (hour == -1)
        {
          const char *p = user_text;
          while (*p)
            {
              if (*p >= '0' && *p <= '9')
                {
                  int h = atoi(p);
                  const char *colon = strchr(p, ':');
                  if (colon && colon - p <= 2 && h >= 0 && h <= 23)
                    {
                      hour = h;
                      minute = atoi(colon + 1);
                      syslog(LOG_INFO, "Found time from HH:MM: %02d:%02d\n", hour, minute);
                      break;
                    }
                }
              p++;
            }
        }

      if (hour >= 0 && hour <= 23)
        {
          /* 提取提醒内容 */
          char reminder_text[ROUTINE_SCHEDULE_TEXT_MAX + 1] = "提醒";

          /* 在"提醒我"后面查找内容 */
          const char *remind_pos = strstr(user_text, "提醒");
          if (remind_pos)
            {
              const char *text_start = remind_pos + strlen("提醒");
              if (strncmp(text_start, "我", strlen("我")) == 0)
                text_start += strlen("我");

              /* 跳过时间和空格 */
              while (*text_start && (*text_start == ' ' ||
                     (*text_start >= '0' && *text_start <= '9') ||
                     strncmp(text_start, "点", 3) == 0 ||
                     strncmp(text_start, "半", 3) == 0 ||
                     *text_start == ':'))
                {
                  if (strncmp(text_start, "点", 3) == 0 ||
                      strncmp(text_start, "半", 3) == 0)
                    text_start += 3;
                  else
                    text_start++;
                }

              /* 复制剩余内容 */
              int len = strlen(text_start);
              if (len > 0 && len < ROUTINE_SCHEDULE_TEXT_MAX)
                {
                  strncpy(reminder_text, text_start, ROUTINE_SCHEDULE_TEXT_MAX);
                  reminder_text[ROUTINE_SCHEDULE_TEXT_MAX] = '\0';

                  /* 移除末尾句号 */
                  len = strlen(reminder_text);
                  if (len >= 3 && strcmp(&reminder_text[len-3], "。") == 0)
                    reminder_text[len-3] = '\0';
                }
            }

          if (strlen(reminder_text) < 2)
            snprintf(reminder_text, sizeof(reminder_text), "提醒事项");

          syslog(LOG_INFO, "Adding reminder: %02d:%02d - %s\n", hour, minute, reminder_text);

          char func_result[256];
          int ret = mimo_add_schedule_reminder(service, hour, minute,
                                              reminder_text,
                                              func_result, sizeof(func_result));

          if (ret == OK && action_result != NULL)
            {
              snprintf(action_result, result_size, "已添加%02d:%02d的提醒：%s",
                       hour, minute, reminder_text);
              syslog(LOG_INFO, "Reminder added successfully\n");
              return 1;
            }
          else
            {
              syslog(LOG_ERR, "Failed to add reminder: ret=%d\n", ret);
              if (action_result != NULL)
                snprintf(action_result, result_size, "添加提醒失败");
              return -1;
            }
        }
      else
        {
          syslog(LOG_WARNING, "Reminder detected but invalid time: hour=%d\n", hour);
        }
    }

  return 0;
}

int mimo_tts(FAR struct mimo_service_s *service,
             FAR const char *text,
             FAR struct mimo_tts_result_s *result)
{
  int ret;
  struct stat st;

  if (service == NULL || text == NULL || result == NULL)
    {
      return -EINVAL;
    }

  if (!service->config.enabled || strlen(service->config.api_key) == 0)
    {
      return -ENOTSUP;
    }

  ret = mimo_ensure_tmp_dir();
  if (ret < 0)
    {
      return ret;
    }

  memset(result, 0, sizeof(struct mimo_tts_result_s));
  service->state = MIMO_STATE_TTS_PROCESSING;

  const char *host = mimo_get_host(service->config.api_key);

  syslog(LOG_INFO, "Calling MiMo TTS for: %s\n", text);

  /* 使用 vela_https_request 调用 TTS API */
  ret = mimo_call_tts_api(host, service->config.api_key,
                          "mimo-v2.5-tts", text,
                          service->config.tts_voice,
                          MIMO_TTS_OUTPUT);

  if (ret != OK)
    {
      syslog(LOG_ERR, "MiMo TTS failed: %d\n", ret);
      service->state = MIMO_STATE_ERROR;
      return ret;
    }

  /* 检查输出文件 */
  if (stat(MIMO_TTS_OUTPUT, &st) == 0 && st.st_size > 0)
    {
      strncpy(result->audio_path, MIMO_TTS_OUTPUT, sizeof(result->audio_path) - 1);
      result->duration_ms = (st.st_size * 1000) / (16000 * 2); /* 16kHz, 16-bit */
      result->success = true;
      syslog(LOG_INFO, "MiMo TTS output: %s (%lu ms)\n",
             result->audio_path, (unsigned long)result->duration_ms);
    }
  else
    {
      syslog(LOG_ERR, "MiMo TTS output file invalid\n");
      service->state = MIMO_STATE_ERROR;
      return -EIO;
    }

  service->state = MIMO_STATE_IDLE;
  return OK;
}

int mimo_service_update_config(FAR struct mimo_service_s *service,
                               FAR const struct mimo_config_s *config)
{
  if (service == NULL || config == NULL)
    {
      return -EINVAL;
    }

  memcpy(&service->config, config, sizeof(struct mimo_config_s));

  syslog(LOG_INFO, "MiMo config updated: enabled=%d, model=%s\n",
         service->config.enabled, service->config.model);

  return OK;
}

void mimo_service_shutdown(FAR struct mimo_service_s *service)
{
  if (service != NULL)
    {
      service->initialized = false;
      service->state = MIMO_STATE_IDLE;
      syslog(LOG_INFO, "MiMo service shutdown\n");
    }
}
