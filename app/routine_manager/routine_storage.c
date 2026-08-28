/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/routine_storage.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>

#include "routine_storage.h"

#define ROUTINE_STORAGE_MAX_FILE 4096


static int routine_storage_mkdir(const char *path)
{
  struct stat info;

  if (mkdir(path, 0700) == 0)
    {
      return 0;
    }

  if (errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode))
    {
      return 0;
    }

  return -errno;
}

static bool routine_storage_export_mount_ready(void)
{
  struct statfs filesystem;

  return statfs(ROUTINE_STORAGE_EXPORT_MOUNT, &filesystem) == 0 &&
         (filesystem.f_type == FATFS_SUPER_MAGIC ||
          filesystem.f_type == MSDOS_SUPER_MAGIC);
}

static int routine_storage_prepare_export_mount(void)
{
  struct stat info;
  int error;
  int ret;

  if (routine_storage_export_mount_ready())
    {
      return 0;
    }

  if (stat(ROUTINE_STORAGE_EXPORT_MOUNT, &info) < 0)
    {
      if (errno != ENOENT)
        {
          return -errno;
        }

      ret = routine_storage_mkdir(ROUTINE_STORAGE_EXPORT_MOUNT);
      if (ret < 0)
        {
          return ret;
        }
    }
  else if (!S_ISDIR(info.st_mode))
    {
      return -ENOTDIR;
    }

  if (mount(ROUTINE_STORAGE_EXPORT_DEVICE, ROUTINE_STORAGE_EXPORT_MOUNT,
            ROUTINE_STORAGE_EXPORT_FSTYPE, 0, NULL) < 0)
    {
      error = errno;
      if (error == EBUSY && routine_storage_export_mount_ready())
        {
          return 0;
        }

      return error == ENOENT || error == ENXIO ? -ENODEV : -error;
    }

  return routine_storage_export_mount_ready() ? 0 : -ENODEV;
}

static int routine_storage_write_all(int fd, const char *data, size_t length)
{
  while (length > 0)
    {
      ssize_t written = write(fd, data, length);

      if (written <= 0)
        {
          return written == 0 ? -EIO : -errno;
        }

      data += written;
      length -= written;
    }

  return 0;
}

static int routine_storage_atomic_write(const char *path, const char *data,
                                        size_t length, mode_t mode)
{
  char temporary[PATH_MAX];
  int fd;
  int ret;

  if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >=
      (int)sizeof(temporary))
    {
      return -ENAMETOOLONG;
    }

  fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, mode);
  if (fd < 0 && errno == EEXIST)
    {
      unlink(temporary);
      fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, mode);
    }

  if (fd < 0)
    {
      return -errno;
    }

  ret = routine_storage_write_all(fd, data, length);
  if (ret == 0 && fsync(fd) < 0)
    {
      ret = -errno;
    }

  if (close(fd) < 0 && ret == 0)
    {
      ret = -errno;
    }

  if (ret < 0 || rename(temporary, path) < 0)
    {
      if (ret == 0)
        {
          ret = -errno;
        }

      unlink(temporary);
      return ret;
    }

  return 0;
}

static int routine_storage_load_line(const char *line,
                                     struct routine_schedule_item_s *item)
{
  unsigned long id;
  unsigned long hour;
  unsigned long minute;
  unsigned long text_id;
  unsigned long reminder_count;
  unsigned long completed;
  unsigned long carry_over;
  long due_day;
  /* The bounded line-oriented schema prevents accepting arbitrary text. */
  if (sscanf(line, "item=%lu,day=%ld,hour=%lu,minute=%lu,text=%lu,reminders=%lu,completed=%lu,carry=%lu",
             &id, &due_day, &hour, &minute, &text_id, &reminder_count,
             &completed, &carry_over) != 8 || id >= ROUTINE_SCHEDULE_MAX ||
      hour > 23 || minute > 59 || text_id >= ROUTINE_SCHEDULE_MAX ||
      reminder_count > ROUTINE_REMINDER_LIMIT || completed > 1 || carry_over > 1)
    {
      return -EINVAL;
    }

  memset(item, 0, sizeof(*item));
  item->id = id;
  item->due_day = due_day;
  item->hour = hour;
  item->minute = minute;
  item->text_id = text_id;
  item->reminder_count = reminder_count;
  item->completed = completed != 0;
  item->carry_over = carry_over != 0;
  return 0;
}

