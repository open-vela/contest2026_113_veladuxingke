/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/voice_wakeup_service.c
 *
 * 语音唤醒服务实现 - VAD 持续监听 + MIMO 在线识别
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/audio/audio.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <system/nxplayer.h>

#include "voice_wakeup_service.h"

#define SAMPLE_RATE 16000
#define CHANNELS 1
#define BITS_PER_SAMPLE 16
#define FRAME_MS 20
#define FRAME_SAMPLES ((SAMPLE_RATE * FRAME_MS) / 1000)
#define FRAME_BYTES (FRAME_SAMPLES * sizeof(int16_t))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* 检查文本是否包含唤醒词 */
static bool contains_wakeword(FAR const char *text)
{
  if (text == NULL)
    {
      return false;
    }

  /* 检查是否包含 "你好Vela" 或相关变体 */
  if (strstr(text, "你好") != NULL && strstr(text, "Vela") != NULL)
    {
      return true;
    }

  if (strstr(text, "你好") != NULL && strstr(text, "vela") != NULL)
    {
      return true;
    }

  if (strstr(text, "你好维拉") != NULL)
    {
      return true;
    }

  return false;
}

/* 保存录音到文件 */
static int save_audio_file(FAR const char *path,
                          FAR const int16_t *buffer,
                          size_t samples)
{
  FILE *fp;
  size_t written;

  fp = fopen(path, "wb");
  if (fp == NULL)
    {
      syslog(LOG_ERR, "Failed to open %s for writing\n", path);
      return -errno;
    }

  written = fwrite(buffer, sizeof(int16_t), samples, fp);
  fclose(fp);

  if (written != samples)
    {
      syslog(LOG_ERR, "Failed to write audio data: %zu/%zu\n",
             written, samples);
      return -EIO;
    }

  syslog(LOG_INFO, "Saved audio: %s (%zu samples, %.2f sec)\n",
         path, samples, (float)samples / SAMPLE_RATE);

  return OK;
}

/* 播放 TTS 音频 */
static int play_tts_audio(FAR struct voice_wakeup_service_s *service,
                         FAR const char *audio_path)
{
  struct nxplayer_s *player;
  int ret;

  player = nxplayer_create();
  if (player == NULL)
    {
      return -ENOMEM;
    }

  ret = nxplayer_setdevice(player, service->config.playback_device);
  if (ret < 0)
    {
      nxplayer_release(player);
      return ret;
    }

  ret = nxplayer_playinternal(player, audio_path, AUDIO_FMT_PCM,
                             CHANNELS, BITS_PER_SAMPLE, SAMPLE_RATE, 3);

  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to play TTS audio: %d\n", ret);
    }

  nxplayer_release(player);
  return ret;
}

/* 处理完整的语音交互流程 */
static int process_voice_interaction(FAR struct voice_wakeup_service_s *service,
                                    FAR const int16_t *audio_buffer,
                                    size_t samples)
{
  char audio_path[128];
  struct mimo_asr_result_s asr_result;
  struct mimo_llm_response_s llm_response;
  struct mimo_tts_result_s tts_result;
  int ret;

  /* 保存音频 */
  snprintf(audio_path, sizeof(audio_path), "%s/wakeup_%lu.pcm",
           service->config.temp_audio_dir,
           (unsigned long)service->wakeup_count);

  ret = save_audio_file(audio_path, audio_buffer, samples);
  if (ret < 0)
    {
      return ret;
    }

  /* 步骤 1: MIMO ASR - 语音转文字 */
  syslog(LOG_INFO, "Calling MIMO ASR...\n");
  pthread_mutex_lock(&service->lock);
  service->state = VOICE_WAKEUP_RECOGNIZING;
  pthread_mutex_unlock(&service->lock);

  ret = mimo_asr(&service->mimo, audio_path, &asr_result);
  if (ret < 0 || !asr_result.success)
    {
      syslog(LOG_WARNING, "MIMO ASR failed\n");
      return -ENODATA;
    }

  syslog(LOG_INFO, "ASR result: \"%s\"\n", asr_result.text);

  /* 步骤 2: 检查是否包含唤醒词 */
  if (contains_wakeword(asr_result.text))
    {
      syslog(LOG_INFO, "Wakeword detected in ASR result!\n");
      pthread_mutex_lock(&service->lock);
      service->state = VOICE_WAKEUP_WAKEWORD_DETECTED;
      service->wakeup_count++;
      pthread_mutex_unlock(&service->lock);

      /* 如果只是唤醒词，不需要 LLM 处理 */
      if (strlen(asr_result.text) < 15)
        {
          syslog(LOG_INFO, "Only wakeword, waiting for next command\n");
          return OK;
        }
    }
  else
    {
      /* 不是唤醒词，忽略 */
      syslog(LOG_INFO, "Not a wakeword, ignoring\n");
      return -EINVAL;
    }

  /* 步骤 3: MIMO LLM - 对话理解 */
  syslog(LOG_INFO, "Calling MIMO LLM...\n");
  pthread_mutex_lock(&service->lock);
  service->state = VOICE_WAKEUP_PROCESSING_COMMAND;
  pthread_mutex_unlock(&service->lock);

  ret = mimo_llm_chat(&service->mimo, asr_result.text, &llm_response);
  if (ret < 0 || !llm_response.success)
    {
      syslog(LOG_WARNING, "MIMO LLM failed\n");
      return -ENODATA;
    }

  syslog(LOG_INFO, "LLM response: \"%s\"\n", llm_response.text);

  pthread_mutex_lock(&service->lock);
  service->command_count++;
  pthread_mutex_unlock(&service->lock);

  /* 步骤 4: MIMO TTS - 文字转语音 */
  syslog(LOG_INFO, "Calling MIMO TTS...\n");
  pthread_mutex_lock(&service->lock);
  service->state = VOICE_WAKEUP_SPEAKING;
  pthread_mutex_unlock(&service->lock);

  ret = mimo_tts(&service->mimo, llm_response.text, &tts_result);
  if (ret < 0 || !tts_result.success)
    {
      syslog(LOG_WARNING, "MIMO TTS failed\n");
      return -ENODATA;
    }

  /* 步骤 5: 播放 TTS 语音 */
  ret = play_tts_audio(service, tts_result.audio_path);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "Failed to play TTS audio\n");
    }

  /* 回调通知 */
  if (service->config.callback != NULL)
    {
      service->config.callback(service->config.callback_arg,
                              asr_result.text,
                              llm_response.text);
    }

  return OK;
}

