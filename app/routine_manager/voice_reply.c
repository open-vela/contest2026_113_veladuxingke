/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/voice_reply.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "voice_reply.h"

#define VOICE_REPLY_COPY_SIZE       1024
#define VOICE_REPLY_BYTES_PER_MS    64
#define VOICE_REPLY_MAX_PARTS       40

static const char *g_digit_assets[] =
{
  "zero.pcm", "one.pcm", "two.pcm", "three.pcm", "four.pcm",
  "five.pcm", "six.pcm", "seven.pcm", "eight.pcm", "nine.pcm"
};

static int voice_reply_append(FAR const char *asset_dir,
                              FAR const char *asset,
                              int output_fd,
                              FAR uint64_t *bytes,
                              voice_reply_cancel_t cancel,
                              FAR void *cancel_arg)
{
  uint8_t buffer[VOICE_REPLY_COPY_SIZE];
  char path[192];
  ssize_t nread;
  int input_fd;
  int length;

  length = snprintf(path, sizeof(path), "%s/%s", asset_dir, asset);
  if (length < 0 || length >= sizeof(path))
    {
      return -ENAMETOOLONG;
    }

  input_fd = open(path, O_RDONLY | O_CLOEXEC);
  if (input_fd < 0)
    {
      return -errno;
    }

  while ((nread = read(input_fd, buffer, sizeof(buffer))) > 0)
    {
      size_t offset = 0;

      if (cancel != NULL && cancel(cancel_arg))
        {
          close(input_fd);
          return -ECANCELED;
        }

      while (offset < nread)
        {
          ssize_t nwritten = write(output_fd, buffer + offset,
                                   nread - offset);
          if (nwritten < 0)
            {
              int ret = -errno;
              close(input_fd);
              return ret;
            }

          offset += nwritten;
          *bytes += nwritten;
        }
    }

  if (nread < 0)
    {
      int ret = -errno;
      close(input_fd);
      return ret;
    }

  close(input_fd);
  return OK;
}

static int voice_reply_add(FAR const char **parts, FAR size_t *count,
                           FAR const char *asset)
{
  if (*count >= VOICE_REPLY_MAX_PARTS)
    {
      return -E2BIG;
    }

  parts[(*count)++] = asset;
  return OK;
}