int routine_storage_init(struct routine_storage_s *storage)
{
  if (storage == NULL)
    {
      return -EINVAL;
    }

  memset(storage, 0, sizeof(*storage));
  return routine_storage_mkdir(ROUTINE_STORAGE_DIR);
}

int routine_storage_load(struct routine_storage_s *storage,
                         struct routine_model_s *model)
{
  char data[ROUTINE_STORAGE_MAX_FILE + 1];
  struct routine_snapshot_s *snapshot;
  FILE *stream;
  char *line;
  size_t length;
  unsigned long generation;
  unsigned long count;
  int ret;
  unsigned int i;

  if (storage == NULL || model == NULL)
    {
      return -EINVAL;
    }

  stream = fopen(ROUTINE_STORAGE_SCHEDULE_PATH, "r");
  if (stream == NULL)
    {
      return errno == ENOENT ? -ENOENT : -errno;
    }

  length = fread(data, 1, sizeof(data) - 1, stream);
  ret = ferror(stream) ? -errno : 0;
  fclose(stream);
  if (ret < 0 || length == sizeof(data) - 1)
    {
      return ret < 0 ? ret : -E2BIG;
    }

  data[length] = '\0';
  snapshot = &model->snapshot;
  line = strtok(data, "\n");
  {
    unsigned long config_generation = 0;
    int fields = line == NULL ? 0 :
      sscanf(line, "schema=2,generation=%lu,config=%lu,count=%lu",
             &generation, &config_generation, &count);

    if (fields != 3 && line != NULL)
      {
        fields = sscanf(line, "schema=1,generation=%lu,count=%lu",
                        &generation, &count);
      }

    if ((fields != 2 && fields != 3) || count != ROUTINE_SCHEDULE_MAX)
      {
        return -EINVAL;
      }

    snapshot->config_generation = config_generation;
  }

  for (i = 0; i < ROUTINE_SCHEDULE_MAX; i++)
    {
      line = strtok(NULL, "\n");
      if (line == NULL || routine_storage_load_line(line, &snapshot->schedule[i]) < 0)
        {
          return -EINVAL;
        }
    }

  storage->generation = generation;
  snapshot->schedule_count = ROUTINE_SCHEDULE_MAX;
  routine_model_clear_due(model);
  return 0;
}

int routine_storage_save(struct routine_storage_s *storage,
                         const struct routine_model_s *model)
{
  char data[ROUTINE_STORAGE_MAX_FILE];
  size_t used;
  const struct routine_snapshot_s *snapshot;
  int length;
  uint8_t i;

  if (storage == NULL || model == NULL ||
      model->snapshot.schedule_count != ROUTINE_SCHEDULE_MAX)
    {
      return -EINVAL;
    }

  snapshot = &model->snapshot;
  storage->generation++;
  used = snprintf(data, sizeof(data),
                  "schema=2,generation=%lu,config=%lu,count=%u\n",
                  (unsigned long)storage->generation,
                  (unsigned long)snapshot->config_generation,
                  snapshot->schedule_count);
  if (used >= sizeof(data))
    {
      return -E2BIG;
    }

  for (i = 0; i < snapshot->schedule_count; i++)
    {
      const struct routine_schedule_item_s *item = &snapshot->schedule[i];

      length = snprintf(data + used, sizeof(data) - used,
                        "item=%u,day=%ld,hour=%u,minute=%u,text=%u,reminders=%u,completed=%u,carry=%u\n",
                        item->id, (long)item->due_day, item->hour,
                        item->minute, item->text_id, item->reminder_count,
                        item->completed ? 1 : 0, item->carry_over ? 1 : 0);
      if (length < 0 || (size_t)length >= sizeof(data) - used)
        {
          return -E2BIG;
        }

      used += length;
    }

  return routine_storage_atomic_write(ROUTINE_STORAGE_SCHEDULE_PATH, data,
                                      used, 0600);
}

