/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/voice_recognizer_online.c
 *
 * 在线语音识别实现
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "voice_recognizer_online.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* 简化的唤醒词检测 - 在实际应用中应该扩展 KWS 模型
 * 这里使用离线 KWS 模型的 VOICE_COMMAND_WAKE 类别
 */
bool voice_recognizer_detect_wake_word(FAR const char *pcm_path,
                                       FAR uint16_t *score)
{
  struct voice_recognition_result_s result;
  int ret;

  if (pcm_path == NULL || score == NULL)
    {
      return false;
    }

  /* 调用现有的 KWS 识别器 */
  ret = voice_recognizer_recognize(pcm_path, &result);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "Wake word detection failed: %d\n", ret);
      return false;
    }

  *score = result.score;

  /* 检查是否为唤醒词类别 */
  if (result.command == VOICE_COMMAND_WAKE && result.score > 180)
    {
      syslog(LOG_INFO, "Wake word detected! Score: %u\n", result.score);
      return true;
    }

  return false;
}

/* 判断是否为离线固定指令查询 */
static enum voice_command_online_e
map_offline_command(enum voice_command_e cmd)
{
  switch (cmd)
    {
      case VOICE_COMMAND_QUERY_TEMPERATURE:
        return VOICE_COMMAND_ONLINE_QUERY_TEMPERATURE;
      case VOICE_COMMAND_QUERY_HUMIDITY:
        return VOICE_COMMAND_ONLINE_QUERY_HUMIDITY;
      case VOICE_COMMAND_QUERY_LIGHT:
        return VOICE_COMMAND_ONLINE_QUERY_LIGHT;
      case VOICE_COMMAND_QUERY_AIR_QUALITY:
        return VOICE_COMMAND_ONLINE_QUERY_AIR_QUALITY;
      case VOICE_COMMAND_WAKE:
        return VOICE_COMMAND_ONLINE_WAKE_WORD;
      default:
        return VOICE_COMMAND_ONLINE_UNKNOWN;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int voice_recognizer_online_recognize(
  FAR const char *pcm_path,
  FAR struct mimo_service_s *mimo_service,
  bool online_mode,
  FAR struct voice_recognition_online_result_s *result)
{
  struct voice_recognition_result_s offline_result;
  int ret;

  if (pcm_path == NULL || result == NULL)
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(struct voice_recognition_online_result_s));
  result->is_online_mode = online_mode;

  /* 在线模式：直接使用 MiMo，不调用 KWS */
  if (online_mode && mimo_service != NULL &&
      mimo_service->config.enabled &&
      strlen(mimo_service->config.api_key) > 0)
    {
      struct mimo_asr_result_s asr_result;
      struct mimo_llm_response_s llm_response;

      syslog(LOG_INFO, "Online mode: using MiMo directly, skipping KWS\n");

      /* ASR: 语音转文字 */
      ret = mimo_asr(mimo_service, pcm_path, &asr_result);
      if (ret < 0 || !asr_result.success)
        {
          syslog(LOG_WARNING, "MiMo ASR failed\n");
          result->command = VOICE_COMMAND_ONLINE_UNKNOWN;
          result->success = false;
          return -ENODATA;
        }

      strncpy(result->text, asr_result.text, sizeof(result->text) - 1);
      syslog(LOG_INFO, "ASR result: %s\n", result->text);

      /* LLM: 对话理解与生成 */
      ret = mimo_llm_chat(mimo_service, asr_result.text, &llm_response);
      if (ret < 0 || !llm_response.success)
        {
          syslog(LOG_WARNING, "MiMo LLM failed\n");
          result->command = VOICE_COMMAND_ONLINE_CHAT;
          result->success = false;
          return -ENODATA;
        }

      strncpy(result->response, llm_response.text, sizeof(result->response) - 1);
      syslog(LOG_INFO, "LLM response: %s\n", result->response);

      /* 检测并执行用户意图（设置阈值、添加提醒等）*/
      char action_result[128];
      int intent_ret = mimo_detect_and_execute_intent(mimo_service,
                                                     asr_result.text,
                                                     llm_response.text,
                                                     action_result,
                                                     sizeof(action_result));

      if (intent_ret > 0)
        {
          /* 执行了操作，将结果附加到响应中 */
          syslog(LOG_INFO, "Action executed: %s\n", action_result);

          /* 如果响应太长，用操作结果替换 */
          if (strlen(result->response) + strlen(action_result) + 10 < sizeof(result->response))
            {
              strncat(result->response, "\n✓ ", sizeof(result->response) - strlen(result->response) - 1);
              strncat(result->response, action_result, sizeof(result->response) - strlen(result->response) - 1);
            }
          else
            {
              strncpy(result->response, action_result, sizeof(result->response) - 1);
            }
        }

      result->command = VOICE_COMMAND_ONLINE_CHAT;
      result->success = true;
      return OK;
    }

  /* 离线模式或在线模式失败 */
  result->command = VOICE_COMMAND_ONLINE_UNKNOWN;
  result->success = false;
  syslog(LOG_INFO, "Voice recognition returned unknown\n");

  return OK;
}
