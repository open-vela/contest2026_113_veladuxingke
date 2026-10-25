/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/voice_wakeup_demo.c
 *
 * 语音唤醒服务演示程序
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>

#include "voice_wakeup_service.h"

static volatile bool g_running = true;

static void signal_handler(int signo)
{
  g_running = false;
}

static void wakeup_callback(FAR void *arg, FAR const char *user_text,
                           FAR const char *response_text)
{
  printf("\n========================================\n");
  printf("语音交互完成:\n");
  printf("用户: %s\n", user_text);
  printf("回复: %s\n", response_text);
  printf("========================================\n\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int voice_wakeup_demo_main(int argc, char *argv[])
{
  struct voice_wakeup_service_s service;
  struct voice_wakeup_config_s config;
  const char *api_key = NULL;
  int ret;
  int i;

  printf("语音唤醒服务演示 - MIMO 2.6\n");
  printf("=====================================\n\n");

  /* 解析命令行参数 */
  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "-k") == 0 && i + 1 < argc)
        {
          api_key = argv[++i];
        }
      else if (strcmp(argv[i], "-h") == 0)
        {
          printf("用法: %s [-k API_KEY]\n", argv[0]);
          printf("  -k API_KEY   设置 MIMO API Key\n");
          printf("  -h           显示帮助\n");
          printf("\n");
          printf("说明:\n");
          printf("  1. 程序启动后会持续监听麦克风\n");
          printf("  2. 检测到人声后开始录音\n");
          printf("  3. 语音结束后上传 MIMO ASR 识别\n");
          printf("  4. 如果识别出'你好Vela'，则继续处理指令\n");
          printf("  5. MIMO LLM 生成回复并通过 TTS 播放\n");
          printf("  6. 按 Ctrl+C 退出\n");
          printf("\n");
          printf("示例:\n");
          printf("  %s -k tp-xxxxxxxxxxxxxxxx\n", argv[0]);
          return 0;
        }
    }

  if (api_key == NULL)
    {
      fprintf(stderr, "错误: 需要提供 MIMO API Key\n");
      fprintf(stderr, "使用 -h 查看帮助\n");
      return 1;
    }

  /* 配置服务 */
  memset(&config, 0, sizeof(config));

  /* 音频设备配置 */
  config.capture_device = "/dev/audio/pcm1c";
  config.playback_device = "/dev/audio/pcm0p";
  config.temp_audio_dir = "/data/voice_wakeup";

  /* VAD 配置 */
  config.vad_config.sample_rate = 16000;
  config.vad_config.frame_samples = 320;
  config.vad_config.minimum_rms = 350;
  config.vad_config.minimum_peak = 900;
  config.vad_config.speech_frames = 3;
  config.vad_config.silence_frames = 20;

  /* MIMO 配置 */
  strncpy(config.mimo_config.api_key, api_key, MIMO_API_KEY_MAX - 1);
  strncpy(config.mimo_config.model, "mimo-v2.6-pro", MIMO_MODEL_NAME_MAX - 1);
  strncpy(config.mimo_config.tts_voice, "冰糖", MIMO_TTS_VOICE_MAX - 1);
  config.mimo_config.enabled = true;

  /* 录音配置 */
  config.max_recording_ms = 7000;   /* 最长 7 秒 */
  config.post_speech_ms = 500;      /* 语音结束后继续录 0.5 秒 */

  /* 回调 */
  config.callback = wakeup_callback;
  config.callback_arg = NULL;

  /* 线程配置 */
  config.priority = 100;
  config.stack_size = 16384;

  /* 初始化服务 */
  printf("正在初始化语音唤醒服务...\n");
  ret = voice_wakeup_service_init(&service, &config);
  if (ret < 0)
    {
      fprintf(stderr, "初始化失败: %d\n", ret);
      return 1;
    }

  /* 启动服务 */
  printf("正在启动服务...\n");
  ret = voice_wakeup_service_start(&service);
  if (ret < 0)
    {
      fprintf(stderr, "启动失败: %d\n", ret);
      voice_wakeup_service_shutdown(&service);
      return 1;
    }

  printf("\n✓ 服务已启动，正在监听...\n");
  printf("  - 说 '你好Vela' 唤醒设备\n");
  printf("  - 然后说出你的指令\n");
  printf("  - 按 Ctrl+C 退出\n\n");

  /* 设置信号处理 */
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  /* 主循环 - 显示状态 */
  while (g_running)
    {
      enum voice_wakeup_state_e state;
      state = voice_wakeup_service_get_state(&service);

      const char *state_str = "未知";
      switch (state)
        {
          case VOICE_WAKEUP_IDLE:
            state_str = "空闲";
            break;
          case VOICE_WAKEUP_LISTENING:
            state_str = "监听中...";
            break;
          case VOICE_WAKEUP_VOICE_DETECTED:
            state_str = "检测到语音";
            break;
          case VOICE_WAKEUP_RECOGNIZING:
            state_str = "识别中 (ASR)";
            break;
          case VOICE_WAKEUP_WAKEWORD_DETECTED:
            state_str = "唤醒词确认";
            break;
          case VOICE_WAKEUP_PROCESSING_COMMAND:
            state_str = "处理指令 (LLM)";
            break;
          case VOICE_WAKEUP_SPEAKING:
            state_str = "回复中 (TTS)";
            break;
          case VOICE_WAKEUP_ERROR:
            state_str = "错误";
            break;
        }

      printf("\r状态: %-20s", state_str);
      fflush(stdout);

      sleep(1);
    }

  printf("\n\n正在停止服务...\n");
  voice_wakeup_service_stop(&service);
  voice_wakeup_service_shutdown(&service);

  printf("服务已停止\n");
  return 0;
}