/* 语音唤醒服务线程 */
static FAR void *voice_wakeup_thread(FAR void *arg)
{
  FAR struct voice_wakeup_service_s *service = arg;
  int16_t frame_buffer[FRAME_SAMPLES];
  int16_t *audio_buffer = NULL;
  size_t audio_samples = 0;
  size_t max_samples;
  uint32_t post_speech_frames = 0;
  bool recording = false;
  ssize_t nbytes;

  syslog(LOG_INFO, "Voice wakeup service thread started\n");

  max_samples = (service->config.max_recording_ms * SAMPLE_RATE) / 1000;
  audio_buffer = malloc(max_samples * sizeof(int16_t));
  if (audio_buffer == NULL)
    {
      syslog(LOG_ERR, "Failed to allocate audio buffer\n");
      return NULL;
    }

  /* 打开音频设备 */
  service->audio_fd = open(service->config.capture_device, O_RDONLY);
  if (service->audio_fd < 0)
    {
      syslog(LOG_ERR, "Failed to open %s: %d\n",
             service->config.capture_device, errno);
      free(audio_buffer);
      return NULL;
    }

  pthread_mutex_lock(&service->lock);
  service->running = true;
  service->state = VOICE_WAKEUP_LISTENING;
  pthread_mutex_unlock(&service->lock);

  /* 持续监听循环 */
  while (!service->shutdown_requested)
    {
      /* 读取一帧音频 */
      nbytes = read(service->audio_fd, frame_buffer, FRAME_BYTES);
      if (nbytes != FRAME_BYTES)
        {
          if (nbytes < 0)
            {
              syslog(LOG_ERR, "Audio read error: %zd\n", nbytes);
            }
          usleep(10000);
          continue;
        }

      /* VAD 检测 */
      bool is_speech = vad_detector_process_frame(&service->vad,
                                                  frame_buffer,
                                                  FRAME_SAMPLES);

      if (is_speech)
        {
          /* 开始录音或继续录音 */
          if (!recording)
            {
              syslog(LOG_INFO, "Voice activity detected, start recording\n");
              audio_samples = 0;
              recording = true;
              post_speech_frames = 0;

              pthread_mutex_lock(&service->lock);
              service->state = VOICE_WAKEUP_VOICE_DETECTED;
              pthread_mutex_unlock(&service->lock);
            }

          /* 保存音频帧 */
          if (audio_samples + FRAME_SAMPLES <= max_samples)
            {
              memcpy(audio_buffer + audio_samples, frame_buffer, FRAME_BYTES);
              audio_samples += FRAME_SAMPLES;
            }
          else
            {
              syslog(LOG_WARNING, "Recording buffer full\n");
              recording = false;
            }
        }
      else if (recording)
        {
          /* 语音结束，继续录音一段时间 */
          post_speech_frames++;

          uint32_t post_frames = (service->config.post_speech_ms * SAMPLE_RATE) /
                                (1000 * FRAME_SAMPLES);

          if (post_speech_frames >= post_frames)
            {
              syslog(LOG_INFO, "Voice ended, processing %zu samples\n",
                     audio_samples);

              /* 处理完整的语音交互 */
              int ret = process_voice_interaction(service, audio_buffer,
                                                 audio_samples);

              if (ret < 0)
                {
                  syslog(LOG_INFO, "Voice interaction failed or ignored: %d\n",
                         ret);
                }

              /* 重置状态 */
              recording = false;
              audio_samples = 0;
              post_speech_frames = 0;
              vad_detector_reset(&service->vad);

              pthread_mutex_lock(&service->lock);
              service->state = VOICE_WAKEUP_LISTENING;
              pthread_mutex_unlock(&service->lock);
            }
          else if (audio_samples + FRAME_SAMPLES <= max_samples)
            {
              /* 继续录音 */
              memcpy(audio_buffer + audio_samples, frame_buffer, FRAME_BYTES);
              audio_samples += FRAME_SAMPLES;
            }
        }
    }

  /* 清理 */
  close(service->audio_fd);
  service->audio_fd = -1;
  free(audio_buffer);

  pthread_mutex_lock(&service->lock);
  service->running = false;
  pthread_mutex_unlock(&service->lock);

  syslog(LOG_INFO, "Voice wakeup service thread stopped\n");
  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int voice_wakeup_service_init(FAR struct voice_wakeup_service_s *service,
                              FAR const struct voice_wakeup_config_s *config)
{
  struct stat st;
  int ret;

  if (service == NULL || config == NULL)
    {
      return -EINVAL;
    }

  memset(service, 0, sizeof(struct voice_wakeup_service_s));
  memcpy(&service->config, config, sizeof(struct voice_wakeup_config_s));

  /* 设置默认值 */
  if (service->config.max_recording_ms == 0)
    {
      service->config.max_recording_ms = 7000;  /* 7 秒 */
    }

  if (service->config.post_speech_ms == 0)
    {
      service->config.post_speech_ms = 500;  /* 0.5 秒 */
    }

  if (service->config.priority == 0)
    {
      service->config.priority = 100;
    }

  if (service->config.stack_size == 0)
    {
      service->config.stack_size = 16384;
    }

  /* 确保临时目录存在 */
  if (stat(service->config.temp_audio_dir, &st) != 0)
    {
      if (mkdir(service->config.temp_audio_dir, 0755) != 0)
        {
          syslog(LOG_ERR, "Failed to create temp dir: %s\n",
                 service->config.temp_audio_dir);
          return -errno;
        }
    }

  /* 初始化 VAD */
  ret = vad_detector_init(&service->vad, &service->config.vad_config);
  if (ret < 0)
    {
      return ret;
    }

  /* 初始化 MIMO */
  ret = mimo_service_init(&service->mimo, &service->config.mimo_config);
  if (ret < 0)
    {
      return ret;
    }

  /* 初始化互斥锁 */
  ret = pthread_mutex_init(&service->lock, NULL);
  if (ret != 0)
    {
      return -ret;
    }

  service->audio_fd = -1;
  service->initialized = true;
  service->state = VOICE_WAKEUP_IDLE;

  syslog(LOG_INFO, "Voice wakeup service initialized\n");
  return OK;
}

int voice_wakeup_service_start(FAR struct voice_wakeup_service_s *service)
{
  pthread_attr_t attr;
  int ret;

  if (!service->initialized || service->thread_created)
    {
      return -EINVAL;
    }

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, service->config.stack_size);

  struct sched_param param;
  param.sched_priority = service->config.priority;
  pthread_attr_setschedparam(&attr, &param);

  ret = pthread_create(&service->thread, &attr, voice_wakeup_thread, service);
  pthread_attr_destroy(&attr);

  if (ret != 0)
    {
      syslog(LOG_ERR, "Failed to create wakeup thread: %d\n", ret);
      return -ret;
    }

  service->thread_created = true;
  syslog(LOG_INFO, "Voice wakeup service started\n");

  return OK;
}