static int voice_reply_add_integer(FAR const char **parts,
                                   FAR size_t *count,
                                   uint32_t value)
{
  uint32_t ten_thousands;
  uint32_t thousands;
  uint32_t hundreds;
  uint32_t tens;
  uint32_t ones;
  bool zero_pending = false;
  int ret;

  if (parts == NULL || count == NULL || value > 65535)
    {
      return -ERANGE;
    }

  if (value == 0)
    {
      return voice_reply_add(parts, count, g_digit_assets[0]);
    }

  ten_thousands = value / 10000;
  thousands = value / 1000 % 10;
  hundreds = value / 100 % 10;
  tens = value / 10 % 10;
  ones = value % 10;

  if (ten_thousands > 0)
    {
      ret = voice_reply_add_integer(parts, count, ten_thousands);
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add(parts, count, "ten_thousand.pcm");
      if (ret < 0)
        {
          return ret;
        }

      zero_pending = thousands == 0;
    }

  if (thousands > 0)
    {
      ret = voice_reply_add(parts, count, g_digit_assets[thousands]);
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add(parts, count, "thousand.pcm");
      if (ret < 0)
        {
          return ret;
        }

      zero_pending = hundreds == 0;
    }

  if (hundreds > 0)
    {
      if (zero_pending)
        {
          ret = voice_reply_add(parts, count, g_digit_assets[0]);
          if (ret < 0)
            {
              return ret;
            }
        }

      ret = voice_reply_add(parts, count, g_digit_assets[hundreds]);
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add(parts, count, "hundred.pcm");
      if (ret < 0)
        {
          return ret;
        }

      zero_pending = tens == 0;
    }

  if (tens > 0)
    {
      if (zero_pending)
        {
          ret = voice_reply_add(parts, count, g_digit_assets[0]);
          if (ret < 0)
            {
              return ret;
            }
        }

      if (tens > 1 || value >= 100)
        {
          ret = voice_reply_add(parts, count, g_digit_assets[tens]);
          if (ret < 0)
            {
              return ret;
            }
        }

      ret = voice_reply_add(parts, count, "ten.pcm");
      if (ret < 0)
        {
          return ret;
        }

      zero_pending = false;
    }

  if (ones > 0)
    {
      if (zero_pending)
        {
          ret = voice_reply_add(parts, count, g_digit_assets[0]);
          if (ret < 0)
            {
              return ret;
            }
        }

      ret = voice_reply_add(parts, count, g_digit_assets[ones]);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}

static int voice_reply_add_fixed(FAR const char **parts, FAR size_t *count,
                                 uint32_t value, uint32_t denominator,
                                 uint8_t digits)
{
  uint32_t fraction;
  int ret;

  ret = voice_reply_add_integer(parts, count, value / denominator);
  if (ret < 0)
    {
      return ret;
    }

  fraction = value % denominator;
  ret = voice_reply_add(parts, count, "point.pcm");
  if (ret < 0)
    {
      return ret;
    }

  if (digits == 2)
    {
      ret = voice_reply_add(parts, count, g_digit_assets[fraction / 10]);
      if (ret < 0)
        {
          return ret;
        }
    }

  return voice_reply_add(parts, count, g_digit_assets[fraction % 10]);
}

int voice_reply_format_temperature(FAR char *buffer, size_t size,
                                   int32_t temperature_x100)
{
  const char *sign = temperature_x100 < 0 ? "-" : "";
  int32_t magnitude = temperature_x100 < 0 ?
                      -temperature_x100 : temperature_x100;
  int length;

  if (buffer == NULL || size == 0 ||
      temperature_x100 < -4500 || temperature_x100 > 13000)
    {
      return -EINVAL;
    }

  length = snprintf(buffer, size, "当前温度 %s%ld.%02ld°C", sign,
                    (long)(magnitude / 100), (long)(magnitude % 100));
  return length < 0 || length >= size ? -ENOSPC : OK;
}

int voice_reply_format(FAR char *buffer, size_t size,
                       enum voice_reply_type_e type,
                       FAR const struct voice_reply_values_s *values)
{
  int length;

  if (buffer == NULL || size == 0 || values == NULL)
    {
      return -EINVAL;
    }

  switch (type)
    {
      case VOICE_REPLY_TEMPERATURE:
        return voice_reply_format_temperature(buffer, size,
                                              values->temperature_x100);
      case VOICE_REPLY_HUMIDITY:
        length = snprintf(buffer, size, "湿度 %.2f%%",
                          values->humidity_x100 / 100.0);
        break;
      case VOICE_REPLY_SHT40:
        length = snprintf(buffer, size, "温度 %.2f°C，湿度 %.2f%%",
                          values->temperature_x100 / 100.0,
                          values->humidity_x100 / 100.0);
        break;
      case VOICE_REPLY_BH1750:
        length = snprintf(buffer, size, "光照 %lu.%lu lux",
                          (unsigned long)(values->light_x10 / 10),
                          (unsigned long)(values->light_x10 % 10));
        break;
      case VOICE_REPLY_SGP30:
        length = snprintf(buffer, size, "eCO2 %u ppm，TVOC %u ppb",
                          values->eco2_ppm, values->tvoc_ppb);
        break;
      case VOICE_REPLY_TEMPERATURE_LOW_WARNING:
        length = snprintf(buffer, size, "温度过低注意保暖");
        break;
      case VOICE_REPLY_TEMPERATURE_HIGH_WARNING:
        length = snprintf(buffer, size, "温度过高注意高温");
        break;
      case VOICE_REPLY_SCHEDULE_REMINDER:
        length = snprintf(buffer, size,
                          "当前时间为 %02u:%02u:%02u，你今日目标时间已到",
                          values->schedule_hour, values->schedule_minute,
                          values->schedule_second);
        break;
      default:
        return -EINVAL;
    }

  return length < 0 || length >= size ? -ENOSPC : OK;
}

static int voice_reply_collect(enum voice_reply_type_e type,
                               FAR const struct voice_reply_values_s *values,
                               FAR const char **parts, FAR size_t *count)
{
  uint32_t magnitude;
  int ret;

  ret = voice_reply_add(parts, count, "cue.pcm");
  if (ret < 0)
    {
      return ret;
    }

  ret = voice_reply_add(parts, count, "silence_80ms.pcm");
  if (ret < 0)
    {
      return ret;
    }

  if (type == VOICE_REPLY_TEMPERATURE_LOW_WARNING)
    {
      return voice_reply_add(parts, count, "temperature_low_warning.pcm");
    }

  if (type == VOICE_REPLY_TEMPERATURE_HIGH_WARNING)
    {
      return voice_reply_add(parts, count, "temperature_high_warning.pcm");
    }

  if (type == VOICE_REPLY_TEMPERATURE || type == VOICE_REPLY_SHT40)
    {
      if (values->temperature_x100 < -4500 ||
          values->temperature_x100 > 13000)
        {
          return -ERANGE;
        }

      ret = voice_reply_add(parts, count, "current_temperature_is.pcm");
      if (ret < 0)
        {
          return ret;
        }

      magnitude = values->temperature_x100 < 0 ?
                  -values->temperature_x100 : values->temperature_x100;
      if (values->temperature_x100 < 0)
        {
          ret = voice_reply_add(parts, count, "below_zero.pcm");
          if (ret < 0)
            {
              return ret;
            }
        }

      ret = voice_reply_add_fixed(parts, count, magnitude, 100, 2);
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add(parts, count, "degree_celsius.pcm");
      if (ret < 0 || type == VOICE_REPLY_TEMPERATURE)
        {
          return ret;
        }

      ret = voice_reply_add(parts, count, "silence_80ms.pcm");
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add(parts, count, "current_humidity_is.pcm");
      if (ret < 0 || values->humidity_x100 > 10000)
        {
          return ret < 0 ? ret : -ERANGE;
        }

      ret = voice_reply_add_fixed(parts, count, values->humidity_x100, 100, 2);
      if (ret < 0)
        {
          return ret;
        }

      return voice_reply_add(parts, count, "percentage.pcm");
    }

  if (type == VOICE_REPLY_HUMIDITY)
    {
      ret = voice_reply_add(parts, count, "current_humidity_is.pcm");
      if (ret < 0 || values->humidity_x100 > 10000)
        {
          return ret < 0 ? ret : -ERANGE;
        }

      ret = voice_reply_add_fixed(parts, count, values->humidity_x100, 100, 2);
      if (ret < 0)
        {
          return ret;
        }

      return voice_reply_add(parts, count, "percentage.pcm");
    }

  if (type == VOICE_REPLY_BH1750)
    {
      ret = voice_reply_add(parts, count, "current_light_is.pcm");
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add_fixed(parts, count, values->light_x10, 10, 1);
      if (ret < 0)
        {
          return ret;
        }

      return voice_reply_add(parts, count, "lux.pcm");
    }

  if (type == VOICE_REPLY_SCHEDULE_REMINDER)
    {
      if (values->schedule_hour > 23 || values->schedule_minute > 59 ||
          values->schedule_second > 59)
        {
          return -ERANGE;
        }

      ret = voice_reply_add(parts, count, "current_time_is.pcm");
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add_integer(parts, count, values->schedule_hour);
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add(parts, count, "hour_unit.pcm");
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add_integer(parts, count, values->schedule_minute);
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add(parts, count, "minute_unit.pcm");
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add_integer(parts, count, values->schedule_second);
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add(parts, count, "second_unit.pcm");
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add(parts, count, "silence_80ms.pcm");
      if (ret < 0)
        {
          return ret;
        }

      return voice_reply_add(parts, count, "today_goal_due.pcm");
    }

  if (type == VOICE_REPLY_SGP30)
    {
      ret = voice_reply_add(parts, count, "eco2_intro.pcm");
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add_integer(parts, count, values->eco2_ppm);
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add(parts, count, "ppm.pcm");
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add(parts, count, "silence_80ms.pcm");
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add(parts, count, "tvoc_intro.pcm");
      if (ret < 0)
        {
          return ret;
        }

      ret = voice_reply_add_integer(parts, count, values->tvoc_ppb);
      if (ret < 0)
        {
          return ret;
        }

      return voice_reply_add(parts, count, "ppb.pcm");
    }

  return -EINVAL;
}

int voice_reply_build_cancelable(
  FAR const char *asset_dir, FAR const char *output_path,
  enum voice_reply_type_e type,
  FAR const struct voice_reply_values_s *values,
  FAR uint32_t *duration_ms, voice_reply_cancel_t cancel,
  FAR void *cancel_arg)
{
  const char *parts[VOICE_REPLY_MAX_PARTS];
  uint64_t bytes = 0;
  size_t count = 0;
  size_t i;
  int output_fd;
  int ret;

  if (asset_dir == NULL || output_path == NULL || values == NULL)
    {
      return -EINVAL;
    }

  if (cancel != NULL && cancel(cancel_arg))
    {
      return -ECANCELED;
    }

  ret = voice_reply_collect(type, values, parts, &count);
  if (ret < 0)
    {
      return ret;
    }

  output_fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                   0644);
  if (output_fd < 0)
    {
      return -errno;
    }

  for (i = 0; i < count; i++)
    {
      if (cancel != NULL && cancel(cancel_arg))
        {
          ret = -ECANCELED;
        }
      else
        {
          ret = voice_reply_append(asset_dir, parts[i], output_fd, &bytes,
                                   cancel, cancel_arg);
        }

      if (ret < 0)
        {
          close(output_fd);
          unlink(output_path);
          return ret;
        }
    }

  if (close(output_fd) < 0)
    {
      ret = -errno;
      unlink(output_path);
      return ret;
    }

  if (duration_ms != NULL)
    {
      *duration_ms = (bytes + VOICE_REPLY_BYTES_PER_MS - 1) /
                     VOICE_REPLY_BYTES_PER_MS;
    }

  return OK;
}

