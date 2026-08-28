/****************************************************************************
 * contest2026_113_veladuxingke/app/lan_panel/lan_panel_main.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netutils/netlib.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/ioexpander/gpio.h>
#include <netutils/cJSON.h>

#include "lan_auth.h"
#include "lan_http.h"
#include "wifi_config.h"

#ifndef CONFIG_CONTEST2026_113_LAN_PANEL_PORT
#  define CONFIG_CONTEST2026_113_LAN_PANEL_PORT 8080
#endif
#ifndef CONFIG_CONTEST2026_113_LAN_PANEL_TIMEOUT_SEC
#  define CONFIG_CONTEST2026_113_LAN_PANEL_TIMEOUT_SEC 5
#endif
#ifndef CONFIG_CONTEST2026_113_LAN_PANEL_KWS_ROOT
#  define CONFIG_CONTEST2026_113_LAN_PANEL_KWS_ROOT "/sdcard/kws_corpus"
#endif

#define LAN_PANEL_WIFI_CONFIG "/data/wifi.cfg"
#define LAN_PANEL_ROUTINE_DIR "/data/routine"
#define LAN_PANEL_STATUS LAN_PANEL_ROUTINE_DIR "/status.json"
#define LAN_PANEL_HISTORY LAN_PANEL_ROUTINE_DIR "/history.jsonl"
#define LAN_PANEL_EVENTS LAN_PANEL_ROUTINE_DIR "/events.log"
#define LAN_PANEL_ROUTINE_CONFIG LAN_PANEL_ROUTINE_DIR "/config.json"
#define LAN_PANEL_WIFI_STATUS "/data/wifi.status"
#define LAN_PANEL_TOKEN_MAX 5
#define LAN_PANEL_PATH_MAX 256
#define LAN_PANEL_SCHEDULE_MAX 3
#define LAN_PANEL_SCHEDULE_TEXT_MAX 48
#define LAN_PANEL_ROUTINE_CONFIG_MAX 2048
#define LAN_PANEL_HISTORY_INTERVAL_MIN 5
#define LAN_PANEL_HISTORY_INTERVAL_MAX 3600
#define LAN_PANEL_HISTORY_INTERVAL_DEFAULT 60
#define LAN_PANEL_SD_DEVICE "/dev/mmcsd1"
#define LAN_PANEL_SD_MOUNT "/sdcard"
#define LAN_PANEL_SD_FSTYPE "vfat"
#define LAN_PANEL_HISTORY_EXPORT_DIR LAN_PANEL_SD_MOUNT "/routine_history"

static volatile sig_atomic_t g_stop;

static void lan_panel_signal(int signo)
{
  (void)signo;
  g_stop = 1;
}

#define LAN_PANEL_BACKLOG 8
#define LAN_PANEL_MAX_WORKERS 4
#define LAN_PANEL_WORKER_STACKSIZE 24576

static pthread_mutex_t g_worker_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_worker_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_mutation_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int g_worker_count;

/* Used by all file-backed configuration writers, including the Wi-Fi writer
 * which is defined before the helper's full implementation below.
 */
static int lan_panel_write_all(int fd, const char *data, size_t length);

static int lan_panel_set_timeout(int fd)
{
  struct timeval timeout;
  timeout.tv_sec = CONFIG_CONTEST2026_113_LAN_PANEL_TIMEOUT_SEC;
  timeout.tv_usec = 0;
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
      setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0)
    {
      return -errno;
    }
  return 0;
}

static int lan_panel_read_all(const char *path, char *buffer, size_t size,
                              size_t *length)
{
  int fd = open(path, O_RDONLY);
  size_t used = 0;

  if (fd < 0)
    {
      return -errno;
    }

  while (used < size)
    {
      ssize_t chunk = read(fd, buffer + used, size - used);
      if (chunk == 0)
        {
          break;
        }
      if (chunk < 0)
        {
          int ret = errno == EINTR ? 0 : -errno;
          if (ret < 0)
            {
              close(fd);
              return ret;
            }
          continue;
        }
      used += (size_t)chunk;
    }

  close(fd);
  *length = used;
  return 0;
}

static int lan_panel_authorized(const struct lan_http_request_s *request)
{
  const char prefix[] = "Bearer ";

  if (request == NULL || strncmp(request->authorization, prefix,
                                 sizeof(prefix) - 1) != 0)
    {
      return -EACCES;
    }

  return lan_auth_check(request->authorization + sizeof(prefix) - 1,
                        strlen(request->authorization + sizeof(prefix) - 1));
}