int routine_storage_publish_status(const struct routine_storage_s *storage,
                                   const struct routine_snapshot_s *snapshot)
{
  char data[2048];
  int length;
  uint8_t i;

  if (storage == NULL || snapshot == NULL)
    {
      return -EINVAL;
    }

  length = snprintf(data, sizeof(data),
                    "{\"schema\":2,\"generation\":%lu,\"config_generation\":%lu,\"clock_valid\":%u,\"day\":%ld,\"hour\":%u,\"minute\":%u,\"second\":%u,\"temperature_x100\":%ld,\"temperature_low_x100\":%ld,\"temperature_high_x100\":%ld,\"history_interval_seconds\":%lu,\"humidity_x100\":%u,\"light_x10\":%lu,\"eco2_ppm\":%u,\"tvoc_ppb\":%u,\"live_mask\":%lu,\"demo_mask\":%lu,\"stale_mask\":%lu,\"warmup_mask\":%lu,\"offline_mask\":%lu,\"schedule\":[",
                    (unsigned long)storage->generation,
                    (unsigned long)snapshot->config_generation,
                    snapshot->clock_valid ? 1 : 0, (long)snapshot->day,
                    snapshot->hour, snapshot->minute, snapshot->second,
                    (long)snapshot->temperature_x100,
                    (long)snapshot->temperature_low_x100,
                    (long)snapshot->temperature_high_x100,
                    (unsigned long)snapshot->history_interval_seconds,
                    snapshot->humidity_x100,
                    (unsigned long)snapshot->light_x10, snapshot->eco2_ppm,
                    snapshot->tvoc_ppb, (unsigned long)snapshot->live_mask,
                    (unsigned long)snapshot->demo_mask,
                    (unsigned long)snapshot->stale_mask,
                    (unsigned long)snapshot->warmup_mask,
                    (unsigned long)snapshot->offline_mask);
  if (length < 0 || (size_t)length >= sizeof(data))
    {
      return -E2BIG;
    }

  for (i = 0; i < snapshot->schedule_count; i++)
    {
      const struct routine_schedule_item_s *item = &snapshot->schedule[i];
      char escaped[ROUTINE_SCHEDULE_TEXT_MAX * 2 + 1];
      size_t source = 0;
      size_t target = 0;

      while (item->text[source] != '\0' && target + 2 < sizeof(escaped))
        {
          if (item->text[source] == '"' || item->text[source] == '\\')
            {
              escaped[target++] = '\\';
            }
          escaped[target++] = item->text[source++];
        }
      escaped[target] = '\0';
      int written = snprintf(data + length, sizeof(data) - length,
                             "%s{\"id\":%u,\"day\":%ld,\"hour\":%u,\"minute\":%u,\"text_id\":%u,\"text\":\"%s\",\"completed\":%u,\"carry_over\":%u}",
                             i == 0 ? "" : ",", item->id,
                             (long)item->due_day, item->hour, item->minute,
                             item->text_id, escaped,
                             item->completed ? 1 : 0,
                             item->carry_over ? 1 : 0);
      if (written < 0 || (size_t)written >= sizeof(data) - length)
        {
          return -E2BIG;
        }

      length += written;
    }

  if ((size_t)length + 2 >= sizeof(data))
    {
      return -E2BIG;
    }

  data[length++] = ']';
  data[length++] = '}';
  data[length] = '\n';
  return routine_storage_atomic_write(ROUTINE_STORAGE_STATUS_PATH, data,
                                      length + 1, 0600);
}

