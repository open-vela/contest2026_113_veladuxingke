/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/voice_recognizer_demo.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "voice_recognizer.h"

#define VOICE_DEMO_FRAME_SAMPLES        320
#define VOICE_DEMO_MIN_ACTIVE_FRAMES    4
#define VOICE_DEMO_MIN_RMS              350
#define VOICE_DEMO_MIN_PEAK             900

struct voice_demo_metrics_s
{
  uint32_t active_frames;
  uint32_t frames;
  uint32_t peak;
};

static void voice_demo_measure(FAR const int16_t *samples, size_t count,
                               FAR struct voice_demo_metrics_s *metrics)
{
  size_t offset;

  for (offset = 0; offset < count; offset += VOICE_DEMO_FRAME_SAMPLES)
    {
      size_t frame_count = count - offset;
      uint64_t energy = 0;
      uint32_t frame_peak = 0;
      size_t i;

      if (frame_count > VOICE_DEMO_FRAME_SAMPLES)
        {
          frame_count = VOICE_DEMO_FRAME_SAMPLES;
        }

      for (i = 0; i < frame_count; i++)
        {
          int32_t sample = samples[offset + i];
          uint32_t magnitude = sample < 0 ? -sample : sample;

          if (magnitude > frame_peak)
            {
              frame_peak = magnitude;
            }

          energy += (uint64_t)(sample * sample);
        }

      if (frame_peak > metrics->peak)
        {
          metrics->peak = frame_peak;
        }

      if (frame_peak >= VOICE_DEMO_MIN_PEAK &&
          energy / frame_count >= VOICE_DEMO_MIN_RMS * VOICE_DEMO_MIN_RMS)
        {
          metrics->active_frames++;
        }

      metrics->frames++;
    }
}

static int voice_demo_result(FAR const struct voice_demo_metrics_s *metrics,
                             FAR uint16_t *score)
{
  if (metrics->frames == 0 ||
      metrics->active_frames < VOICE_DEMO_MIN_ACTIVE_FRAMES ||
      metrics->peak < VOICE_DEMO_MIN_PEAK)
    {
      *score = 0;
      return -ENODATA;
    }

  *score = metrics->active_frames * 1000 / metrics->frames;
  return OK;
}

static int voice_demo_analyze(FAR const int16_t *samples, size_t count,
                              FAR uint16_t *score)
{
  struct voice_demo_metrics_s metrics = {0};

  if (samples == NULL || score == NULL || count == 0)
    {
      return -EINVAL;
    }

  voice_demo_measure(samples, count, &metrics);
  return voice_demo_result(&metrics, score);
}

int voice_recognizer_demo_recognize(
  FAR const char *pcm_path,
  FAR struct voice_recognition_result_s *result)
{
  int16_t samples[VOICE_DEMO_FRAME_SAMPLES * 4];
  struct voice_demo_metrics_s metrics = {0};
  ssize_t nread;
  int fd;

  if (pcm_path == NULL || result == NULL)
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(*result));
  fd = open(pcm_path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    {
      return -errno;
    }

  while ((nread = read(fd, samples, sizeof(samples))) > 0)
    {
      voice_demo_measure(samples, nread / sizeof(samples[0]), &metrics);
    }

  if (nread < 0)
    {
      int ret = -errno;
      close(fd);
      return ret;
    }

  close(fd);
  if (voice_demo_result(&metrics, &result->score) < 0)
    {
      result->command = VOICE_COMMAND_UNKNOWN;
      return -ENODATA;
    }

  result->command = VOICE_COMMAND_QUERY_TEMPERATURE;
  result->semantic_verified = false;
  return OK;
}

int voice_recognizer_demo_selftest(void)
{
  int16_t silence[VOICE_DEMO_FRAME_SAMPLES * 5];
  int16_t speech[VOICE_DEMO_FRAME_SAMPLES * 5];
  uint16_t score;
  size_t i;

  memset(silence, 0, sizeof(silence));
  for (i = 0; i < sizeof(speech) / sizeof(speech[0]); i++)
    {
      speech[i] = (i & 1) == 0 ? 1200 : -1200;
    }

  if (voice_demo_analyze(silence,
                         sizeof(silence) / sizeof(silence[0]), &score) >= 0 ||
      voice_demo_analyze(speech,
                         sizeof(speech) / sizeof(speech[0]), &score) < 0 ||
      score != 1000)
    {
      return -EINVAL;
    }

  return OK;
}

#ifndef CONFIG_ROUTINE_MANAGER_KWS
int voice_recognizer_recognize(
  FAR const char *pcm_path,
  FAR struct voice_recognition_result_s *result)
{
  return voice_recognizer_demo_recognize(pcm_path, result);
}

int voice_recognizer_selftest(void)
{
  return voice_recognizer_demo_selftest();
}
#endif
