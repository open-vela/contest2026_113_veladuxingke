/****************************************************************************
 * contest2026_113_veladuxingke/app/lan_panel/voice_config_api.c
 *
 * 语音配置 API 实现
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <netutils/cJSON.h>

#include "voice_config_api.h"

#define ROUTINE_CONFIG_PATH "/data/routine/config.json"
#define ROUTINE_CONFIG_MAX_BYTES 4096

/* HTTP 响应辅助函数 */
static int send_response(int fd, int status_code, const char *status_text,
                        const char *content_type, const char *body)
{
  char header[512];
  size_t body_len = body ? strlen(body) : 0;

  snprintf(header, sizeof(header),
           "HTTP/1.1 %d %s\r\n"
           "Content-Type: %s\r\n"
           "Content-Length: %zu\r\n"
           "Connection: close\r\n"
           "\r\n",
           status_code, status_text, content_type, body_len);

  if (write(fd, header, strlen(header)) < 0)
    {
      return -errno;
    }

  if (body && body_len > 0)
    {
      if (write(fd, body, body_len) < 0)
        {
          return -errno;
        }
    }

  return 0;
}

static int send_json_error(int fd, int status_code, const char *message)
{
  char body[256];
  snprintf(body, sizeof(body), "{\"error\":\"%s\"}", message);
  return send_response(fd, status_code, "Error", "application/json", body);
}

/* GET /api/v1/voice/config */
int voice_config_api_get(int client_fd)
{
  char buffer[ROUTINE_CONFIG_MAX_BYTES];
  cJSON *root = NULL;
  cJSON *voice_config = NULL;
  char *response_json = NULL;
  int fd = -1;
  ssize_t bytes_read;
  int ret = -1;
  bool use_defaults = false;

  /* 读取配置文件 */
  fd = open(ROUTINE_CONFIG_PATH, O_RDONLY);
  if (fd < 0)
    {
      /* 配置文件不存在，使用默认值 */
      use_defaults = true;
    }
  else
    {
      bytes_read = read(fd, buffer, sizeof(buffer) - 1);
      close(fd);

      if (bytes_read <= 0)
        {
          use_defaults = true;
        }
      else
        {
          buffer[bytes_read] = '\0';

          /* 解析 JSON */
          root = cJSON_Parse(buffer);
          if (root == NULL)
            {
              use_defaults = true;
            }
        }
    }

  /* 提取语音相关配置 */
  voice_config = cJSON_CreateObject();
  if (voice_config == NULL)
    {
      if (root != NULL)
        {
          cJSON_Delete(root);
        }
      return send_json_error(client_fd, 500, "Memory allocation failed");
    }

  /* 读取语音模式 */
  cJSON *voice_mode = NULL;
  if (!use_defaults && root != NULL)
    {
      voice_mode = cJSON_GetObjectItemCaseSensitive(root, "voice_mode");
    }

  if (voice_mode && cJSON_IsString(voice_mode))
    {
      cJSON_AddStringToObject(voice_config, "voice_mode", voice_mode->valuestring);
    }
  else
    {
      cJSON_AddStringToObject(voice_config, "voice_mode", "offline");
    }

  /* 读取 MiMo 配置 */
  cJSON *mimo_enabled = NULL;
  cJSON *mimo_model = NULL;
  cJSON *mimo_tts_voice = NULL;
  cJSON *mimo_tts_style = NULL;
  cJSON *mimo_api_key = NULL;

  if (!use_defaults && root != NULL)
    {
      mimo_enabled = cJSON_GetObjectItemCaseSensitive(root, "mimo_enabled");
      mimo_model = cJSON_GetObjectItemCaseSensitive(root, "mimo_model");
      mimo_tts_voice = cJSON_GetObjectItemCaseSensitive(root, "mimo_tts_voice");
      mimo_tts_style = cJSON_GetObjectItemCaseSensitive(root, "mimo_tts_style");
      mimo_api_key = cJSON_GetObjectItemCaseSensitive(root, "mimo_api_key");
    }

  cJSON_AddBoolToObject(voice_config, "mimo_enabled",
                        mimo_enabled && cJSON_IsTrue(mimo_enabled));

  if (mimo_model && cJSON_IsString(mimo_model))
    {
      cJSON_AddStringToObject(voice_config, "mimo_model", mimo_model->valuestring);
    }
  else
    {
      cJSON_AddStringToObject(voice_config, "mimo_model", "mimo-v2.6-pro");
    }

  if (mimo_tts_voice && cJSON_IsString(mimo_tts_voice))
    {
      cJSON_AddStringToObject(voice_config, "mimo_tts_voice", mimo_tts_voice->valuestring);
    }
  else
    {
      cJSON_AddStringToObject(voice_config, "mimo_tts_voice", "冰糖");
    }

  /* 读取 TTS 风格 */
  if (mimo_tts_style && cJSON_IsString(mimo_tts_style))
    {
      cJSON_AddStringToObject(voice_config, "mimo_tts_style", mimo_tts_style->valuestring);
    }
  else
    {
      cJSON_AddStringToObject(voice_config, "mimo_tts_style", "");
    }

  /* API Key 只返回是否已配置，不返回实际值（安全考虑） */
  bool has_api_key = mimo_api_key && cJSON_IsString(mimo_api_key) &&
                     strlen(mimo_api_key->valuestring) > 0;
  cJSON_AddBoolToObject(voice_config, "has_api_key", has_api_key);

  /* 生成响应 */
  response_json = cJSON_PrintUnformatted(voice_config);
  if (response_json)
    {
      ret = send_response(client_fd, 200, "OK", "application/json", response_json);
      free(response_json);
    }
  else
    {
      ret = send_json_error(client_fd, 500, "Failed to generate JSON");
    }

  cJSON_Delete(voice_config);
  if (root != NULL)
    {
      cJSON_Delete(root);
    }
  return ret;
}