int routine_storage_append_history(struct routine_storage_s *storage,
                                   const struct routine_snapshot_s *snapshot,
                                   uint32_t interval_seconds)
{
  char line[384];
  int64_t timestamp;
  int64_t slot;
  int fd;
  int length;

  if (storage == NULL || snapshot == NULL || !snapshot->clock_valid ||
      !routine_history_interval_valid(interval_seconds))
    {
      return -EINVAL;
    }

  timestamp = (int64_t)snapshot->day * 24 * 60 * 60 +
              (int64_t)snapshot->hour * 60 * 60 +
              (int64_t)snapshot->minute * 60 + snapshot->second;
  slot = timestamp / interval_seconds;
  if (storage->history_valid &&
      storage->last_history_interval_seconds == interval_seconds &&
      storage->last_history_slot == slot)
    {
      return 0;
    }

  fd = open(ROUTINE_STORAGE_HISTORY_PATH, O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (fd < 0)
    {
      return -errno;
    }

  length = snprintf(line, sizeof(line),
                    "{\"day\":%ld,\"hour\":%u,\"minute\":%u,\"second\":%u,\"interval_seconds\":%lu,\"temperature_x100\":%ld,\"humidity_x100\":%u,\"light_x10\":%lu,\"eco2_ppm\":%u,\"tvoc_ppb\":%u,\"live_mask\":%lu,\"stale_mask\":%lu}\n",
                    (long)snapshot->day, snapshot->hour, snapshot->minute,
                    snapshot->second, (unsigned long)interval_seconds,
                    (long)snapshot->temperature_x100, snapshot->humidity_x100,
                    (unsigned long)snapshot->light_x10, snapshot->eco2_ppm,
                    snapshot->tvoc_ppb, (unsigned long)snapshot->live_mask,
                    (unsigned long)snapshot->stale_mask);
  if (length < 0 || (size_t)length >= sizeof(line))
    {
      close(fd);
      return -E2BIG;
    }

  if (routine_storage_write_all(fd, line, length) < 0 || fsync(fd) < 0)
    {
      int ret = -errno;
      close(fd);
      return ret;
    }

  close(fd);
  storage->last_history_slot = slot;
  storage->last_history_interval_seconds = interval_seconds;
  storage->history_valid = true;
  return 0;
}

int routine_storage_export_history(struct routine_storage_s *storage,
                                   char *path, size_t path_size)
{
  char buffer[1024];
  char target[PATH_MAX];
  time_t now;
  int source = -1;
  int output = -1;
  int attempt;
  int ret = 0;

  if (storage == NULL || path == NULL || path_size == 0)
    {
      return -EINVAL;
    }

  ret = routine_storage_prepare_export_mount();
  if (ret < 0)
    {
      return ret;
    }

  source = open(ROUTINE_STORAGE_HISTORY_PATH, O_RDONLY);
  if (source < 0)
    {
      return errno == ENOENT ? -ENODATA : -errno;
    }

  ret = routine_storage_mkdir(ROUTINE_STORAGE_EXPORT_DIR);
  if (ret < 0)
    {
      close(source);
      return ret;
    }

  now = time(NULL);
  for (attempt = 0; attempt < 100; attempt++)
    {
      int length;

      if (attempt == 0)
        {
          length = snprintf(target, sizeof(target),
                            ROUTINE_STORAGE_EXPORT_DIR
                            "/routine-history-%ld.jsonl", (long)now);
        }
      else
        {
          length = snprintf(target, sizeof(target),
                            ROUTINE_STORAGE_EXPORT_DIR
                            "/routine-history-%ld-%d.jsonl",
                            (long)now, attempt);
        }
      if (length < 0 || (size_t)length >= sizeof(target))
        {
          ret = -ENAMETOOLONG;
          goto out;
        }

      output = open(target, O_WRONLY | O_CREAT | O_EXCL, 0644);
      if (output >= 0)
        {
          break;
        }
      if (errno != EEXIST)
        {
          ret = -errno;
          goto out;
        }
    }

  if (output < 0)
    {
      ret = -EEXIST;
      goto out;
    }

  for (;;)
    {
      ssize_t count = read(source, buffer, sizeof(buffer));

      if (count < 0 && errno == EINTR)
        {
          continue;
        }
      if (count < 0)
        {
          ret = -errno;
          break;
        }
      if (count == 0)
        {
          break;
        }
      ret = routine_storage_write_all(output, buffer, (size_t)count);
      if (ret < 0)
        {
          break;
        }
    }

  if (ret == 0 && fsync(output) < 0)
    {
      ret = -errno;
    }

out:
  if (source >= 0)
    {
      close(source);
    }
  if (output >= 0 && close(output) < 0 && ret == 0)
    {
      ret = -errno;
    }
  if (ret < 0 && output >= 0)
    {
      unlink(target);
    }
  if (ret == 0 && snprintf(path, path_size, "%s", target) >=
                  (int)path_size)
    {
      ret = -ENAMETOOLONG;
    }
  return ret;
}

int routine_storage_append_event(struct routine_storage_s *storage,
                                 const char *event, int result)
{
  char line[256];
  int fd;
  int length;

  if (storage == NULL || event == NULL || strlen(event) > 120)
    {
      return -EINVAL;
    }

  fd = open(ROUTINE_STORAGE_EVENTS_PATH, O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (fd < 0)
    {
      return -errno;
    }

  length = snprintf(line, sizeof(line), "{\"event\":\"%s\",\"result\":%d}\n",
                    event, result);
  if (length < 0 || (size_t)length >= sizeof(line))
    {
      close(fd);
      return -E2BIG;
    }

  if (routine_storage_write_all(fd, line, length) < 0 || fsync(fd) < 0)
    {
      int ret = -errno;
      close(fd);
      return ret;
    }

  close(fd);
  return 0;
}

uint64_t routine_storage_generation(const struct routine_storage_s *storage)
{
  return storage == NULL ? 0 : storage->generation;
}