int voice_wakeup_service_stop(FAR struct voice_wakeup_service_s *service)
{
  if (!service->thread_created)
    {
      return OK;
    }

  service->shutdown_requested = true;

  if (service->audio_fd >= 0)
    {
      close(service->audio_fd);
      service->audio_fd = -1;
    }

  pthread_join(service->thread, NULL);
  service->thread_created = false;

  syslog(LOG_INFO, "Voice wakeup service stopped\n");
  return OK;
}

enum voice_wakeup_state_e
voice_wakeup_service_get_state(FAR struct voice_wakeup_service_s *service)
{
  enum voice_wakeup_state_e state;

  pthread_mutex_lock(&service->lock);
  state = service->state;
  pthread_mutex_unlock(&service->lock);

  return state;
}

int voice_wakeup_service_update_mimo(
  FAR struct voice_wakeup_service_s *service,
  FAR const struct mimo_config_s *config)
{
  if (service == NULL || config == NULL)
    {
      return -EINVAL;
    }

  return mimo_service_update_config(&service->mimo, config);
}

void voice_wakeup_service_shutdown(FAR struct voice_wakeup_service_s *service)
{
  if (service == NULL)
    {
      return;
    }

  voice_wakeup_service_stop(service);
  mimo_service_shutdown(&service->mimo);
  pthread_mutex_destroy(&service->lock);

  service->initialized = false;
  syslog(LOG_INFO, "Voice wakeup service shutdown complete\n");
}