/* POST /api/v1/voice/config */
int voice_config_api_post(int client_fd, const char *body, size_t body_len)
{
  char config_buffer[ROUTINE_CONFIG_MAX_BYTES];
  cJSON *root = NULL;
  cJSON *request = NULL;
  char *updated_json = NULL;
  char temp_path[128];
  int fd = -1;
  ssize_t bytes_read;
  bool modified = false;

  if (body == NULL || body_len == 0)
    {
      return send_json_error(client_fd, 400, "Empty request body");
    }

  /* 解析请求 */
  request = cJSON_Parse(body);
  if (request == NULL)
    {
      return send_json_error(client_fd, 400, "Invalid JSON");
    }

  /* 读取现有配置，如果不存在则创建默认配置 */
  fd = open(ROUTINE_CONFIG_PATH, O_RDONLY);
  if (fd < 0)
    {
      /* 配置文件不存在，创建默认配置 */
      root = cJSON_CreateObject();
      if (root == NULL)
        {
          cJSON_Delete(request);
          return send_json_error(client_fd, 500, "Memory allocation failed");
        }

      /* 添加默认值 */
      cJSON_AddNumberToObject(root, "schema", 1);
      cJSON_AddNumberToObject(root, "generation", 0);
      cJSON_AddNumberToObject(root, "temperature_low_x100", 1800);
      cJSON_AddNumberToObject(root, "temperature_high_x100", 2600);
      cJSON_AddNumberToObject(root, "history_interval_seconds", 300);

      /* 添加默认 schedule */
      cJSON *schedule = cJSON_CreateArray();
      if (schedule == NULL)
        {
          cJSON_Delete(root);
          cJSON_Delete(request);
          return send_json_error(client_fd, 500, "Memory allocation failed");
        }

      const char *default_texts[] = {"喝水", "专注", "通风"};
      int default_hours[] = {9, 14, 18};
      int default_minutes[] = {0, 30, 0};

      for (int i = 0; i < 3; i++)
        {
          cJSON *item = cJSON_CreateObject();
          if (item)
            {
              cJSON_AddNumberToObject(item, "id", i);
              cJSON_AddNumberToObject(item, "hour", default_hours[i]);
              cJSON_AddNumberToObject(item, "minute", default_minutes[i]);
              cJSON_AddStringToObject(item, "text", default_texts[i]);
              cJSON_AddItemToArray(schedule, item);
            }
        }
      cJSON_AddItemToObject(root, "schedule", schedule);

      /* 添加默认语音配置 */
      cJSON_AddStringToObject(root, "voice_mode", "offline");
      cJSON_AddBoolToObject(root, "mimo_enabled", false);
      cJSON_AddStringToObject(root, "mimo_model", "mimo-v2.6-pro");
      cJSON_AddStringToObject(root, "mimo_tts_voice", "冰糖");
    }
  else
    {
      bytes_read = read(fd, config_buffer, sizeof(config_buffer) - 1);
      close(fd);
      fd = -1;

      if (bytes_read <= 0)
        {
          cJSON_Delete(request);
          return send_json_error(client_fd, 500, "Failed to read config");
        }

      config_buffer[bytes_read] = '\0';

      /* 解析现有配置 */
      root = cJSON_Parse(config_buffer);
      if (root == NULL)
        {
          cJSON_Delete(request);
          return send_json_error(client_fd, 500, "Invalid config file");
        }
    }

  /* 更新语音模式 */
  cJSON *voice_mode = cJSON_GetObjectItemCaseSensitive(request, "voice_mode");
  if (voice_mode && cJSON_IsString(voice_mode))
    {
      const char *mode = voice_mode->valuestring;
      if (strcmp(mode, "online") == 0 || strcmp(mode, "offline") == 0)
        {
          cJSON_DeleteItemFromObject(root, "voice_mode");
          cJSON_AddStringToObject(root, "voice_mode", mode);
          modified = true;
        }
    }

  /* 更新 MiMo 启用状态 */
  cJSON *mimo_enabled = cJSON_GetObjectItemCaseSensitive(request, "mimo_enabled");
  if (mimo_enabled && cJSON_IsBool(mimo_enabled))
    {
      cJSON_DeleteItemFromObject(root, "mimo_enabled");
      cJSON_AddBoolToObject(root, "mimo_enabled", cJSON_IsTrue(mimo_enabled));
      modified = true;
    }

  /* 更新 API Key */
  cJSON *mimo_api_key = cJSON_GetObjectItemCaseSensitive(request, "mimo_api_key");
  if (mimo_api_key && cJSON_IsString(mimo_api_key))
    {
      const char *key = mimo_api_key->valuestring;
      if (strlen(key) > 0)
        {
          cJSON_DeleteItemFromObject(root, "mimo_api_key");
          cJSON_AddStringToObject(root, "mimo_api_key", key);
          modified = true;
        }
    }

  /* 更新模型 */
  cJSON *mimo_model = cJSON_GetObjectItemCaseSensitive(request, "mimo_model");
  if (mimo_model && cJSON_IsString(mimo_model))
    {
      cJSON_DeleteItemFromObject(root, "mimo_model");
      cJSON_AddStringToObject(root, "mimo_model", mimo_model->valuestring);
      modified = true;
    }

  /* 更新 TTS 音色 */
  cJSON *mimo_tts_voice = cJSON_GetObjectItemCaseSensitive(request, "mimo_tts_voice");
  if (mimo_tts_voice && cJSON_IsString(mimo_tts_voice))
    {
      cJSON_DeleteItemFromObject(root, "mimo_tts_voice");
      cJSON_AddStringToObject(root, "mimo_tts_voice", mimo_tts_voice->valuestring);
      modified = true;
    }

  /* 更新 TTS 风格 */
  cJSON *mimo_tts_style = cJSON_GetObjectItemCaseSensitive(request, "mimo_tts_style");
  if (mimo_tts_style && cJSON_IsString(mimo_tts_style))
    {
      cJSON_DeleteItemFromObject(root, "mimo_tts_style");
      cJSON_AddStringToObject(root, "mimo_tts_style", mimo_tts_style->valuestring);
      modified = true;
    }

  if (!modified)
    {
      cJSON_Delete(request);
      cJSON_Delete(root);
      return send_response(client_fd, 200, "OK", "application/json",
                          "{\"status\":\"no_changes\"}");
    }

  /* 增加 generation 版本号 */
  cJSON *generation = cJSON_GetObjectItemCaseSensitive(root, "generation");
  int gen_num = (generation && cJSON_IsNumber(generation)) ? generation->valueint : 0;
  cJSON_DeleteItemFromObject(root, "generation");
  cJSON_AddNumberToObject(root, "generation", gen_num + 1);

  /* 生成新的 JSON */
  updated_json = cJSON_PrintUnformatted(root);
  if (updated_json == NULL)
    {
      cJSON_Delete(request);
      cJSON_Delete(root);
      return send_json_error(client_fd, 500, "Failed to generate JSON");
    }

  /* 确保目录存在 */
  if (mkdir("/data/routine", 0700) < 0 && errno != EEXIST)
    {
      free(updated_json);
      cJSON_Delete(request);
      cJSON_Delete(root);
      return send_json_error(client_fd, 500, "Failed to create config directory");
    }

  /* 原子写入（写入临时文件再重命名） */
  snprintf(temp_path, sizeof(temp_path), "%s.tmp", ROUTINE_CONFIG_PATH);

  /* 删除可能存在的旧临时文件 */
  unlink(temp_path);

  fd = open(temp_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0)
    {
      free(updated_json);
      cJSON_Delete(request);
      cJSON_Delete(root);
      return send_json_error(client_fd, 500, "Failed to create temp file");
    }

  size_t json_len = strlen(updated_json);
  ssize_t written = write(fd, updated_json, json_len);

  if (written != (ssize_t)json_len || fsync(fd) < 0)
    {
      close(fd);
      unlink(temp_path);
      free(updated_json);
      cJSON_Delete(request);
      cJSON_Delete(root);
      return send_json_error(client_fd, 500, "Failed to write config");
    }

  close(fd);

  /* 重命名为正式文件 */
  if (rename(temp_path, ROUTINE_CONFIG_PATH) < 0)
    {
      unlink(temp_path);
      free(updated_json);
      cJSON_Delete(request);
      cJSON_Delete(root);
      return send_json_error(client_fd, 500, "Failed to save config");
    }

  free(updated_json);
  cJSON_Delete(request);
  cJSON_Delete(root);

  return send_response(client_fd, 200, "OK", "application/json",
                      "{\"status\":\"success\",\"message\":\"Voice config updated\"}");
}
