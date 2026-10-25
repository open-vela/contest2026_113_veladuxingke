/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/voice_wakeup_service.h
 *
 * 语音唤醒服务 - VAD 持续监听 + MIMO 在线识别
 ****************************************************************************/

#ifndef __VOICE_WAKEUP_SERVICE_H
#define __VOICE_WAKEUP_SERVICE_H

#include <nuttx/config.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "vad_detector.h"
#include "mimo_integration.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define VOICE_WAKEUP_AUDIO_BUFFER_SIZE 112000  /* 7 秒 @ 16kHz 16-bit */

/* 唤醒服务状态 */
enum voice_wakeup_state_e
{
  VOICE_WAKEUP_IDLE = 0,
  VOICE_WAKEUP_LISTENING,           /* 监听中 */
  VOICE_WAKEUP_VOICE_DETECTED,      /* 检测到语音 */
  VOICE_WAKEUP_RECOGNIZING,         /* 识别中 */
  VOICE_WAKEUP_WAKEWORD_DETECTED,   /* 唤醒词确认 */
  VOICE_WAKEUP_PROCESSING_COMMAND,  /* 处理指令 */
  VOICE_WAKEUP_SPEAKING,            /* 回复中 */
  VOICE_WAKEUP_ERROR
};

/* 唤醒回调函数类型 */
typedef void (*voice_wakeup_callback_t)(FAR void *arg,
                                        FAR const char *user_text,
                                        FAR const char *response_text);

/* 唤醒服务配置 */
struct voice_wakeup_config_s
{
  FAR const char *capture_device;
  FAR const char *playback_device;
  FAR const char *temp_audio_dir;
  struct vad_config_s vad_config;
  struct mimo_config_s mimo_config;
  voice_wakeup_callback_t callback;
  FAR void *callback_arg;
  uint32_t max_recording_ms;        /* 最大录音时长 */
  uint32_t post_speech_ms;          /* 语音结束后继续录音时长 */
  int priority;
  size_t stack_size;
};

/* 唤醒服务实例 */
struct voice_wakeup_service_s
{
  pthread_t thread;
  pthread_mutex_t lock;
  struct voice_wakeup_config_s config;
  struct vad_detector_s vad;
  struct mimo_service_s mimo;
  enum voice_wakeup_state_e state;
  int audio_fd;
  bool initialized;
  bool thread_created;
  bool running;
  bool shutdown_requested;
  uint32_t wakeup_count;
  uint32_t command_count;
};

/**
 * 初始化语音唤醒服务
 */
int voice_wakeup_service_init(FAR struct voice_wakeup_service_s *service,
                              FAR const struct voice_wakeup_config_s *config);

/**
 * 启动语音唤醒服务（后台线程持续监听）
 */
int voice_wakeup_service_start(FAR struct voice_wakeup_service_s *service);

/**
 * 停止语音唤醒服务
 */
int voice_wakeup_service_stop(FAR struct voice_wakeup_service_s *service);

/**
 * 获取服务状态
 */
enum voice_wakeup_state_e
voice_wakeup_service_get_state(FAR struct voice_wakeup_service_s *service);

/**
 * 更新 MIMO 配置
 */
int voice_wakeup_service_update_mimo(
  FAR struct voice_wakeup_service_s *service,
  FAR const struct mimo_config_s *config);

/**
 * 关闭服务
 */
void voice_wakeup_service_shutdown(FAR struct voice_wakeup_service_s *service);

#ifdef __cplusplus
}
#endif

#endif /* __VOICE_WAKEUP_SERVICE_H */