int voice_reply_build(FAR const char *asset_dir,
                      FAR const char *output_path,
                      enum voice_reply_type_e type,
                      FAR const struct voice_reply_values_s *values,
                      FAR uint32_t *duration_ms)
{
  return voice_reply_build_cancelable(asset_dir, output_path, type, values,
                                      duration_ms, NULL, NULL);
}

int voice_reply_build_temperature(FAR const char *asset_dir,
                                  FAR const char *output_path,
                                  int32_t temperature_x100,
                                  FAR uint32_t *duration_ms)
{
  struct voice_reply_values_s values = {0};

  values.temperature_x100 = temperature_x100;
  return voice_reply_build(asset_dir, output_path, VOICE_REPLY_TEMPERATURE,
                           &values, duration_ms);
}

int voice_reply_selftest(void)
{
  struct voice_reply_values_s values = {0};
  char text[96];

  if (voice_reply_format_temperature(text, sizeof(text), 2345) < 0 ||
      strcmp(text, "当前温度 23.45°C") != 0 ||
      voice_reply_format_temperature(text, sizeof(text), -508) < 0 ||
      strcmp(text, "当前温度 -5.08°C") != 0 ||
      voice_reply_format_temperature(text, sizeof(text), 13001) >= 0)
    {
      return -EINVAL;
    }

  values.temperature_x100 = 2345;
  values.humidity_x100 = 5754;
  values.light_x10 = 177;
  values.eco2_ppm = 400;
  values.tvoc_ppb = 39;
  if (voice_reply_format(text, sizeof(text), VOICE_REPLY_HUMIDITY, &values) < 0 ||
      strcmp(text, "湿度 57.54%") != 0 ||
      voice_reply_format(text, sizeof(text), VOICE_REPLY_SHT40, &values) < 0 ||
      strcmp(text, "温度 23.45°C，湿度 57.54%") != 0 ||
      voice_reply_format(text, sizeof(text), VOICE_REPLY_BH1750, &values) < 0 ||
      strcmp(text, "光照 17.7 lux") != 0 ||
      voice_reply_format(text, sizeof(text), VOICE_REPLY_SGP30, &values) < 0 ||
      strcmp(text, "eCO2 400 ppm，TVOC 39 ppb") != 0)
    {
      return -EINVAL;
    }

  return OK;
}
