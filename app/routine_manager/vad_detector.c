/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/vad_detector.c
 *
 * 简单的 VAD 实现 - 基于能量和峰值检测
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <math.h>
#include <string.h>
#include <syslog.h>

#include "vad_detector.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* 计算 RMS 能量 */
static uint32_t calculate_rms(FAR const int16_t *samples, uint16_t count)
{
  uint64_t sum = 0;
  uint16_t i;

  for (i = 0; i < count; i++)
    {
      int32_t sample = samples[i];
      sum += (uint64_t)(sample * sample);
    }

  return (uint32_t)sqrt(sum / count);
}

/* 计算峰值 */
static uint16_t calculate_peak(FAR const int16_t *samples, uint16_t count)
{
  uint16_t peak = 0;
  uint16_t i;

  for (i = 0; i < count; i++)
    {
      uint16_t abs_sample = samples[i] < 0 ? -samples[i] : samples[i];
      if (abs_sample > peak)
        {
          peak = abs_sample;
        }
    }

  return peak;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int vad_detector_init(FAR struct vad_detector_s *detector,
                      FAR const struct vad_config_s *config)
{
  if (detector == NULL || config == NULL)
    {
      return -EINVAL;
    }

  memset(detector, 0, sizeof(struct vad_detector_s));
  memcpy(&detector->config, config, sizeof(struct vad_config_s));

  /* 设置默认值 */
  if (detector->config.sample_rate == 0)
    {
      detector->config.sample_rate = 16000;
    }

  if (detector->config.frame_samples == 0)
    {
      detector->config.frame_samples = 320;  /* 20ms @ 16kHz */
    }

  if (detector->config.minimum_rms == 0)
    {
      detector->config.minimum_rms = 350;
    }

  if (detector->config.minimum_peak == 0)
    {
      detector->config.minimum_peak = 900;
    }

  if (detector->config.speech_frames == 0)
    {
      detector->config.speech_frames = 3;  /* 60ms */
    }

  if (detector->config.silence_frames == 0)
    {
      detector->config.silence_frames = 20;  /* 400ms */
    }

  detector->initialized = true;

  syslog(LOG_INFO, "VAD detector initialized: rms=%u, peak=%u\n",
         detector->config.minimum_rms, detector->config.minimum_peak);

  return OK;
}

bool vad_detector_process_frame(FAR struct vad_detector_s *detector,
                                FAR const int16_t *samples,
                                uint16_t count)
{
  uint32_t rms;
  uint16_t peak;
  bool is_voice;

  if (!detector->initialized || samples == NULL || count == 0)
    {
      return false;
    }

  /* 计算能量指标 */
  rms = calculate_rms(samples, count);
  peak = calculate_peak(samples, count);

  /* 判断是否为语音 */
  is_voice = (rms >= detector->config.minimum_rms &&
              peak >= detector->config.minimum_peak);

  detector->state.total_frames++;

  if (is_voice)
    {
      detector->state.speech_count++;
      detector->state.silence_count = 0;

      /* 连续语音帧达到阈值，进入语音状态 */
      if (detector->state.speech_count >= detector->config.speech_frames)
        {
          if (!detector->state.in_speech)
            {
              syslog(LOG_INFO, "VAD: Speech detected (rms=%lu, peak=%u)\n",
                     (unsigned long)rms, peak);
            }
          detector->state.in_speech = true;
        }
    }
  else
    {
      detector->state.silence_count++;
      detector->state.speech_count = 0;

      /* 连续静音帧达到阈值，退出语音状态 */
      if (detector->state.silence_count >= detector->config.silence_frames)
        {
          if (detector->state.in_speech)
            {
              syslog(LOG_INFO, "VAD: Speech ended\n");
            }
          detector->state.in_speech = false;
        }
    }

  return detector->state.in_speech;
}

void vad_detector_reset(FAR struct vad_detector_s *detector)
{
  if (detector != NULL)
    {
      memset(&detector->state, 0, sizeof(struct vad_state_s));
    }
}

void vad_detector_get_state(FAR struct vad_detector_s *detector,
                            FAR struct vad_state_s *state)
{
  if (detector != NULL && state != NULL)
    {
      memcpy(state, &detector->state, sizeof(struct vad_state_s));
    }
}
