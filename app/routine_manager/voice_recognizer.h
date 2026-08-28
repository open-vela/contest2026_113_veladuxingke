/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/voice_recognizer.h
 ****************************************************************************/

#ifndef __VOICE_RECOGNIZER_H
#define __VOICE_RECOGNIZER_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum voice_command_e
{
  VOICE_COMMAND_NONE = 0,
  VOICE_COMMAND_QUERY_TEMPERATURE,
  VOICE_COMMAND_QUERY_HUMIDITY,
  VOICE_COMMAND_QUERY_LIGHT,
  VOICE_COMMAND_QUERY_AIR_QUALITY,
  VOICE_COMMAND_WAKE,
  VOICE_COMMAND_UNKNOWN
};

struct voice_recognition_result_s
{
  enum voice_command_e command;
  uint16_t score;
  bool semantic_verified;
};

int voice_recognizer_recognize(
  FAR const char *pcm_path,
  FAR struct voice_recognition_result_s *result);
int voice_recognizer_selftest(void);
int voice_recognizer_demo_recognize(
  FAR const char *pcm_path,
  FAR struct voice_recognition_result_s *result);
int voice_recognizer_demo_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* __VOICE_RECOGNIZER_H */
