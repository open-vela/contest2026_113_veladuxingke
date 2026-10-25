/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/voice_recognizer_online.h
 *
 * 在线语音识别（支持唤醒词 + MiMo）
 ****************************************************************************/

#ifndef __VOICE_RECOGNIZER_ONLINE_H
#define __VOICE_RECOGNIZER_ONLINE_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include "mimo_integration.h"
#include "voice_recognizer.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 扩展的语音命令，包含唤醒词 */
enum voice_command_online_e
{
  VOICE_COMMAND_ONLINE_NONE = 0,
  VOICE_COMMAND_ONLINE_WAKE_WORD,           /* 你好，openvela */
  VOICE_COMMAND_ONLINE_QUERY_TEMPERATURE,   /* 离线固定指令 */
  VOICE_COMMAND_ONLINE_QUERY_HUMIDITY,
  VOICE_COMMAND_ONLINE_QUERY_LIGHT,
  VOICE_COMMAND_ONLINE_QUERY_AIR_QUALITY,
  VOICE_COMMAND_ONLINE_CHAT,                /* MiMo 智能对话 */
  VOICE_COMMAND_ONLINE_UNKNOWN
};

/* 在线语音识别结果 */
struct voice_recognition_online_result_s
{
  enum voice_command_online_e command;
  char text[512];                /* ASR 文本（在线模式） */
  char response[2048];           /* LLM 响应（在线模式） */
  uint16_t score;
  bool is_wake_word;
  bool is_online_mode;
  bool success;
};

/**
 * 在线语音识别（支持唤醒词检测 + MiMo 对话）
 *
 * @param pcm_path PCM 文件路径
 * @param mimo_service MiMo 服务实例（NULL 表示离线模式）
 * @param online_mode true = 在线模式，false = 离线模式
 * @param result 输出识别结果
 * @return 0 成功，负数为错误码
 */
int voice_recognizer_online_recognize(
  FAR const char *pcm_path,
  FAR struct mimo_service_s *mimo_service,
  bool online_mode,
  FAR struct voice_recognition_online_result_s *result);

/**
 * 检测是否为唤醒词 "你好，openvela"
 *
 * @param pcm_path PCM 文件路径
 * @param score 输出置信度分数
 * @return true = 检测到唤醒词，false = 未检测到
 */
bool voice_recognizer_detect_wake_word(FAR const char *pcm_path,
                                       FAR uint16_t *score);

#ifdef __cplusplus
}
#endif

#endif /* __VOICE_RECOGNIZER_ONLINE_H */
