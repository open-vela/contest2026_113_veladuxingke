/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/routine_config.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <netutils/cJSON.h>

#include "routine_config.h"

#define ROUTINE_CONFIG_MAX_BYTES 2048
#define ROUTINE_CONFIG_DIR "/data/routine"

static bool routine_config_whitespace(char value)
{
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static bool routine_config_integer(FAR const cJSON *item, int32_t minimum,
                                   int32_t maximum, FAR int32_t *value)
{
  if (!cJSON_IsNumber(item) || item->valuedouble != (double)item->valueint ||
      item->valueint < minimum || item->valueint > maximum)
    {
      return false;
    }

  *value = item->valueint;
  return true;
}

static bool routine_config_utf8(FAR const char *text, size_t length)
{
  size_t offset = 0;

  if (text == NULL || length == 0 || length > ROUTINE_SCHEDULE_TEXT_MAX)
    {
      return false;
    }

  while (offset < length)
    {
      uint8_t first = (uint8_t)text[offset++];
      uint32_t codepoint;
      uint8_t continuation;

      if (first < 0x80)
        {
          if (first < 0x20 || first == 0x7f)
            {
              return false;
            }

          continue;
        }

      if (first >= 0xc2 && first <= 0xdf)
        {
          codepoint = first & 0x1f;
          continuation = 1;
        }
      else if (first >= 0xe0 && first <= 0xef)
        {
          codepoint = first & 0x0f;
          continuation = 2;
        }
      else if (first >= 0xf0 && first <= 0xf4)
        {
          codepoint = first & 0x07;
          continuation = 3;
        }
      else
        {
          return false;
        }

      if (offset + continuation > length)
        {
          return false;
        }

      while (continuation-- > 0)
        {
          uint8_t next = (uint8_t)text[offset++];
          if ((next & 0xc0) != 0x80)
            {
              return false;
            }

          codepoint = (codepoint << 6) | (next & 0x3f);
        }

      if ((first <= 0xdf && codepoint < 0x80) ||
          (first <= 0xef && codepoint < 0x800) ||
          (first >= 0xf0 && codepoint < 0x10000) ||
          codepoint > 0x10ffff ||
          (codepoint >= 0xd800 && codepoint <= 0xdfff))
        {
          return false;
        }
    }

  return true;
}

void routine_config_defaults(FAR struct routine_settings_s *settings)
{
  static const uint8_t hours[ROUTINE_SCHEDULE_MAX] = {9, 14, 18};
  static const uint8_t minutes[ROUTINE_SCHEDULE_MAX] = {0, 30, 0};
  static const char *texts[ROUTINE_SCHEDULE_MAX] =
  {
    "喝水", "专注", "通风"
  };
  uint8_t i;

  memset(settings, 0, sizeof(*settings));
  settings->temperature_low_x100 =
    ROUTINE_DEFAULT_TEMPERATURE_LOW_X100;
  settings->temperature_high_x100 =
    ROUTINE_DEFAULT_TEMPERATURE_HIGH_X100;
  settings->history_interval_seconds =
    ROUTINE_DEFAULT_HISTORY_INTERVAL_SECONDS;
  for (i = 0; i < ROUTINE_SCHEDULE_MAX; i++)
    {
      settings->schedule[i].id = i;
      settings->schedule[i].hour = hours[i];
      settings->schedule[i].minute = minutes[i];
      strlcpy(settings->schedule[i].text, texts[i],
              sizeof(settings->schedule[i].text));
    }
}

int routine_config_load(FAR struct routine_settings_s *settings)
{
  struct routine_settings_s parsed;
  bool ids[ROUTINE_SCHEDULE_MAX] = {false};
  char data[ROUTINE_CONFIG_MAX_BYTES + 1];
  FAR const cJSON *schedule;
  FAR const cJSON *history_interval;
  FAR const cJSON *item;
  FAR const char *end = NULL;
  struct stat info;
  cJSON *root = NULL;
  ssize_t total = 0;
  int32_t number;
  int fd;
  int ret = -EINVAL;
  int i;

  if (settings == NULL || lstat(ROUTINE_CONFIG_PATH, &info) < 0)
    {
      return settings == NULL ? -EINVAL : -errno;
    }

  if (!S_ISREG(info.st_mode) || info.st_size <= 0 ||
      info.st_size > ROUTINE_CONFIG_MAX_BYTES)
    {
      return -EINVAL;
    }

  fd = open(ROUTINE_CONFIG_PATH, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    {
      return -errno;
    }

  while (total < info.st_size)
    {
      ssize_t count = read(fd, data + total, info.st_size - total);
      if (count < 0 && errno == EINTR)
        {
          continue;
        }
      if (count <= 0)
        {
          close(fd);
          return count == 0 ? -EIO : -errno;
        }
      total += count;
    }
  close(fd);
  data[total] = '\0';

  root = cJSON_ParseWithLengthOpts(data, total, &end, false);
  if (root == NULL || !cJSON_IsObject(root) || end == NULL)
    {
      goto out;
    }
  while (end < data + total && routine_config_whitespace(*end))
    {
      end++;
    }
  if (end != data + total)
    {
      goto out;
    }

  routine_config_defaults(&parsed);
  if (!routine_config_integer(
        cJSON_GetObjectItemCaseSensitive(root, "schema"), 1, 1, &number))
    {
      goto out;
    }
  if (!routine_config_integer(
        cJSON_GetObjectItemCaseSensitive(root, "generation"), 0,
        INT32_MAX, &number))
    {
      goto out;
    }
  parsed.generation = (uint32_t)number;
  if (!routine_config_integer(
        cJSON_GetObjectItemCaseSensitive(root, "temperature_low_x100"),
        -4000, 12400, &parsed.temperature_low_x100) ||
      !routine_config_integer(
        cJSON_GetObjectItemCaseSensitive(root, "temperature_high_x100"),
        -3900, 12500, &parsed.temperature_high_x100) ||
      parsed.temperature_high_x100 - parsed.temperature_low_x100 < 100)
    {
      goto out;
    }

  history_interval = cJSON_GetObjectItemCaseSensitive(
    root, "history_interval_seconds");
  if (history_interval != NULL &&
      (!routine_config_integer(
         history_interval, ROUTINE_HISTORY_INTERVAL_MIN_SECONDS,
         ROUTINE_HISTORY_INTERVAL_MAX_SECONDS, &number) ||
       !routine_history_interval_valid((uint32_t)number)))
    {
      goto out;
    }
  if (history_interval != NULL)
    {
      parsed.history_interval_seconds = (uint32_t)number;
    }

  schedule = cJSON_GetObjectItemCaseSensitive(root, "schedule");
  if (!cJSON_IsArray(schedule) ||
      cJSON_GetArraySize(schedule) != ROUTINE_SCHEDULE_MAX)
    {
      goto out;
    }

  cJSON_ArrayForEach(item, schedule)
    {
      FAR const cJSON *text;
      int32_t id;
      int32_t hour;
      int32_t minute;
      size_t length;

      if (!cJSON_IsObject(item) ||
          !routine_config_integer(
            cJSON_GetObjectItemCaseSensitive(item, "id"), 0,
            ROUTINE_SCHEDULE_MAX - 1, &id) || ids[id] ||
          !routine_config_integer(
            cJSON_GetObjectItemCaseSensitive(item, "hour"), 0, 23, &hour) ||
          !routine_config_integer(
            cJSON_GetObjectItemCaseSensitive(item, "minute"), 0, 59,
            &minute))
        {
          goto out;
        }

      text = cJSON_GetObjectItemCaseSensitive(item, "text");
      length = cJSON_IsString(text) ? strlen(text->valuestring) : 0;
      if (!routine_config_utf8(cJSON_IsString(text) ? text->valuestring : NULL,
                               length))
        {
          goto out;
        }

      ids[id] = true;
      parsed.schedule[id].id = id;
      parsed.schedule[id].hour = hour;
      parsed.schedule[id].minute = minute;
      memcpy(parsed.schedule[id].text, text->valuestring, length + 1);
    }

  for (i = 0; i < ROUTINE_SCHEDULE_MAX; i++)
    {
      if (!ids[i])
        {
          goto out;
        }
    }

  *settings = parsed;
  ret = OK;

out:
  cJSON_Delete(root);
  memset(data, 0, sizeof(data));
  return ret;
}

static int routine_config_write_all(int fd, FAR const char *data,
                                    size_t length)
{
  while (length > 0)
    {
      ssize_t written = write(fd, data, length);

      if (written < 0 && errno == EINTR)
        {
          continue;
        }
      if (written <= 0)
        {
          return written == 0 ? -EIO : -errno;
        }

      data += written;
      length -= written;
    }

  return 0;
}

int routine_config_save(FAR const struct routine_settings_s *settings)
{
  bool ids[ROUTINE_SCHEDULE_MAX] = {false};
  char temporary[sizeof(ROUTINE_CONFIG_PATH) + 8];
  cJSON *root = NULL;
  cJSON *schedule = NULL;
  char *rendered = NULL;
  size_t rendered_length;
  int fd = -1;
  int ret = -EINVAL;
  uint8_t i;

  if (settings == NULL || settings->temperature_low_x100 < -4000 ||
      settings->temperature_high_x100 > 12500 ||
      settings->temperature_high_x100 - settings->temperature_low_x100 < 100 ||
      !routine_history_interval_valid(settings->history_interval_seconds))
    {
      return -EINVAL;
    }

  for (i = 0; i < ROUTINE_SCHEDULE_MAX; i++)
    {
      FAR const struct routine_schedule_setting_s *item =
        &settings->schedule[i];
      size_t length = strnlen(item->text, ROUTINE_SCHEDULE_TEXT_MAX + 1);

      if (item->id >= ROUTINE_SCHEDULE_MAX || ids[item->id] ||
          item->hour > 23 || item->minute > 59 ||
          !routine_config_utf8(item->text, length))
        {
          return -EINVAL;
        }
      ids[item->id] = true;
    }

  root = cJSON_CreateObject();
  schedule = cJSON_CreateArray();
  if (root == NULL || schedule == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

  cJSON_AddNumberToObject(root, "schema", 1);
  cJSON_AddNumberToObject(root, "generation", settings->generation);
  cJSON_AddNumberToObject(root, "temperature_low_x100",
                          settings->temperature_low_x100);
  cJSON_AddNumberToObject(root, "temperature_high_x100",
                          settings->temperature_high_x100);
  cJSON_AddNumberToObject(root, "history_interval_seconds",
                          settings->history_interval_seconds);
  cJSON_AddItemToObject(root, "schedule", schedule);
  schedule = NULL;

  for (i = 0; i < ROUTINE_SCHEDULE_MAX; i++)
    {
      FAR const struct routine_schedule_setting_s *setting =
        &settings->schedule[i];
      cJSON *item = cJSON_CreateObject();

      if (item == NULL)
        {
          ret = -ENOMEM;
          goto out;
        }
      cJSON_AddNumberToObject(item, "id", setting->id);
      cJSON_AddNumberToObject(item, "hour", setting->hour);
      cJSON_AddNumberToObject(item, "minute", setting->minute);
      cJSON_AddStringToObject(item, "text", setting->text);
      cJSON_AddItemToArray(
        cJSON_GetObjectItemCaseSensitive(root, "schedule"), item);
    }

  rendered = cJSON_PrintUnformatted(root);
  rendered_length = rendered == NULL ? 0 : strlen(rendered);
  if (rendered_length == 0 || rendered_length >= ROUTINE_CONFIG_MAX_BYTES)
    {
      ret = rendered == NULL ? -ENOMEM : -E2BIG;
      goto out;
    }

  if (mkdir(ROUTINE_CONFIG_DIR, 0700) < 0 && errno != EEXIST)
    {
      ret = -errno;
      goto out;
    }

  snprintf(temporary, sizeof(temporary), "%s.tmp", ROUTINE_CONFIG_PATH);
  fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0 && errno == EEXIST)
    {
      unlink(temporary);
      fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
    }
  if (fd < 0)
    {
      ret = -errno;
      goto out;
    }

  ret = routine_config_write_all(fd, rendered, rendered_length);
  if (ret == 0 && fsync(fd) < 0)
    {
      ret = -errno;
    }
  if (close(fd) < 0 && ret == 0)
    {
      ret = -errno;
    }
  fd = -1;
  if (ret == 0 && rename(temporary, ROUTINE_CONFIG_PATH) < 0)
    {
      ret = -errno;
    }
  if (ret < 0)
    {
      unlink(temporary);
    }

out:
  if (fd >= 0)
    {
      close(fd);
    }
  free(rendered);
  cJSON_Delete(schedule);
  cJSON_Delete(root);
  return ret;
}
