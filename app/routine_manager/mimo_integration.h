/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/mimo_integration.h
 *
 * MiMo 在线语音助手集成
 ****************************************************************************/

#ifndef __MIMO_INTEGRATION_H
#define __MIMO_INTEGRATION_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MIMO_API_KEY_MAX 128
#define MIMO_MODEL_NAME_MAX 64
#define MIMO_TTS_VOICE_MAX 32
#define MIMO_RESPONSE_MAX 2048

/* MiMo 配置 */
struct mimo_config_s
{
  char api_key[MIMO_API_KEY_MAX];
  char model[MIMO_MODEL_NAME_MAX];
  char tts_voice[MIMO_TTS_VOICE_MAX];
  bool enabled;
};

/* MiMo ASR 结果 */
struct mimo_asr_result_s
{
  char text[512];
  int confidence;
  bool success;
};

/* MiMo LLM 响应 */
struct mimo_llm_response_s
{
  char text[MIMO_RESPONSE_MAX];
  bool success;
  int error_code;
};

/* MiMo TTS 结果 */
struct mimo_tts_result_s
{
  char audio_path[128];
  uint32_t duration_ms;
  bool success;
};

/* MiMo 服务状态 */
enum mimo_service_state_e
{
  MIMO_STATE_IDLE = 0,
  MIMO_STATE_ASR_PROCESSING,
  MIMO_STATE_LLM_PROCESSING,
  MIMO_STATE_TTS_PROCESSING,
  MIMO_STATE_ERROR
};

/* MiMo 传感器数据 */
struct mimo_sensor_data_s
{
  int32_t temperature_x100;   /* 温度 × 100 */
  uint16_t humidity_x100;     /* 湿度 × 100 */
  uint32_t light_x10;         /* 光照 × 10 */
  uint16_t eco2_ppm;          /* CO2 浓度 (ppm) */
  uint16_t tvoc_ppb;          /* TVOC 浓度 (ppb) */
  bool valid;                 /* 数据是否有效 */
};

/* MiMo 服务实例 */
struct mimo_service_s
{
  struct mimo_config_s config;
  enum mimo_service_state_e state;
  bool initialized;
  FAR const struct mimo_sensor_data_s *sensor_data;  /* 传感器数据指针 */
  FAR void *control_context;  /* 控制上下文（用于回调）*/
};

/* 控制回调函数类型 */
typedef int (*mimo_set_threshold_callback_t)(const char *sensor, float low, float high, void *context);
typedef int (*mimo_add_reminder_callback_t)(int hour, int minute, const char *text, void *context);

/**
 * 设置控制回调（用于 AI 操控设备）
 */
void mimo_service_set_control_callbacks(FAR struct mimo_service_s *service,
                                        mimo_set_threshold_callback_t set_threshold_cb,
                                        mimo_add_reminder_callback_t add_reminder_cb,
                                        FAR void *context);

/**
 * 初始化 MiMo 服务
 */
int mimo_service_init(FAR struct mimo_service_s *service,
                      FAR const struct mimo_config_s *config);

/**
 * ASR: 语音转文字
 * @param service MiMo 服务实例
 * @param pcm_path 输入 PCM 文件路径 (16kHz, 16-bit, mono)
 * @param result 输出 ASR 结果
 */
int mimo_asr(FAR struct mimo_service_s *service,
             FAR const char *pcm_path,
             FAR struct mimo_asr_result_s *result);

/**
 * LLM: 对话理解与生成
 * @param service MiMo 服务实例
 * @param user_text 用户输入文本
 * @param response 输出 LLM 响应
 */
int mimo_llm_chat(FAR struct mimo_service_s *service,
                  FAR const char *user_text,
                  FAR struct mimo_llm_response_s *response);

/**
 * 检测用户意图并执行操作（简化版 Function Calling）
 * @param service MiMo 服务实例
 * @param user_text 用户输入文本
 * @param llm_response LLM 的回复
 * @param action_result 输出操作结果描述
 * @param result_size 结果缓冲区大小
 * @return 0=未执行操作，1=执行了操作，<0=错误
 */
int mimo_detect_and_execute_intent(FAR struct mimo_service_s *service,
                                   const char *user_text,
                                   const char *llm_response,
                                   char *action_result,
                                   size_t result_size);

/**
 * TTS: 文字转语音
 * @param service MiMo 服务实例
 * @param text 输入文本
 * @param result 输出 TTS 结果（包含音频文件路径）
 */
int mimo_tts(FAR struct mimo_service_s *service,
             FAR const char *text,
             FAR struct mimo_tts_result_s *result);

/**
 * 更新配置
 */
int mimo_service_update_config(FAR struct mimo_service_s *service,
                               FAR const struct mimo_config_s *config);

/**
 * 设置传感器数据（用于 Function Calling）
 */
void mimo_service_set_sensor_data(FAR struct mimo_service_s *service,
                                   FAR const struct mimo_sensor_data_s *sensor_data);

/**
 * 关闭服务
 */
void mimo_service_shutdown(FAR struct mimo_service_s *service);

#ifdef __cplusplus
}
#endif

#endif /* __MIMO_INTEGRATION_H */