static bool lan_panel_json_whitespace(char value)
{
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static cJSON *lan_panel_parse_json_body(const char *body, size_t length)
{
  const char *end = NULL;
  cJSON *root;

  if (body == NULL || length == 0)
    {
      return NULL;
    }

  root = cJSON_ParseWithLengthOpts(body, length, &end, false);
  if (root == NULL || end == NULL)
    {
      return NULL;
    }

  while (end < body + length && lan_panel_json_whitespace(*end))
    {
      end++;
    }

  if (end != body + length)
    {
      cJSON_Delete(root);
      return NULL;
    }

  return root;
}

static bool lan_panel_wifi_value_valid(const char *value, size_t minimum,
                                       size_t maximum)
{
  size_t length;
  size_t i;

  if (value == NULL)
    {
      return false;
    }

  length = strlen(value);
  if (length < minimum || length > maximum)
    {
      return false;
    }

  for (i = 0; i < length; i++)
    {
      unsigned char character = (unsigned char)value[i];
      if (character < 0x20 || character == 0x7f)
        {
          return false;
        }
    }

  return true;
}

static int lan_panel_parse_wifi_json(const char *body, size_t length,
                                     struct wifi_config_s *config)
{
  const cJSON *ssid;
  const cJSON *password;
  cJSON *root;
  int ret = -EINVAL;

  root = lan_panel_parse_json_body(body, length);
  if (root == NULL || !cJSON_IsObject(root))
    {
      cJSON_Delete(root);
      return -EINVAL;
    }

  ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
  password = cJSON_GetObjectItemCaseSensitive(root, "password");
  if (!cJSON_IsString(ssid) || !cJSON_IsString(password) ||
      !lan_panel_wifi_value_valid(ssid->valuestring, 1,
                                  WIFI_CONFIG_SSID_MAX) ||
      !lan_panel_wifi_value_valid(password->valuestring,
                                  WIFI_CONFIG_PASSWORD_MIN,
                                  WIFI_CONFIG_PASSWORD_MAX))
    {
      goto out;
    }

  memset(config, 0, sizeof(*config));
  memcpy(config->ssid, ssid->valuestring, strlen(ssid->valuestring));
  memcpy(config->password, password->valuestring,
         strlen(password->valuestring));
  ret = 0;

out:
  cJSON_Delete(root);
  return ret;
}

static int lan_panel_append_wifi_line(char *output, size_t size,
                                      size_t *used, const char *key,
                                      const char *value)
{
  size_t key_length = strlen(key);

  if (*used + key_length + 4 > size)
    {
      return -E2BIG;
    }

  memcpy(output + *used, key, key_length);
  *used += key_length;
  output[(*used)++] = '=';
  output[(*used)++] = '"';

  while (*value != '\0')
    {
      if (*value == '\\' || *value == '"')
        {
          if (*used + 1 >= size)
            {
              return -E2BIG;
            }

          output[(*used)++] = '\\';
        }

      if (*used + 1 >= size)
        {
          return -E2BIG;
        }

      output[(*used)++] = *value++;
    }

  if (*used + 3 > size)
    {
      return -E2BIG;
    }

  output[(*used)++] = '"';
  output[(*used)++] = '\n';
  output[*used] = '\0';
  return 0;
}

static int lan_panel_write_wifi(const char *body, size_t length)
{
  struct wifi_config_s config;
  char temporary[sizeof(LAN_PANEL_WIFI_CONFIG) + 16];
  char text[WIFI_CONFIG_FILE_MAX];
  size_t used = 0;
  int fd;
  int ret;

  if (body == NULL || length == 0 || length >= sizeof(text))
    {
      return -EINVAL;
    }

  memset(&config, 0, sizeof(config));
  if (lan_panel_parse_wifi_json(body, length, &config) < 0)
    {
      wifi_config_clear(&config);
      return -EINVAL;
    }

  ret = lan_panel_append_wifi_line(text, sizeof(text), &used, "SSID",
                                   config.ssid);
  if (ret == 0)
    {
      ret = lan_panel_append_wifi_line(text, sizeof(text), &used, "PASSWORD",
                                       config.password);
    }
  wifi_config_clear(&config);
  if (ret < 0)
    {
      memset(text, 0, sizeof(text));
      return ret;
    }

  snprintf(temporary, sizeof(temporary), "%s.tmp", LAN_PANEL_WIFI_CONFIG);
  fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0 && errno == EEXIST)
    {
      unlink(temporary);
      fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
    }
  if (fd < 0)
    {
      return -errno;
    }

  ret = lan_panel_write_all(fd, text, used);
  if (ret == 0 && fsync(fd) < 0)
    {
      ret = -errno;
    }
  close(fd);
  memset(text, 0, sizeof(text));
  if (ret < 0 || rename(temporary, LAN_PANEL_WIFI_CONFIG) < 0)
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

static int lan_panel_write_light_config(const char *body, size_t length)
{
  char buffer[128];
  const cJSON *thresh_item;
  const cJSON *levels_item;
  cJSON *root;
  int thresh;
  int levels;
  int fd;
  int written;

  if (body == NULL || length == 0 || length >= sizeof(buffer))
    {
      return -EINVAL;
    }

  root = lan_panel_parse_json_body(body, length);
  if (root == NULL || !cJSON_IsObject(root))
    {
      cJSON_Delete(root);
      return -EINVAL;
    }

  thresh_item = cJSON_GetObjectItemCaseSensitive(root, "thresh");
  levels_item = cJSON_GetObjectItemCaseSensitive(root, "levels");
  if (!cJSON_IsNumber(thresh_item) || !cJSON_IsNumber(levels_item) ||
      thresh_item->valuedouble != (double)thresh_item->valueint ||
      levels_item->valuedouble != (double)levels_item->valueint)
    {
      cJSON_Delete(root);
      return -EINVAL;
    }

  thresh = thresh_item->valueint;
  levels = levels_item->valueint;
  cJSON_Delete(root);
  if (thresh < 100 || thresh > 2400 || levels < 3 || levels > 10)
    {
      return -EINVAL;
    }

  fd = open("/data/light.cfg", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      return -errno;
    }

  written = snprintf(buffer, sizeof(buffer), "THRESHOLD=%d\nLEVELS=%d\n",
                     thresh, levels);
  if (written < 0 || (size_t)written >= sizeof(buffer))
    {
      close(fd);
      return -E2BIG;
    }

  if (write(fd, buffer, written) != written)
    {
      close(fd);
      return -EIO;
    }

  fsync(fd);
  close(fd);
  return 0;
}

struct lan_panel_schedule_s
{
  int id;
  int hour;
  int minute;
  char text[LAN_PANEL_SCHEDULE_TEXT_MAX + 1];
};

struct lan_panel_routine_config_s
{
  int temperature_low_x100;
  int temperature_high_x100;
  int history_interval_seconds;
  struct lan_panel_schedule_s schedule[LAN_PANEL_SCHEDULE_MAX];
};

static bool lan_panel_utf8_valid(const char *text, size_t length)
{
  size_t offset = 0;

  if (text == NULL || length == 0 || length > LAN_PANEL_SCHEDULE_TEXT_MAX)
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

static bool lan_panel_json_integer(const cJSON *item, int minimum,
                                   int maximum, int *value)
{
  if (!cJSON_IsNumber(item) || item->valuedouble != (double)item->valueint ||
      item->valueint < minimum || item->valueint > maximum)
    {
      return false;
    }

  *value = item->valueint;
  return true;
}

static bool lan_panel_history_interval_valid(int interval_seconds)
{
  return interval_seconds == 5 || interval_seconds == 10 ||
         (interval_seconds >= 60 &&
          interval_seconds <= LAN_PANEL_HISTORY_INTERVAL_MAX &&
          interval_seconds % 60 == 0);
}

static int lan_panel_parse_routine_config(
  const char *body, size_t length,
  struct lan_panel_routine_config_s *config)
{
  bool ids[LAN_PANEL_SCHEDULE_MAX] = {false};
  const cJSON *schedule;
  const cJSON *history_interval;
  const cJSON *item;
  cJSON *root;
  int ret = -EINVAL;

  if (config == NULL || length > LAN_PANEL_ROUTINE_CONFIG_MAX)
    {
      return -E2BIG;
    }

  config->history_interval_seconds = LAN_PANEL_HISTORY_INTERVAL_DEFAULT;
  root = lan_panel_parse_json_body(body, length);
  if (root == NULL || !cJSON_IsObject(root) ||
      !lan_panel_json_integer(
        cJSON_GetObjectItemCaseSensitive(root, "temperature_low_x100"),
        -4000, 12400, &config->temperature_low_x100) ||
      !lan_panel_json_integer(
        cJSON_GetObjectItemCaseSensitive(root, "temperature_high_x100"),
        -3900, 12500, &config->temperature_high_x100) ||
      config->temperature_high_x100 - config->temperature_low_x100 < 100)
    {
      goto out;
    }

  history_interval = cJSON_GetObjectItemCaseSensitive(
    root, "history_interval_seconds");
  if (history_interval != NULL &&
      (!lan_panel_json_integer(history_interval,
                               LAN_PANEL_HISTORY_INTERVAL_MIN,
                               LAN_PANEL_HISTORY_INTERVAL_MAX,
                               &config->history_interval_seconds) ||
       !lan_panel_history_interval_valid(
         config->history_interval_seconds)))
    {
      goto out;
    }

  schedule = cJSON_GetObjectItemCaseSensitive(root, "schedule");
  if (!cJSON_IsArray(schedule) ||
      cJSON_GetArraySize(schedule) != LAN_PANEL_SCHEDULE_MAX)
    {
      goto out;
    }

  cJSON_ArrayForEach(item, schedule)
    {
      const cJSON *text;
      int id;
      int hour;
      int minute;
      size_t text_length;

      if (!cJSON_IsObject(item) ||
          !lan_panel_json_integer(
            cJSON_GetObjectItemCaseSensitive(item, "id"), 0,
            LAN_PANEL_SCHEDULE_MAX - 1, &id) || ids[id] ||
          !lan_panel_json_integer(
            cJSON_GetObjectItemCaseSensitive(item, "hour"), 0, 23, &hour) ||
          !lan_panel_json_integer(
            cJSON_GetObjectItemCaseSensitive(item, "minute"), 0, 59,
            &minute))
        {
          goto out;
        }

      text = cJSON_GetObjectItemCaseSensitive(item, "text");
      text_length = cJSON_IsString(text) ? strlen(text->valuestring) : 0;
      if (!lan_panel_utf8_valid(
            cJSON_IsString(text) ? text->valuestring : NULL, text_length))
        {
          goto out;
        }

      ids[id] = true;
      config->schedule[id].id = id;
      config->schedule[id].hour = hour;
      config->schedule[id].minute = minute;
      memcpy(config->schedule[id].text, text->valuestring, text_length + 1);
    }

  ret = 0;

out:
  cJSON_Delete(root);
  return ret;
}

static int lan_panel_config_generation(void)
{
  char data[LAN_PANEL_ROUTINE_CONFIG_MAX];
  const cJSON *generation;
  cJSON *root;
  size_t length;
  int value = 0;

  if (lan_panel_read_all(LAN_PANEL_ROUTINE_CONFIG, data, sizeof(data),
                         &length) < 0 || length == 0 ||
      length == sizeof(data))
    {
      return 0;
    }

  root = lan_panel_parse_json_body(data, length);
  generation = root == NULL ? NULL :
    cJSON_GetObjectItemCaseSensitive(root, "generation");
  if (cJSON_IsNumber(generation) &&
      generation->valuedouble == (double)generation->valueint &&
      generation->valueint > 0 && generation->valueint < INT_MAX)
    {
      value = generation->valueint;
    }
  cJSON_Delete(root);
  return value;
}

static int lan_panel_write_all(int fd, const char *data, size_t length)
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

static int lan_panel_write_routine_config(const char *body, size_t length)
{
  struct lan_panel_routine_config_s config;
  char temporary[sizeof(LAN_PANEL_ROUTINE_CONFIG) + 16];
  cJSON *schedule = NULL;
  cJSON *root = NULL;
  char *rendered = NULL;
  size_t rendered_length;
  int generation;
  int fd = -1;
  int ret;
  int i;

  memset(&config, 0, sizeof(config));
  ret = lan_panel_parse_routine_config(body, length, &config);
  if (ret < 0)
    {
      return ret;
    }

  generation = lan_panel_config_generation() + 1;
  root = cJSON_CreateObject();
  schedule = cJSON_CreateArray();
  if (root == NULL || schedule == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

  cJSON_AddNumberToObject(root, "schema", 1);
  cJSON_AddNumberToObject(root, "generation", generation);
  cJSON_AddNumberToObject(root, "temperature_low_x100",
                          config.temperature_low_x100);
  cJSON_AddNumberToObject(root, "temperature_high_x100",
                          config.temperature_high_x100);
  cJSON_AddNumberToObject(root, "history_interval_seconds",
                          config.history_interval_seconds);
  cJSON_AddItemToObject(root, "schedule", schedule);
  schedule = NULL;
  for (i = 0; i < LAN_PANEL_SCHEDULE_MAX; i++)
    {
      cJSON *entry = cJSON_CreateObject();
      if (entry == NULL)
        {
          ret = -ENOMEM;
          goto out;
        }
      cJSON_AddNumberToObject(entry, "id", config.schedule[i].id);
      cJSON_AddNumberToObject(entry, "hour", config.schedule[i].hour);
      cJSON_AddNumberToObject(entry, "minute", config.schedule[i].minute);
      cJSON_AddStringToObject(entry, "text", config.schedule[i].text);
      cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(root, "schedule"),
                           entry);
    }

  rendered = cJSON_PrintUnformatted(root);
  rendered_length = rendered == NULL ? 0 : strlen(rendered);
  if (rendered_length == 0 || rendered_length >= LAN_PANEL_ROUTINE_CONFIG_MAX)
    {
      ret = rendered == NULL ? -ENOMEM : -E2BIG;
      goto out;
    }

  if (mkdir(LAN_PANEL_ROUTINE_DIR, 0700) < 0 && errno != EEXIST)
    {
      ret = -errno;
      goto out;
    }

  snprintf(temporary, sizeof(temporary), "%s.tmp",
           LAN_PANEL_ROUTINE_CONFIG);
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

  ret = lan_panel_write_all(fd, rendered, rendered_length);
  if (ret == 0 && fsync(fd) < 0)
    {
      ret = -errno;
    }
  if (close(fd) < 0 && ret == 0)
    {
      ret = -errno;
    }
  fd = -1;
  if (ret == 0 && rename(temporary, LAN_PANEL_ROUTINE_CONFIG) < 0)
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
  memset(&config, 0, sizeof(config));
  return ret;
}

static bool lan_panel_sd_mount_ready(void)
{
  struct statfs filesystem;

  return statfs(LAN_PANEL_SD_MOUNT, &filesystem) == 0 &&
         (filesystem.f_type == FATFS_SUPER_MAGIC ||
          filesystem.f_type == MSDOS_SUPER_MAGIC);
}

static int lan_panel_prepare_sd_mount(void)
{
  struct stat info;
  int error;

  if (lan_panel_sd_mount_ready())
    {
      return 0;
    }

  if (stat(LAN_PANEL_SD_MOUNT, &info) < 0)
    {
      if (errno != ENOENT)
        {
          return -errno;
        }
      if (mkdir(LAN_PANEL_SD_MOUNT, 0755) < 0 && errno != EEXIST)
        {
          return -errno;
        }
    }
  else if (!S_ISDIR(info.st_mode))
    {
      return -ENOTDIR;
    }

  if (mount(LAN_PANEL_SD_DEVICE, LAN_PANEL_SD_MOUNT,
            LAN_PANEL_SD_FSTYPE, 0, NULL) < 0)
    {
      error = errno;
      if (error == EBUSY && lan_panel_sd_mount_ready())
        {
          return 0;
        }

      return error == ENOENT || error == ENXIO ? -ENODEV : -error;
    }

  return lan_panel_sd_mount_ready() ? 0 : -ENODEV;
}

static int lan_panel_export_history(char *path, size_t path_size)
{
  struct stat info;
  char target[PATH_MAX];
  char buffer[1024];
  time_t now;
  int source = -1;
  int output = -1;
  int attempt;
  int ret = 0;

  if (path == NULL || path_size == 0)
    {
      return -EINVAL;
    }

  ret = lan_panel_prepare_sd_mount();
  if (ret < 0)
    {
      return ret;
    }

  source = open(LAN_PANEL_HISTORY, O_RDONLY);
  if (source < 0)
    {
      return errno == ENOENT ? -ENODATA : -errno;
    }

  if (mkdir(LAN_PANEL_HISTORY_EXPORT_DIR, 0755) < 0 &&
      errno != EEXIST)
    {
      ret = -errno;
      goto out;
    }
  if (stat(LAN_PANEL_HISTORY_EXPORT_DIR, &info) < 0 ||
      !S_ISDIR(info.st_mode))
    {
      ret = -ENOTDIR;
      goto out;
    }

  now = time(NULL);
  for (attempt = 0; attempt < 100; attempt++)
    {
      int length;

      if (attempt == 0)
        {
          length = snprintf(target, sizeof(target),
                            LAN_PANEL_HISTORY_EXPORT_DIR
                            "/routine-history-%ld.jsonl", (long)now);
        }
      else
        {
          length = snprintf(target, sizeof(target),
                            LAN_PANEL_HISTORY_EXPORT_DIR
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
      ret = lan_panel_write_all(output, buffer, (size_t)count);
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

static bool lan_panel_safe_component(const char *value, size_t length)
{
  size_t i;

  if (value == NULL || length == 0 || length > 32 ||
      strstr(value, "..") != NULL)
    {
      return false;
    }

  for (i = 0; i < length; i++)
    {
      if (!((value[i] >= 'a' && value[i] <= 'z') ||
            (value[i] >= 'A' && value[i] <= 'Z') ||
            (value[i] >= '0' && value[i] <= '9') ||
            value[i] == '_' || value[i] == '-'))
        {
          return false;
        }
    }
  return true;
}

static int lan_panel_kws_path(const char *path, char *output, size_t size,
                              const char **type)
{
  const char *cursor;
  const char *slash;
  const char *extension;
  size_t session_length;
  size_t sample_length;
  char session[33];
  char sample[33];

  if (strncmp(path, "/api/v1/kws/", 12) != 0)
    {
      return -ENOENT;
    }
  cursor = path + 12;
  slash = strchr(cursor, '/');
  if (slash == NULL || strncmp(slash, "/samples/", 9) != 0)
    {
      return -EINVAL;
    }
  session_length = slash - cursor;
  cursor = slash + 9;
  extension = strrchr(cursor, '.');
  if (extension == NULL || extension == cursor ||
      strchr(cursor, '/') != NULL)
    {
      return -EINVAL;
    }
  sample_length = extension - cursor;
  if (session_length == 0 || session_length > 32 || sample_length == 0 ||
      sample_length > 32)
    {
      return -EINVAL;
    }

  memcpy(session, path + 12, session_length);
  session[session_length] = '\0';
  memcpy(sample, cursor, sample_length);
  sample[sample_length] = '\0';
  if (!lan_panel_safe_component(session, session_length) ||
      !lan_panel_safe_component(sample, sample_length))
    {
      return -EINVAL;
    }
  if (strcmp(extension, ".pcm") != 0 && strcmp(extension, ".json") != 0)
    {
      return -EINVAL;
    }
  if (snprintf(output, size, "%s/%s/%s%s", CONFIG_CONTEST2026_113_LAN_PANEL_KWS_ROOT,
               session, sample, extension) >= (int)size)
    {
      return -ENAMETOOLONG;
    }
  *type = strcmp(extension, ".json") == 0 ? "application/json" :
          "application/octet-stream";
  return 0;
}

static int lan_panel_handle(int fd, const char *token)
{
  struct lan_http_request_s request;
  char body[4096];
  char path[LAN_PANEL_PATH_MAX];
  const char *type;
  int ret;

  ret = lan_http_read_request(fd, &request);
  if (ret == -ENODATA)
    {
      /* Speculative connection that never sent a request.  Answering it
       * would waste the single serving slot other tabs are queued on.
       */

      return 0;
    }
  if (ret < 0)
    {
      return lan_http_send_response(fd, 400, "text/plain", "Bad request\n", 12);
    }
  (void)token;

  if (strcmp(request.method, "GET") != 0 && strcmp(request.method, "HEAD") != 0 &&
      strcmp(request.method, "POST") != 0)
    {
      return lan_http_send_response(fd, 405, "text/plain", "Method not allowed\n", 19);
    }

  if (strcmp(request.path, "/") == 0)
    {
      static const char page[] =
        "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>R528 桌面管家</title>"
        "<style>*{margin:0;padding:0;box-sizing:border-box}"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px}"
        ".container{max-width:1200px;margin:0 auto}"
        "h1{color:#fff;text-align:center;margin-bottom:12px;font-size:2em;text-shadow:0 2px 4px rgba(0,0,0,0.2)}"
        ".page-tools{display:flex;align-items:center;justify-content:flex-end;gap:10px;flex-wrap:wrap;margin-bottom:20px;color:#fff}"
        ".page-tools label{display:flex;align-items:center;gap:8px;font-size:.9em}"
        ".page-tools select{width:auto;min-width:96px;margin:0;padding:8px 10px;background:#fff}"
        ".page-tools button{width:auto;min-width:120px;margin:0;padding:9px 14px;background:#fff;color:#4454b8}"
        ".page-tools button:hover{background:#f1f3ff}"
        ".page-tools button:disabled{opacity:.65;cursor:wait}"
        ".refresh-meta{font-size:.85em;min-width:132px;text-align:right}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:20px}"
        ".card{background:#fff;border-radius:12px;padding:20px;box-shadow:0 8px 16px rgba(0,0,0,0.1);transition:transform 0.2s}"
        ".card:hover{transform:translateY(-4px);box-shadow:0 12px 24px rgba(0,0,0,0.15)}"
        ".card h2{color:#333;margin-bottom:15px;font-size:1.3em;border-bottom:2px solid #667eea;padding-bottom:10px}"
        ".metric{display:flex;justify-content:space-between;align-items:center;padding:10px 0;border-bottom:1px solid #f0f0f0}"
        ".metric:last-child{border:none}"
        ".metric-label{color:#666;font-size:0.9em}"
        ".metric-value{font-weight:600;color:#333;font-size:1.1em}"
        ".metric-unit{color:#999;font-size:0.85em;margin-left:4px}"
        ".temp{color:#ff6b6b}"
        ".humid{color:#4ecdc4}"
        ".light{color:#ffd93d}"
        ".co2{color:#a8dadc}"
        ".schedule-item{background:#f8f9fa;padding:12px;margin:8px 0;border-radius:8px;border-left:4px solid #667eea}"
        ".schedule-time{font-weight:600;color:#333;margin-bottom:4px}"
        ".schedule-text{color:#666;font-size:0.9em}"
        ".schedule-edit{display:grid;grid-template-columns:110px 1fr;gap:8px;align-items:center;margin:8px 0}"
        ".schedule-edit input{margin:0}"
        ".thresholds{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
        "input,select{width:100%;padding:10px;margin:8px 0;border:2px solid #e0e0e0;border-radius:8px;font-size:1em;transition:border 0.3s}"
        "input:focus,select:focus{outline:none;border-color:#667eea}"
        "button{width:100%;padding:12px;background:#667eea;color:#fff;border:none;border-radius:8px;font-size:1em;cursor:pointer;transition:background 0.3s;font-weight:600}"
        "button:hover{background:#5568d3}"
        ".status{padding:10px;border-radius:8px;margin:10px 0;text-align:center;font-weight:500}"
        ".success{background:#d4edda;color:#155724}"
        ".error{background:#f8d7da;color:#721c24}"
        ".wifi-status{display:flex;align-items:center;justify-content:space-between;background:#f8f9fa;padding:12px;border-radius:8px;margin:10px 0}"
        ".wifi-dot{width:12px;height:12px;border-radius:50%;margin-right:8px}"
        ".wifi-connected{background:#28a745}"
        ".wifi-disconnected{background:#dc3545}"
        ".slider-container{margin:15px 0}"
        ".slider-label{display:flex;justify-content:space-between;margin-bottom:8px;color:#666;font-size:0.9em}"
        ".slider{width:100%;height:8px;border-radius:4px;background:#e0e0e0;outline:none;-webkit-appearance:none}"
        ".slider::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;border-radius:50%;background:#667eea;cursor:pointer}"
        ".slider::-moz-range-thumb{width:20px;height:20px;border-radius:50%;background:#667eea;cursor:pointer;border:none}"
        ".data-table-wrap{background:#f8f9fa;border-radius:8px;max-height:420px;overflow:auto;color:#333}"
        ".data-table{width:100%;border-collapse:collapse;min-width:850px;font-size:.84em}"
        ".data-table th,.data-table td{padding:8px 9px;border-bottom:1px solid #e2e8f0;text-align:right;white-space:nowrap}"
        ".data-table th{position:sticky;top:0;z-index:1;background:#eef2ff;color:#3949ab;font-weight:700}"
        ".data-table th:nth-child(-n+3),.data-table td:nth-child(-n+3){text-align:left}"
        ".data-table tbody tr:nth-child(even){background:#fff}.data-table tbody tr:hover{background:#e8edff}"
        ".empty-data{padding:20px;text-align:center;color:#999;font-style:italic}"
        ".history-meta{display:block;margin:8px 0;color:#666;font-size:.85em}"
        ".modal{display:none;position:fixed;inset:0;z-index:1000;background:rgba(12,16,24,.72);align-items:center;justify-content:center;padding:18px}"
        ".modal.open{display:flex}"
        ".modal-card{width:min(960px,100%);max-height:94vh;overflow:auto;background:#fff;border-radius:14px;padding:20px;box-shadow:0 20px 60px rgba(0,0,0,.35)}"
        ".modal-card h2{margin-bottom:12px;color:#333}"
        ".curve-wrap{position:relative;margin:12px 0}.curve-canvas{display:block;width:100%;height:auto;min-height:220px;background:#111827;border-radius:9px;touch-action:none}"
        ".curve-tooltip{display:none;position:absolute;z-index:2;max-width:260px;padding:9px 11px;border-radius:7px;background:rgba(15,23,42,.96);color:#f8fafc;font-size:.78em;line-height:1.5;white-space:pre-line;pointer-events:none;box-shadow:0 5px 18px rgba(0,0,0,.35)}"
        ".curve-points-title{margin:14px 0 8px;color:#444;font-size:1em}"
        ".curve-controls{display:grid;grid-template-columns:minmax(180px,1fr) repeat(3,minmax(110px,auto));gap:10px;align-items:end}"
        ".curve-controls label{color:#555}"
        ".curve-controls select{margin-bottom:0}"
        ".curve-controls button{margin:0}"
        ".raw-data{display:none}"
        "@media(max-width:600px){.page-tools{justify-content:stretch}.page-tools label{flex:1}.page-tools button{flex:1}.refresh-meta{width:100%;text-align:left}.curve-controls{grid-template-columns:1fr}.modal{padding:8px}.modal-card{padding:14px}}"
        "</style>"
        "<div class=container><h1>🏠 R528 桌面管家</h1>"
        "<div class=page-tools>"
        "<button id=refreshNow type=button onclick='refreshStatus()'>&#8635; 立即刷新</button>"
        "<button id=curveOpen type=button onclick='openCurve()'>📈 运行曲线</button>"
        "<label>自动刷新<select id=refreshInterval onchange='setRefreshInterval(this.value)' aria-label='自动刷新周期'>"
        "<option value=0>关闭</option><option value=2>2 秒</option><option value=5>5 秒</option>"
        "<option value=10 selected>10 秒</option><option value=30>30 秒</option><option value=60>60 秒</option>"
        "</select></label><span id=refreshMeta class=refresh-meta>尚未刷新</span></div>"
        "<div class=grid>"
        "<div class=card><h2>📊 环境监测</h2>"
        "<div class=metric><span class=metric-label>🌡️ 温度</span>"
        "<span class=metric-value><span id=temp class=temp>--</span><span class=metric-unit>°C</span></span></div>"
        "<div class=metric><span class=metric-label>💧 湿度</span>"
        "<span class=metric-value><span id=humid class=humid>--</span><span class=metric-unit>%</span></span></div>"
        "<div class=metric><span class=metric-label>💡 光照</span>"
        "<span class=metric-value><span id=light class=light>--</span><span class=metric-unit>lux</span></span></div>"
        "<div class=metric><span class=metric-label>🌫️ CO₂</span>"
        "<span class=metric-value><span id=co2 class=co2>--</span><span class=metric-unit>ppm</span></span></div>"
        "<div class=metric><span class=metric-label>🧪 TVOC</span>"
        "<span class=metric-value><span id=tvoc>--</span><span class=metric-unit>ppb</span></span></div>"
        "<div class='raw-data' id=status></div></div>"
        "<div class=card><h2>📅 今日日程</h2><div id=schedule>加载中...</div></div>"
        "<div class=card><h2>温度告警与日程编辑</h2>"
        "<div class=thresholds><label>低温阈值（℃）<input id=tempLow type=number min=-40 max=124 step=0.5></label>"
        "<label>高温阈值（℃）<input id=tempHigh type=number min=-39 max=125 step=0.5></label></div>"
        "<div class=schedule-edit><input id=scheduleTime0 type=time aria-label='日程一时间'><input id=scheduleText0 maxlength=16 aria-label='日程一内容'></div>"
        "<div class=schedule-edit><input id=scheduleTime1 type=time aria-label='日程二时间'><input id=scheduleText1 maxlength=16 aria-label='日程二内容'></div>"
        "<div class=schedule-edit><input id=scheduleTime2 type=time aria-label='日程三时间'><input id=scheduleText2 maxlength=16 aria-label='日程三内容'></div>"
        "<button onclick='saveRoutine()'>保存温度与日程</button><div id=routineResult></div></div>"
        "<div class=card><h2>💡 智能照明</h2>"
        "<div class=slider-container><div class=slider-label><span>光照阈值</span><span id=threshVal>500 lux</span></div>"
        "<input type=range class=slider id=thresh min=100 max=2400 step=100 value=500 oninput='updateThresh(this.value)'></div>"
        "<div class=slider-container><div class=slider-label><span>档位数</span><span id=levelsVal>5 档</span></div>"
        "<input type=range class=slider id=levels min=3 max=10 step=1 value=5 oninput='updateLevels(this.value)'></div>"
        "<button onclick='saveLight()'>保存配置</button><div id=lightResult></div></div>"
        "<div class=card><h2>📶 Wi-Fi 设置</h2>"
        "<div class=wifi-status><div style='display:flex;align-items:center'>"
        "<div class=wifi-dot id=wifiDot></div><span id=wifiText>检查中...</span></div>"
        "<span id=wifiIp style='color:#999;font-size:0.9em'>--</span></div>"
        "<div class='raw-data' id=wifiStatus></div>"
        "<input id=ssid placeholder='WiFi 名称'>"
        "<input id=pass type=password placeholder='WiFi 密码'>"
        "<button onclick='setWifi()'>连接 Wi-Fi</button><div id=wifiResult></div></div>"
        "<div class=card><h2>📜 历史记录</h2>"
        "<button id=historyLoad onclick='loadHistory()'>加载全部历史</button>"
        "<span id=historyMeta class=history-meta>每次点击都会从设备重新读取</span>"
        "<div class=data-table-wrap id=history><div class=empty-data>点击按钮加载历史数据</div></div></div>"
        "</div></div>"
        "<div id=curveModal class=modal role=dialog aria-modal=true aria-labelledby=curveTitle>"
        "<div class=modal-card><h2 id=curveTitle>📈 运行曲线</h2>"
        "<div id=curveMeta class=history-meta>正在加载全部历史数据...</div>"
        "<div class=curve-wrap><canvas id=curveCanvas class=curve-canvas width=900 height=380></canvas>"
        "<div id=curveTooltip class=curve-tooltip></div></div>"
        "<div class=curve-controls><label>曲线记录间隔<select id=historyInterval>"
        "<option value=5>5 秒</option><option value=10>10 秒</option>"
        "<option value=60>1 分钟</option><option value=300>5 分钟</option>"
        "<option value=600>10 分钟</option><option value=1800>30 分钟</option>"
        "<option value=3600>1 小时</option></select></label>"
        "<button type=button onclick='saveRoutine(true)'>保存间隔</button>"
        "<button type=button onclick='exportHistory()'>导出到SD卡</button>"
        "<button type=button onclick='closeCurve()'>关闭</button></div>"
        "<div id=curveResult></div><h3 class=curve-points-title>曲线点数据（每个点的实际数值）</h3>"
        "<div id=curvePoints class=data-table-wrap><div class=empty-data>暂无曲线数据</div></div></div></div>"
        "<script>function el(i){return document.getElementById(i)}"
        "function tok(){return sessionStorage.token||''}"
        "function hdr(){return{'Authorization':'Bearer '+tok()}}"
        "function ask(){for(;;){const t=prompt('请输入设备屏幕显示的4位访问令牌:');"
        "if(t===null)return false;if(/^\\d{4}$/.test(t)){sessionStorage.token=t;return true}alert('令牌必须是4位数字')}}"
        "function networkError(e){return e instanceof TypeError||/NetworkError|Failed to fetch|Load failed/i.test(String(e&&e.message||e))}"
        "function retryFetch(u,o,retries){return fetch(u,o).catch(e=>{if(retries<=0||!networkError(e))throw e;"
        "const wait=retries===2?250:600;return new Promise(resolve=>setTimeout(resolve,wait)).then(()=>retryFetch(u,o,retries-1))})}"
        "function configPost(u,body){return retryFetch(u,{method:'POST',cache:'no-store',"
        "headers:Object.assign({'Content-Type':'application/json'},hdr()),body:body},2)}"
        "function get(u){return retryFetch(u,{headers:hdr(),cache:'no-store'},1).then(r=>{"
        "if(r.status==401){sessionStorage.removeItem('token');throw new Error('令牌无效，请刷新页面重新输入')}"
        "if(!r.ok)throw new Error('HTTP '+r.status);return r})}"
        "function showStatus(id,msg,success){const e=el(id);if(e){e.textContent=msg;e.className='status '+(success?'success':'error');setTimeout(()=>e.textContent='',3000)}}"
        "function renderSchedule(rows){const out=el('schedule');out.replaceChildren();"
        "if(!rows||!rows.length){const empty=document.createElement('p');empty.textContent='暂无日程';out.appendChild(empty);return}"
        "rows.forEach(s=>{const item=document.createElement('div'),time=document.createElement('div'),text=document.createElement('div');"
        "item.className='schedule-item';time.className='schedule-time';text.className='schedule-text';"
        "time.textContent=String(s.hour).padStart(2,'0')+':'+String(s.minute).padStart(2,'0');"
        "text.textContent=s.text||['喝水','专注','通风'][s.text_id]||'任务';item.append(time,text);out.appendChild(item)})}"
        "let refreshTimer=0,refreshInFlight=false;"
        "const refreshValues=['0','2','5','10','30','60'];"
        "function scheduleRefresh(){clearTimeout(refreshTimer);"
        "const v=el('refreshInterval').value;localStorage.setItem('refreshSeconds',v);"
        "if(parseInt(v,10)>0)refreshTimer=setTimeout(refreshStatus,parseInt(v,10)*1000)}"
        "function setRefreshInterval(v){if(!refreshValues.includes(v))v='10';"
        "el('refreshInterval').value=v;scheduleRefresh()}"
        "function refreshStatus(){if(!tok()||refreshInFlight)return;"
        "clearTimeout(refreshTimer);refreshInFlight=true;let refreshOk=true;"
        "const button=el('refreshNow');button.disabled=true;"
        "const statusRequest=get('/api/v1/status').then(r=>r.json()).then(d=>{"
        "el('status').textContent=JSON.stringify(d,null,2);"
        "el('temp').textContent=(d.temperature_x100/100).toFixed(1);"
        "el('humid').textContent=(d.humidity_x100/100).toFixed(1);"
        "el('light').textContent=(d.light_x10/10).toFixed(0);"
        "el('co2').textContent=d.eco2_ppm;"
        "el('tvoc').textContent=d.tvoc_ppb;"
        "renderSchedule(d.schedule)})"
        ".catch(e=>{refreshOk=false;el('status').textContent='错误: '+e.message});"
        "const wifiRequest=get('/api/v1/wifi/status').then(r=>r.json()).then(d=>{"
        "el('wifiStatus').textContent=JSON.stringify(d,null,2);"
        "const dot=el('wifiDot'),txt=el('wifiText'),ip=el('wifiIp');"
        "if(d.ipv4_ready){dot.className='wifi-dot wifi-connected';txt.textContent='已连接';ip.textContent=d.ipv4}"
        "else{dot.className='wifi-dot wifi-disconnected';txt.textContent='未连接';ip.textContent='--'}})"
        ".catch(e=>{refreshOk=false;el('wifiStatus').textContent='错误: '+e.message;el('wifiDot').className='wifi-dot wifi-disconnected';el('wifiText').textContent='检查失败'});"
        "Promise.all([statusRequest,wifiRequest]).then(()=>{"
        "el('refreshMeta').textContent=(refreshOk?'已更新 ':'刷新失败 ')+new Date().toLocaleTimeString()})"
        ".finally(()=>{refreshInFlight=false;button.disabled=false;scheduleRefresh()})}"
        "function setWifi(){const s=el('ssid').value,p=el('pass').value;"
        "if(!s||!p){showStatus('wifiResult','请填写完整信息',false);return}"
        "if(!tok()){showStatus('wifiResult','缺少令牌，请刷新页面',false);return}"
        "configPost('/api/v1/wifi/config',JSON.stringify({ssid:s,password:p}))"
        ".then(r=>{if(r.status==401){sessionStorage.removeItem('token');throw new Error('令牌无效，请刷新页面重新输入')}"
        "return r.text().then(t=>{let d={};try{d=JSON.parse(t)}catch(x){}if(!r.ok)throw new Error('HTTP '+r.status);return d})})"
        ".then(d=>{showStatus('wifiResult',d.ok?'✓ 配置已提交':'✗ 配置失败',d.ok);if(d.ok)setTimeout(refreshStatus,2000)})"
        ".catch(e=>showStatus('wifiResult','✗ '+e.message,false))}"
        "function historyLines(t){return t.trim().split('\\n').map(l=>l.trim()).filter(l=>l)}"
        "function historyRowsFromText(t){const rows=[];historyLines(t).forEach(l=>{try{const r=JSON.parse(l);if(r&&typeof r==='object')rows.push(r)}catch(e){}});return rows}"
        "function fetchHistoryText(){return get('/api/v1/history').then(r=>r.text())}"
        "function historyDate(r){const day=Number(r.day);if(!Number.isInteger(day))return '--';const d=new Date(day*86400000);return Number.isFinite(d.getTime())?d.toISOString().slice(0,10):'--'}"
        "function historyTime(r){return String(Number(r.hour)||0).padStart(2,'0')+':'+String(Number(r.minute)||0).padStart(2,'0')+':'+String(Number(r.second)||0).padStart(2,'0')}"
        "function historyStamp(r){return historyDate(r)+' '+historyTime(r)}"
        "function measure(v,f,d,u){const n=Number(v);return Number.isFinite(n)?(n*f).toFixed(d)+' '+u:'--'}"
        "function intervalText(v){const n=Number(v);if(!Number.isFinite(n))return '--';if(n<60)return n+' 秒';if(n%3600===0)return n/3600+' 小时';return n/60+' 分钟'}"
        "const historyColumns=[['序号',(r,i)=>String(i+1)],['日期',r=>historyDate(r)],['时间',r=>historyTime(r)],"
        "['温度',r=>measure(r.temperature_x100,.01,2,'℃')],['湿度',r=>measure(r.humidity_x100,.01,2,'%')],"
        "['光照',r=>measure(r.light_x10,.1,1,'lux')],['eCO₂',r=>measure(r.eco2_ppm,1,0,'ppm')],"
        "['TVOC',r=>measure(r.tvoc_ppb,1,0,'ppb')],['记录间隔',r=>intervalText(r.interval_seconds)]];"
        "function renderDataTable(rows,targetId){const target=el(targetId);target.replaceChildren();if(!rows.length){const empty=document.createElement('div');empty.className='empty-data';empty.textContent='暂无历史数据';target.appendChild(empty);return}"
        "const table=document.createElement('table'),head=document.createElement('thead'),hr=document.createElement('tr'),body=document.createElement('tbody');table.className='data-table';"
        "historyColumns.forEach(c=>{const th=document.createElement('th');th.textContent=c[0];hr.appendChild(th)});head.appendChild(hr);"
        "rows.forEach((r,i)=>{const tr=document.createElement('tr');historyColumns.forEach(c=>{const td=document.createElement('td');td.textContent=c[1](r,i);tr.appendChild(td)});body.appendChild(tr)});"
        "table.append(head,body);target.appendChild(table)}"
        "let historyRows=[];"
        "function loadHistory(){if(!tok())return;const button=el('historyLoad'),meta=el('historyMeta');"
        "button.disabled=true;el('history').textContent='正在从设备重新读取全部历史...';"
        "fetchHistoryText().then(t=>{historyRows=historyRowsFromText(t);renderDataTable(historyRows,'history');"
        "meta.textContent=historyRows.length?'本次重新加载 '+historyRows.length+' 条 · '+historyStamp(historyRows[0])+' — '+historyStamp(historyRows[historyRows.length-1])+' · 加载于 '+new Date().toLocaleTimeString():'本次重新加载 0 条 · '+new Date().toLocaleTimeString()})"
        ".catch(e=>{el('history').textContent='加载失败: '+e.message;meta.textContent='加载失败'})"
        ".finally(()=>{button.disabled=false})}"
        "function drawCurve(rows){const c=el('curveCanvas'),x=c.getContext('2d'),w=c.width,h=c.height,L=52,R=18,T=50,B=36;"
        "x.fillStyle='#111827';x.fillRect(0,0,w,h);x.strokeStyle='#334155';x.lineWidth=1;"
        "for(let i=0;i<=5;i++){const y=T+(h-T-B)*i/5;x.beginPath();x.moveTo(L,y);x.lineTo(w-R,y);x.stroke()}"
        "if(!rows.length){x.fillStyle='#cbd5e1';x.font='22px sans-serif';x.fillText('暂无历史数据',L,T+40);return}"
        "const defs=[['temperature_x100',.01,'温度℃','#fb7185'],['humidity_x100',.01,'湿度%','#2dd4bf'],['light_x10',.1,'光照lux','#facc15'],['eco2_ppm',1,'eCO2 ppm','#60a5fa']];"
        "defs.forEach((d,di)=>{const vals=rows.map(r=>Number(r[d[0]])*d[1]).filter(Number.isFinite);if(!vals.length)return;"
        "let lo=vals[0],hi=vals[0];vals.forEach(v=>{if(v<lo)lo=v;if(v>hi)hi=v});if(hi===lo){hi+=1;lo-=1}x.strokeStyle=d[3];x.lineWidth=2;x.beginPath();let started=false;"
        "rows.forEach((r,i)=>{const v=Number(r[d[0]])*d[1];if(!Number.isFinite(v))return;const px=L+(w-L-R)*(rows.length===1?0:i/(rows.length-1)),py=T+(h-T-B)*(1-(v-lo)/(hi-lo));if(!started){x.moveTo(px,py);started=true}else x.lineTo(px,py)});x.stroke();"
        "x.fillStyle=d[3];rows.forEach((r,i)=>{const v=Number(r[d[0]])*d[1];if(!Number.isFinite(v))return;const px=L+(w-L-R)*(rows.length===1?0:i/(rows.length-1)),py=T+(h-T-B)*(1-(v-lo)/(hi-lo));x.beginPath();x.arc(px,py,2.8,0,Math.PI*2);x.fill();if(rows.length<=18){x.font='9px sans-serif';x.fillText(v.toFixed(d[1]<1?1:0),px+4,py-4)}});"
        "x.font='14px sans-serif';const latest=vals[vals.length-1];x.fillText(d[2]+' '+latest.toFixed(d[1]<1?1:0)+'  ['+lo.toFixed(1)+'~'+hi.toFixed(1)+']',L+di*205,22)});"
        "x.fillStyle='#94a3b8';x.fillText(historyStamp(rows[0]),L,h-12);const last=historyStamp(rows[rows.length-1]);x.fillText(last,w-R-x.measureText(last).width,h-12)}"
        "function curvePointer(e){if(!historyRows.length)return;const c=el('curveCanvas'),rect=c.getBoundingClientRect(),L=52,R=18,px=(e.clientX-rect.left)*c.width/rect.width;"
        "let i=historyRows.length===1?0:Math.round((px-L)/(c.width-L-R)*(historyRows.length-1));i=Math.max(0,Math.min(historyRows.length-1,i));const r=historyRows[i],tip=el('curveTooltip');"
        "tip.textContent='第 '+(i+1)+' 点  '+historyStamp(r)+'\\n温度 '+measure(r.temperature_x100,.01,2,'℃')+'  湿度 '+measure(r.humidity_x100,.01,2,'%')+'\\n光照 '+measure(r.light_x10,.1,1,'lux')+'  eCO₂ '+measure(r.eco2_ppm,1,0,'ppm')+'\\nTVOC '+measure(r.tvoc_ppb,1,0,'ppb');"
        "tip.style.display='block';tip.style.left=Math.max(4,Math.min(rect.width-260,e.clientX-rect.left+12))+'px';tip.style.top=Math.max(4,e.clientY-rect.top-80)+'px'}"
        "function hideCurveTooltip(){el('curveTooltip').style.display='none'}"
        "function loadCurveHistory(){const meta=el('curveMeta');meta.textContent='正在从设备重新读取全部历史数据...';"
        "return fetchHistoryText().then(t=>{historyRows=historyRowsFromText(t);drawCurve(historyRows);renderDataTable(historyRows,'curvePoints');"
        "meta.textContent=historyRows.length?'已加载 '+historyRows.length+' 条：'+historyStamp(historyRows[0])+' — '+historyStamp(historyRows[historyRows.length-1]):'暂无历史数据'}).catch(e=>{meta.textContent='曲线加载失败: '+e.message;drawCurve([]);renderDataTable([],'curvePoints')})}"
        "function openCurve(){el('curveModal').classList.add('open');loadRoutine();loadCurveHistory()}"
        "function closeCurve(){el('curveModal').classList.remove('open');hideCurveTooltip()}"
        "function exportHistory(){if(!tok())return;if(!confirm('确认将全部历史数据导出到SD卡吗？'))return;"
        "fetch('/api/v1/history/export',{method:'POST',cache:'no-store',headers:hdr()})"
        ".then(r=>r.text().then(t=>{let d={};try{d=JSON.parse(t)}catch(e){}if(r.status===401){sessionStorage.removeItem('token');throw new Error('令牌无效')}if(!r.ok)throw new Error(r.status===503?'未检测到SD卡':r.status===409?'暂无历史数据':'HTTP '+r.status);return d}))"
        ".then(d=>showStatus('curveResult','已导出到 '+d.path,true)).catch(e=>showStatus('curveResult','导出失败: '+e.message,false))}"
        "let lightCfg={thresh:500,levels:5};"
        "function updateThresh(v){el('threshVal').textContent=v+' lux';lightCfg.thresh=parseInt(v)}"
        "function updateLevels(v){el('levelsVal').textContent=v+' 档';lightCfg.levels=parseInt(v)}"
        "function saveLight(){if(!tok()){showStatus('lightResult','缺少令牌',false);return}"
        "configPost('/api/v1/light/config',JSON.stringify(lightCfg))"
        ".then(r=>{if(r.status==401){sessionStorage.removeItem('token');throw new Error('令牌无效')}"
        "return r.text().then(t=>{let d={};try{d=JSON.parse(t)}catch(x){}if(!r.ok)throw new Error('HTTP '+r.status);return d})})"
        ".then(d=>showStatus('lightResult',d.ok?'✓ 照明配置已保存':'✗ 保存失败',d.ok))"
        ".catch(e=>showStatus('lightResult','✗ '+e.message,false))}"
        "function loadRoutine(){if(!tok())return;get('/api/v1/routine/config').then(r=>r.json()).then(d=>{"
        "el('tempLow').value=(d.temperature_low_x100/100).toFixed(1);el('tempHigh').value=(d.temperature_high_x100/100).toFixed(1);"
        "const hi=Number(d.history_interval_seconds)||60;el('historyInterval').value=['5','10','60','300','600','1800','3600'].includes(String(hi))?String(hi):'60';"
        "d.schedule.forEach(s=>{el('scheduleTime'+s.id).value=String(s.hour).padStart(2,'0')+':'+String(s.minute).padStart(2,'0');el('scheduleText'+s.id).value=s.text})})"
        ".catch(e=>showStatus('routineResult','加载失败: '+e.message,false))}"
        "function saveRoutine(fromCurve){if(!tok()){showStatus(fromCurve?'curveResult':'routineResult','缺少令牌',false);return}"
        "const low=Math.round(Number(el('tempLow').value)*100),high=Math.round(Number(el('tempHigh').value)*100),interval=Number(el('historyInterval').value),schedule=[];"
        "for(let i=0;i<3;i++){const parts=el('scheduleTime'+i).value.split(':'),text=el('scheduleText'+i).value.trim();"
        "if(parts.length!==2||!text){showStatus('routineResult','请填写全部日程',false);return}schedule.push({id:i,hour:Number(parts[0]),minute:Number(parts[1]),text:text})}"
        "if(!Number.isInteger(low)||!Number.isInteger(high)||high-low<100){showStatus('routineResult','高低温至少相差 1℃',false);return}"
        "configPost('/api/v1/routine/config',JSON.stringify({temperature_low_x100:low,temperature_high_x100:high,history_interval_seconds:interval,schedule:schedule}))"
        ".then(r=>{if(r.status==401){sessionStorage.removeItem('token');throw new Error('令牌无效')}if(!r.ok)throw new Error('HTTP '+r.status);return r.json()})"
        ".then(()=>{showStatus(fromCurve?'curveResult':'routineResult',fromCurve?'曲线记录间隔已保存':'配置已保存',true);loadRoutine();setTimeout(refreshStatus,1200)})"
        ".catch(e=>showStatus(fromCurve?'curveResult':'routineResult','保存失败: '+e.message,false))}"
        "el('curveCanvas').addEventListener('pointermove',curvePointer);el('curveCanvas').addEventListener('pointerdown',curvePointer);el('curveCanvas').addEventListener('pointerleave',hideCurveTooltip);"
        "if(!tok()&&!ask()){document.body.innerHTML='<h1 style=color:#fff;text-align:center;padding:50px>需要令牌</h1>'}else{"
        "const saved=localStorage.getItem('refreshSeconds');"
        "if(saved&&refreshValues.includes(saved))el('refreshInterval').value=saved;"
        "refreshStatus();loadRoutine()}</script>";
      return lan_http_send_response(fd, 200, "text/html", page, sizeof(page) - 1);
    }

  if (strcmp(request.path, "/favicon.ico") == 0)
    {
      /* The browser fetches this without the page's Authorization header, so
       * answering 401 here just logs a console error on every load.
       */

      return lan_http_send_response(fd, 204, "image/x-icon", NULL, 0);
    }

  if (lan_panel_authorized(&request) < 0)
    {
      return lan_http_send_response(fd, 401, "text/plain", "Unauthorized\n",
                                    strlen("Unauthorized\n"));
    }

  if (strcmp(request.path, "/api/v1/status") == 0)
    {
      size_t length = 0;
      int attempt;

      /* routine_manager republishes this file by rename(), so a request that
       * lands in that window sees ENOENT even though status is healthy.
       * Retry briefly instead of reporting 503 to the page.
       */

      for (attempt = 0; attempt < 3; attempt++)
        {
          ret = lan_panel_read_all(LAN_PANEL_STATUS, body, sizeof(body),
                                   &length);
          if (ret == 0 && length > 0)
            {
              break;
            }
          usleep(30000);
        }

      if (ret < 0 || length == 0)
        {
          return lan_http_send_response(fd, 503, "text/plain",
                                        "Status unavailable\n", 19);
        }
      return lan_http_send_response(fd, 200, "application/json", body, length);
    }

  if (strcmp(request.path, "/api/v1/wifi/status") == 0)
    {
      return lan_http_send_file(fd, LAN_PANEL_WIFI_STATUS, "application/json");
    }

  if (strcmp(request.path, "/api/v1/routine/config") == 0 &&
      (strcmp(request.method, "GET") == 0 ||
       strcmp(request.method, "HEAD") == 0))
    {
      static const char defaults[] =
        "{\"schema\":1,\"generation\":0,"
        "\"temperature_low_x100\":1600,"
        "\"temperature_high_x100\":3000,"
        "\"history_interval_seconds\":60,\"schedule\":["
        "{\"id\":0,\"hour\":9,\"minute\":0,\"text\":\"喝水\"},"
        "{\"id\":1,\"hour\":14,\"minute\":30,\"text\":\"专注\"},"
        "{\"id\":2,\"hour\":18,\"minute\":0,\"text\":\"通风\"}]}";

      if (access(LAN_PANEL_ROUTINE_CONFIG, R_OK) == 0)
        {
          return lan_http_send_file(fd, LAN_PANEL_ROUTINE_CONFIG,
                                    "application/json");
        }
      return lan_http_send_response(fd, 200, "application/json", defaults,
                                    sizeof(defaults) - 1);
    }

  if (strcmp(request.path, "/api/v1/wifi/config") == 0 &&
      strcmp(request.method, "POST") == 0)
    {
      static const char accepted[] = "{\"ok\":true}\n";
      static const char rejected[] = "{\"ok\":false}\n";
      const char *reply;
      int status;

      /* Derive the length from the literal.  Hand-counted lengths overran
       * these bodies and the trailing garbage broke JSON.parse in the page.
       */

      pthread_mutex_lock(&g_mutation_lock);
      ret = lan_panel_write_wifi(request.body, request.body_length);
      pthread_mutex_unlock(&g_mutation_lock);
      reply = ret < 0 ? rejected : accepted;
      status = ret == 0 ? 202 :
               (ret == -EINVAL || ret == -E2BIG ? 400 : 500);
      if (ret < 0)
        {
          printf("lan_panel: Wi-Fi config failed: %d (body=%lu bytes)\n",
                 -ret, (unsigned long)request.body_length);
        }

      return lan_http_send_response(fd, status, "application/json",
                                    reply, strlen(reply));
    }

  if (strcmp(request.path, "/api/v1/light/config") == 0 &&
      strcmp(request.method, "POST") == 0)
    {
      static const char accepted[] = "{\"ok\":true}\n";
      static const char rejected[] = "{\"ok\":false}\n";
      const char *reply;
      int status;

      pthread_mutex_lock(&g_mutation_lock);
      ret = lan_panel_write_light_config(request.body, request.body_length);
      pthread_mutex_unlock(&g_mutation_lock);
      reply = ret < 0 ? rejected : accepted;
      status = ret == 0 ? 202 :
               (ret == -EINVAL || ret == -E2BIG ? 400 : 500);
      if (ret < 0)
        {
          printf("lan_panel: light config failed: %d (body=%lu bytes)\n",
                 -ret, (unsigned long)request.body_length);
        }

      return lan_http_send_response(fd, status, "application/json",
                                    reply, strlen(reply));
    }

  if (strcmp(request.path, "/api/v1/routine/config") == 0 &&
      strcmp(request.method, "POST") == 0)
    {
      static const char accepted[] = "{\"ok\":true}\n";
      static const char rejected[] = "{\"ok\":false}\n";
      const char *reply;
      int status;

      pthread_mutex_lock(&g_mutation_lock);
      ret = lan_panel_write_routine_config(request.body,
                                           request.body_length);
      pthread_mutex_unlock(&g_mutation_lock);
      reply = ret < 0 ? rejected : accepted;
      status = ret == 0 ? 202 :
               (ret == -EINVAL || ret == -E2BIG ? 400 : 500);
      if (ret < 0)
        {
          printf("lan_panel: routine config failed: %d (body=%lu bytes)\n",
                 -ret, (unsigned long)request.body_length);
        }

      return lan_http_send_response(fd, status, "application/json",
                                    reply, strlen(reply));
    }

  if (strcmp(request.path, "/api/v1/history/export") == 0 &&
      strcmp(request.method, "POST") == 0)
    {
      int status;
      int length;

      pthread_mutex_lock(&g_mutation_lock);
      ret = lan_panel_export_history(path, sizeof(path));
      pthread_mutex_unlock(&g_mutation_lock);
      if (ret < 0)
        {
          length = snprintf(body, sizeof(body),
                            "{\"ok\":false,\"error\":%d}\n", -ret);
          status = ret == -ENODATA ? 409 :
                   (ret == -ENODEV || ret == -ENOTDIR ? 503 : 500);
        }
      else
        {
          length = snprintf(body, sizeof(body),
                            "{\"ok\":true,\"path\":\"%s\"}\n", path);
          status = 200;
        }

      if (length < 0 || (size_t)length >= sizeof(body))
        {
          return lan_http_send_response(fd, 500, "application/json",
                                        "{\"ok\":false}\n", 13);
        }
      return lan_http_send_response(fd, status, "application/json", body,
                                    (size_t)length);
    }

  /* Both logs only appear after the first completed slot.  An absent file is
   * an empty history, not a routing error, so answer 200 with no records.
   */

  if (strcmp(request.path, "/api/v1/history") == 0)
    {
      if (access(LAN_PANEL_HISTORY, R_OK) < 0)
        {
          return lan_http_send_response(fd, 200, "application/x-ndjson",
                                        NULL, 0);
        }
      return lan_http_send_file(fd, LAN_PANEL_HISTORY,
                                "application/x-ndjson");
    }

  if (strcmp(request.path, "/api/v1/events") == 0)
    {
      if (access(LAN_PANEL_EVENTS, R_OK) < 0)
        {
          return lan_http_send_response(fd, 200, "application/x-ndjson",
                                        NULL, 0);
        }
      return lan_http_send_file(fd, LAN_PANEL_EVENTS,
                                "application/x-ndjson");
    }

  if (strncmp(request.path, "/api/v1/kws/", 12) == 0)
    {
      ret = lan_panel_kws_path(request.path, path, sizeof(path), &type);
      if (ret < 0)
        {
          return lan_http_send_response(fd, 404, "text/plain", "Not found\n", 10);
        }
      return lan_http_send_file(fd, path, type);
    }

  return lan_http_send_response(fd, 404, "text/plain", "Not found\n", 10);
}

struct lan_panel_worker_s
{
  int fd;
};

static int lan_panel_worker_slot_acquire(void)
{
  int ret = 0;

  pthread_mutex_lock(&g_worker_lock);
  while (!g_stop && g_worker_count >= LAN_PANEL_MAX_WORKERS)
    {
      ret = pthread_cond_wait(&g_worker_cond, &g_worker_lock);
      if (ret != 0)
        {
          ret = -ret;
          break;
        }
    }

  if (ret == 0)
    {
      if (g_stop)
        {
          ret = -EINTR;
        }
      else
        {
          g_worker_count++;
        }
    }
  pthread_mutex_unlock(&g_worker_lock);
  return ret;
}

static void lan_panel_worker_slot_release(void)
{
  pthread_mutex_lock(&g_worker_lock);
  if (g_worker_count > 0)
    {
      g_worker_count--;
    }
  pthread_cond_broadcast(&g_worker_cond);
  pthread_mutex_unlock(&g_worker_lock);
}

static void lan_panel_wait_for_workers(void)
{
  pthread_mutex_lock(&g_worker_lock);
  while (g_worker_count > 0)
    {
      pthread_cond_wait(&g_worker_cond, &g_worker_lock);
    }
  pthread_mutex_unlock(&g_worker_lock);
}

static void *lan_panel_connection_worker(void *argument)
{
  struct lan_panel_worker_s *worker = argument;
  int fd = worker->fd;

  free(worker);
  lan_panel_handle(fd, NULL);
  lan_http_close(fd);
  lan_panel_worker_slot_release();
  return NULL;
}

/****************************************************************************
 * Light Control Thread (GPIO software PWM adaptive lighting)
 ****************************************************************************/

#ifdef CONFIG_DEV_GPIO

#define LIGHT_CONFIG_PATH "/data/light.cfg"
#define LED_GPIO_DEVICE "/dev/gpio2"
#define LED_ON_VALUE 0
#define LED_OFF_VALUE 1
#define ROUTINE_LIGHT_MASK (1u << 2)
#define SOFT_PWM_FREQUENCY 100
#define POLL_INTERVAL_MS 2000

struct light_config_s
{
  int threshold_lux;
  int levels;
};

static int load_light_config(struct light_config_s *config)
{
  FILE *fp;
  char line[128];

  config->threshold_lux = 500;
  config->levels = 5;

  fp = fopen(LIGHT_CONFIG_PATH, "r");
  if (fp == NULL)
    {
      return 0;
    }

  while (fgets(line, sizeof(line), fp) != NULL)
    {
      int value;
      if (sscanf(line, "THRESHOLD=%d", &value) == 1 &&
          value >= 100 && value <= 2400)
        {
          config->threshold_lux = value;
        }
      else if (sscanf(line, "LEVELS=%d", &value) == 1 &&
               value >= 3 && value <= 10)
        {
          config->levels = value;
        }
    }

  fclose(fp);
  return 0;
}

static int read_light_sensor(int *light_lux)
{
  FILE *fp;
  char buffer[1024];
  char *pos;
  char *live_pos;
  char *stale_pos;
  char *offline_pos;
  int light_x10 = -1;
  unsigned long live_mask;
  unsigned long stale_mask;
  unsigned long offline_mask;
  size_t nread;

  fp = fopen(LAN_PANEL_STATUS, "r");
  if (fp == NULL)
    {
      return -errno;
    }

  nread = fread(buffer, 1, sizeof(buffer) - 1, fp);
  if (nread == 0)
    {
      fclose(fp);
      return -ENODATA;
    }

  fclose(fp);
  buffer[nread] = '\0';

  pos = strstr(buffer, "\"light_x10\":");
  live_pos = strstr(buffer, "\"live_mask\":");
  stale_pos = strstr(buffer, "\"stale_mask\":");
  offline_pos = strstr(buffer, "\"offline_mask\":");
  if (pos == NULL || live_pos == NULL || stale_pos == NULL ||
      offline_pos == NULL ||
      sscanf(pos + 12, "%d", &light_x10) != 1 || light_x10 < 0 ||
      sscanf(live_pos + 12, "%lu", &live_mask) != 1 ||
      sscanf(stale_pos + 13, "%lu", &stale_mask) != 1 ||
      sscanf(offline_pos + 15, "%lu", &offline_mask) != 1)
    {
      return -EINVAL;
    }

  if ((live_mask & ROUTINE_LIGHT_MASK) == 0 ||
      (stale_mask & ROUTINE_LIGHT_MASK) != 0 ||
      (offline_mask & ROUTINE_LIGHT_MASK) != 0)
    {
      return -EAGAIN;
    }

  *light_lux = light_x10 / 10;
  return 0;
}

static int calculate_pwm_duty(int light_lux, const struct light_config_s *config)
{
  int level;
  int max_lux = config->threshold_lux * config->levels;

  if (light_lux >= max_lux)
    {
      return 0;
    }

  level = config->levels - (light_lux / config->threshold_lux);
  if (level <= 0)
    {
      return 0;
    }
  if (level > config->levels)
    {
      level = config->levels;
    }

  return 20 + ((level - 1) * 75 / (config->levels - 1));
}

static int light_gpio_open(void)
{
  int fd;
  int ret;

  fd = open(LED_GPIO_DEVICE, O_RDWR);
  if (fd < 0)
    {
      return -errno;
    }

  ret = ioctl(fd, GPIOC_SETPINTYPE, (unsigned long)GPIO_OUTPUT_PIN);
  if (ret < 0)
    {
      ret = -errno;
      close(fd);
      return ret;
    }

  ret = ioctl(fd, GPIOC_WRITE, (unsigned long)LED_OFF_VALUE);
  if (ret < 0)
    {
      ret = -errno;
      close(fd);
      return ret;
    }

  return fd;
}

static int light_gpio_write(int fd, int value)
{
  int ret;

  ret = ioctl(fd, GPIOC_WRITE, (unsigned long)value);
  return ret < 0 ? -errno : 0;
}

static int light_gpio_read(int fd, bool *value)
{
  int ret;

  ret = ioctl(fd, GPIOC_READ, (unsigned long)((uintptr_t)value));
  return ret < 0 ? -errno : 0;
}

static int light_manual_test(const char *state)
{
  bool readback;
  int fd;
  int value;
  int ret;

  if (strcmp(state, "on") == 0)
    {
      value = LED_ON_VALUE;
    }
  else if (strcmp(state, "off") == 0)
    {
      value = LED_OFF_VALUE;
    }
  else
    {
      printf("Usage: lan_panel --light <on|off>\n");
      return -EINVAL;
    }

  fd = light_gpio_open();
  if (fd < 0)
    {
      printf("lan_panel: cannot open %s: %d\n", LED_GPIO_DEVICE, -fd);
      return fd;
    }

  ret = light_gpio_write(fd, value);
  if (ret == 0)
    {
      ret = light_gpio_read(fd, &readback);
    }

  if (ret == 0 && readback != (bool)value)
    {
      printf("lan_panel: PD22 write/read mismatch: requested=%s read=%s\n",
             value == LED_ON_VALUE ? "LOW" : "HIGH",
             readback ? "HIGH" : "LOW");
      ret = -EIO;
    }

  if (ret < 0)
    {
      printf("lan_panel: control %s failed: %d\n", LED_GPIO_DEVICE, -ret);
    }
  else
    {
      printf("lan_panel: light %s on %s, PD22 readback=%s\n",
             state, LED_GPIO_DEVICE, readback ? "HIGH" : "LOW");
    }

  close(fd);
  return ret;
}

static void *light_control_thread(void *arg)
{
  struct light_config_s config;
  int gpio_fd;
  int light_lux;
  int duty_percent;
  int last_duty = -1;
  int cycle_us = 1000000 / SOFT_PWM_FREQUENCY;

  (void)arg;

  gpio_fd = light_gpio_open();
  if (gpio_fd < 0)
    {
      printf("lan_panel: light control: cannot open %s: %d\n",
             LED_GPIO_DEVICE, -gpio_fd);
      return NULL;
    }

  printf("lan_panel: PD22 light control started on %s (active low)\n",
         LED_GPIO_DEVICE);

  while (!g_stop)
    {
      load_light_config(&config);

      if (read_light_sensor(&light_lux) == 0)
        {
          duty_percent = calculate_pwm_duty(light_lux, &config);

          /* PD22 sinks current: GPIO LOW lights the LED, GPIO HIGH turns it
           * off.  duty_percent is therefore the LOW-time percentage.
           */

          if (duty_percent != last_duty)
            {
              printf("lan_panel: light=%d lux, level=%d/%d, low-duty=%d%%\n",
                     light_lux,
                     duty_percent == 0 ? 0 :
                       1 + ((duty_percent - 20) * (config.levels - 1) / 75),
                     config.levels, duty_percent);
              last_duty = duty_percent;
            }

          /* Software PWM: toggle GPIO based on duty cycle */
          if (duty_percent >= 100)
            {
              /* Always on (LED bright) - GPIO LOW */
              light_gpio_write(gpio_fd, LED_ON_VALUE);
              usleep(POLL_INTERVAL_MS * 1000);
            }
          else if (duty_percent <= 0)
            {
              /* Always off (LED dark) - GPIO HIGH */
              light_gpio_write(gpio_fd, LED_OFF_VALUE);
              usleep(POLL_INTERVAL_MS * 1000);
            }
          else
            {
              /* PWM: repeat cycles for POLL_INTERVAL_MS */
              int total_cycles = (POLL_INTERVAL_MS * 1000) / cycle_us;
              int on_time_us = (cycle_us * duty_percent) / 100;
              int off_time_us = cycle_us - on_time_us;

              for (int i = 0; i < total_cycles && !g_stop; i++)
                {
                  if (on_time_us > 0)
                    {
                      light_gpio_write(gpio_fd, LED_ON_VALUE);
                      usleep(on_time_us);
                    }
                  if (off_time_us > 0)
                    {
                      light_gpio_write(gpio_fd, LED_OFF_VALUE);
                      usleep(off_time_us);
                    }
                }
            }
        }
      else
        {
          if (last_duty != 0)
            {
              light_gpio_write(gpio_fd, LED_OFF_VALUE);
              printf("lan_panel: light sensor not live, PD22 LED off\n");
              last_duty = 0;
            }

          usleep(POLL_INTERVAL_MS * 1000);
        }
    }

  /* Turn off LED on exit */
  light_gpio_write(gpio_fd, LED_OFF_VALUE);
  close(gpio_fd);

  printf("lan_panel: light control stopped\n");
  return NULL;
}

#endif /* CONFIG_DEV_GPIO */

int main(int argc, FAR char *argv[])
{
  struct sockaddr_in address;
  struct sockaddr_in client;
  socklen_t client_length;
  char token[LAN_PANEL_TOKEN_MAX];
  int listensd;
  int acceptsd;
  int opt = 1;
  int ret;
  pthread_attr_t worker_attr;
  bool worker_attr_ready = false;
#ifdef CONFIG_DEV_GPIO
  pthread_t light_thread;
  bool light_thread_started = false;
#endif

  (void)argc;
  (void)argv;

#ifdef CONFIG_DEV_GPIO
  if (argc == 3 && strcmp(argv[1], "--light") == 0)
    {
      return light_manual_test(argv[2]) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }
#endif

  signal(SIGINT, lan_panel_signal);
  signal(SIGTERM, lan_panel_signal);

  ret = lan_auth_load(token, sizeof(token));
  if (ret < 0)
    {
      printf("lan_panel: authentication storage unavailable: %d\n", ret);
      return EXIT_FAILURE;
    }
  printf("lan_panel: four-digit access token is ready\n");

  listensd = socket(AF_INET, SOCK_STREAM, 0);
  if (listensd < 0)
    {
      return EXIT_FAILURE;
    }
  setsockopt(listensd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(CONFIG_CONTEST2026_113_LAN_PANEL_PORT);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  /* A backlog of 1 makes the browser's parallel connections (favicon, the
   * status poll, a second tab) hit a full queue and fail to connect, which is
   * what shows up as the page loading only sometimes.
   */

  if (bind(listensd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
      listen(listensd, LAN_PANEL_BACKLOG) < 0)
    {
      close(listensd);
      return EXIT_FAILURE;
    }

  printf("lan_panel: listening on port %u\n",
         CONFIG_CONTEST2026_113_LAN_PANEL_PORT);

  ret = pthread_attr_init(&worker_attr);
  if (ret == 0)
    {
      worker_attr_ready = true;
      ret = pthread_attr_setstacksize(&worker_attr,
                                      LAN_PANEL_WORKER_STACKSIZE);
    }
  if (ret == 0)
    {
      ret = pthread_attr_setdetachstate(&worker_attr,
                                        PTHREAD_CREATE_DETACHED);
    }
  if (ret != 0)
    {
      printf("lan_panel: worker setup failed: %d\n", ret);
      if (worker_attr_ready)
        {
          pthread_attr_destroy(&worker_attr);
        }
      close(listensd);
      return EXIT_FAILURE;
    }

#ifdef CONFIG_DEV_GPIO
  /* Start light control thread */
  pthread_attr_t light_attr;
  pthread_attr_init(&light_attr);
  pthread_attr_setstacksize(&light_attr, 4096);
  ret = pthread_create(&light_thread, &light_attr, light_control_thread, NULL);
  if (ret == 0)
    {
      light_thread_started = true;
    }
  else
    {
      printf("lan_panel: failed to start light control thread: %d\n", ret);
    }
  pthread_attr_destroy(&light_attr);
#endif

  while (!g_stop)
    {
      /* Reserve a worker before accepting the socket.  If all workers are
       * busy (for example while an SD-card export is being copied), waiting
       * here leaves new clients in the listen backlog instead of starting
       * their receive timeout while the main thread is blocked.  This keeps
       * browser requests from expiring before a worker can read them.
       */

      ret = lan_panel_worker_slot_acquire();
      if (ret < 0)
        {
          break;
        }

      client_length = sizeof(client);
      acceptsd = accept(listensd, (struct sockaddr *)&client, &client_length);
      if (acceptsd < 0)
        {
          lan_panel_worker_slot_release();
          if (errno == EINTR)
            {
              continue;
            }
          break;
        }

      if (lan_panel_set_timeout(acceptsd) < 0)
        {
          lan_http_close(acceptsd);
          lan_panel_worker_slot_release();
          continue;
        }

      {
        struct lan_panel_worker_s *worker = malloc(sizeof(*worker));
        pthread_t worker_thread;

        if (worker == NULL)
          {
            lan_http_send_response(acceptsd, 503, "text/plain",
                                   "Server busy\n", 12);
            lan_http_close(acceptsd);
            lan_panel_worker_slot_release();
            continue;
          }

        worker->fd = acceptsd;
        ret = pthread_create(&worker_thread, &worker_attr,
                             lan_panel_connection_worker, worker);
        if (ret != 0)
          {
            free(worker);
            lan_http_send_response(acceptsd, 503, "text/plain",
                                   "Server busy\n", 12);
            lan_http_close(acceptsd);
            lan_panel_worker_slot_release();
          }
        }
    }

  g_stop = 1;
  close(listensd);
  if (worker_attr_ready)
    {
      pthread_attr_destroy(&worker_attr);
    }
  lan_panel_wait_for_workers();
#ifdef CONFIG_DEV_GPIO
  if (light_thread_started)
    {
      pthread_join(light_thread, NULL);
    }
#endif
  memset(token, 0, sizeof(token));
  return EXIT_SUCCESS;
}
