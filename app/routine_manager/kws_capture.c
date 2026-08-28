/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/kws_capture.c
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/audio/audio.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <time.h>
#include <unistd.h>

#include <system/nxplayer.h>
#include <system/nxrecorder.h>

#include "kws_capture.h"

#define KWS_CAPTURE_CHANNELS           1
#define KWS_CAPTURE_BITS_PER_SAMPLE    16
#define KWS_CAPTURE_SAMPLE_RATE        16000
#define KWS_CAPTURE_CHANNEL_MAP        1
#define KWS_CAPTURE_FRAME_SAMPLES      320
#define KWS_CAPTURE_MIN_RMS            350
#define KWS_CAPTURE_MIN_PEAK           900
#define KWS_CAPTURE_POLL_MS            20
#define KWS_CAPTURE_DURATION_TOLERANCE 1000
#define KWS_CAPTURE_MIN_FREE_BYTES     (1024 * 1024)
#define KWS_CAPTURE_MOUNT_WAIT_MS      5000
#define KWS_CAPTURE_MOUNT_POLL_MS      100

struct kws_capture_metrics_s
{
  uint64_t sum_squares;
  uint64_t samples;
  uint32_t active_frames;
  uint32_t frames;
  uint32_t peak;
  uint32_t rms;
  uint64_t bytes;
};

static const char *const g_kws_capture_labels[] =
{
  "query_temperature",
  "query_humidity",
  "query_light",
  "query_air_quality",
  "unknown_speech",
  "background",
  "wake_nihao_vela"
};

static volatile sig_atomic_t g_kws_capture_cancelled;

static void kws_capture_signal_handler(int signo)
{
  g_kws_capture_cancelled = 1;
}

static int kws_capture_label_index(FAR const char *label)
{
  size_t i;

  if (label == NULL)
    {
      return -EINVAL;
    }

  for (i = 0; i < sizeof(g_kws_capture_labels) /
                  sizeof(g_kws_capture_labels[0]); i++)
    {
      if (strcmp(label, g_kws_capture_labels[i]) == 0)
        {
          return i;
        }
    }

  return -EINVAL;
}

static bool kws_capture_label_valid(FAR const char *label)
{
  return kws_capture_label_index(label) >= 0;
}

static uint64_t kws_capture_hash_token(uint64_t hash, FAR const char *token)
{
  while (*token != '\0')
    {
      hash ^= (uint8_t)*token++;
      hash *= UINT64_C(1099511628211);
    }

  hash ^= 0xff;
  hash *= UINT64_C(1099511628211);
  return hash;
}

static int kws_capture_sample_id(
  FAR const struct kws_capture_options_s *options, uint16_t take,
  FAR char *sample_id, size_t size)
{
  static const char *const label_codes[] =
  {
    "qt", "qh", "ql", "qa", "us", "bg", "wv"
  };
  uint64_t hash = UINT64_C(14695981039346656037);
  int label_index;
  int length;

  label_index = kws_capture_label_index(options->label);
  if (label_index < 0)
    {
      return label_index;
    }

  hash = kws_capture_hash_token(hash, options->device_id);
  hash = kws_capture_hash_token(hash, options->speaker_id);
  hash = kws_capture_hash_token(hash, options->session_id);
  length = snprintf(sample_id, size, "k%016llx_%s_%04u",
                    (unsigned long long)hash, label_codes[label_index], take);
  if (length < 0 || (size_t)length >= size ||
      (size_t)length + sizeof(".json") - 1 > CONFIG_NAME_MAX)
    {
      return -ENAMETOOLONG;
    }

  return OK;
}

static bool kws_capture_id_valid(FAR const char *id)
{
  size_t length;
  size_t i;

  if (id == NULL)
    {
      return false;
    }

  length = strlen(id);
  if (length == 0 || length > KWS_CAPTURE_ID_MAX ||
      strstr(id, "..") != NULL)
    {
      return false;
    }

  for (i = 0; i < length; i++)
    {
      if (!isalnum((unsigned char)id[i]) && id[i] != '-' && id[i] != '_')
        {
          return false;
        }
    }

  return true;
}

static bool kws_capture_path_under_mount(FAR const char *path,
                                         FAR const char *mountpoint)
{
  size_t path_length;
  size_t mount_length;

  if (path == NULL || mountpoint == NULL || mountpoint[0] != '/')
    {
      return false;
    }

  path_length = strlen(path);
  mount_length = strlen(mountpoint);
  while (mount_length > 1 && mountpoint[mount_length - 1] == '/')
    {
      mount_length--;
    }

  return path_length >= mount_length &&
         strncmp(path, mountpoint, mount_length) == 0 &&
         (path_length == mount_length || path[mount_length] == '/');
}

int kws_capture_validate_options(
  FAR const struct kws_capture_options_s *options)
{
  if (options == NULL || !kws_capture_label_valid(options->label) ||
      !kws_capture_id_valid(options->speaker_id) ||
      !kws_capture_id_valid(options->session_id) ||
      !kws_capture_id_valid(options->device_id) ||
      options->capture_device == NULL || options->capture_device[0] != '/' ||
      options->playback_device == NULL ||
      options->playback_device[0] != '/' ||
      options->output_root == NULL || options->output_root[0] != '/' ||
      options->storage_mountpoint == NULL ||
      options->storage_mountpoint[0] != '/' ||
      !kws_capture_path_under_mount(options->output_root,
                                     options->storage_mountpoint) ||
      options->playback_volume < 1 || options->playback_volume > 100 ||
      options->count == 0 || options->count > 1000 ||
      options->record_ms < 1000 || options->record_ms > 10000)
    {
      return -EINVAL;
    }

  return OK;
}

static int kws_capture_make_directory(FAR const char *path)
{
  struct stat info;

  if (mkdir(path, 0700) == 0)
    {
      return OK;
    }

  if (errno != EEXIST || stat(path, &info) < 0 ||
      !S_ISDIR(info.st_mode))
    {
      return -errno;
    }

  return OK;
}

static int kws_capture_make_directories(FAR const char *path)
{
  char current[PATH_MAX];
  size_t length;
  size_t i;
  int ret;

  length = strlen(path);
  if (length == 0 || length >= sizeof(current))
    {
      return -ENAMETOOLONG;
    }

  memcpy(current, path, length + 1);
  for (i = 1; i <= length; i++)
    {
      if (current[i] != '/' && current[i] != '\0')
        {
          continue;
        }

      current[i] = '\0';
      ret = kws_capture_make_directory(current);
      if (ret < 0)
        {
          return ret;
        }

      current[i] = path[i];
    }

  return OK;
}

static int kws_capture_wait_for_storage(
  FAR const struct kws_capture_options_s *options)
{
  struct stat info;
  struct statfs storage;
  uint32_t elapsed;
  int last_error = -ENODEV;

  for (elapsed = 0; elapsed <= KWS_CAPTURE_MOUNT_WAIT_MS;
       elapsed += KWS_CAPTURE_MOUNT_POLL_MS)
    {
      if (stat(options->storage_mountpoint, &info) == 0 &&
          statfs(options->storage_mountpoint, &storage) == 0)
        {
          /*
           * R528 may report a mounted FAT volume through either the
           * generic FATFS VFS type or the legacy MSDOS type.  Both are
           * valid FAT/vfat storage for this capture path.
           */
          if (storage.f_type != FATFS_SUPER_MAGIC &&
              storage.f_type != MSDOS_SUPER_MAGIC)
            {
              fprintf(stderr,
                      "kws capture: %s is not a FAT/vfat mount (type=0x%08lx)\n",
                      options->storage_mountpoint,
                      (unsigned long)storage.f_type);
              return -EINVAL;
            }

          printf("kws capture: SD mount ready at %s\n",
                 options->storage_mountpoint);
          return OK;
        }

      last_error = -errno;
      if (elapsed < KWS_CAPTURE_MOUNT_WAIT_MS)
        {
          usleep(KWS_CAPTURE_MOUNT_POLL_MS * 1000);
        }
    }

  fprintf(stderr,
          "kws capture: SD mount %s is not ready after %u ms; "
          "insert a FAT32 card and wait for /dev/mmcsd1\n",
          options->storage_mountpoint, KWS_CAPTURE_MOUNT_WAIT_MS);
  return last_error < 0 ? last_error : -ENODEV;
}

static int kws_capture_check_storage(FAR const char *path,
                                     uint32_t record_ms,
                                     uint16_t count)
{
  struct statfs storage;
  uint64_t required;
  uint64_t available;

  if (statfs(path, &storage) < 0)
    {
      int ret = -errno;

      fprintf(stderr, "kws capture: statfs %s failed: %d\n", path, ret);
      return ret;
    }

  required = (uint64_t)KWS_CAPTURE_SAMPLE_RATE * sizeof(int16_t) *
             record_ms * count / 1000 + KWS_CAPTURE_MIN_FREE_BYTES;
  available = (uint64_t)storage.f_bavail * storage.f_bsize;
  printf("kws capture: storage type=0x%08lx available=%llu required=%llu\n",
         (unsigned long)storage.f_type, (unsigned long long)available,
         (unsigned long long)required);
  fflush(stdout);
  if (available < required)
    {
      fprintf(stderr,
              "kws capture: insufficient storage: available=%llu required=%llu\n",
              (unsigned long long)available,
              (unsigned long long)required);
      return -ENOSPC;
    }

  return OK;
}

static uint32_t kws_capture_isqrt(uint64_t value)
{
  uint64_t result = 0;
  uint64_t bit = (uint64_t)1 << 62;

  while (bit > value)
    {
      bit >>= 2;
    }

  while (bit != 0)
    {
      if (value >= result + bit)
        {
          value -= result + bit;
          result = (result >> 1) + bit;
        }
      else
        {
          result >>= 1;
        }

      bit >>= 2;
    }

  return (uint32_t)result;
}

static int kws_capture_measure(FAR const char *path,
                               FAR struct kws_capture_metrics_s *metrics)
{
  int16_t samples[KWS_CAPTURE_FRAME_SAMPLES];
  ssize_t nread;
  int fd;

  memset(metrics, 0, sizeof(*metrics));
  fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    {
      return -errno;
    }

  while ((nread = read(fd, samples, sizeof(samples))) > 0)
    {
      size_t count;
      size_t i;
      uint64_t frame_energy = 0;
      uint32_t frame_peak = 0;

      if ((nread & 1) != 0)
        {
          close(fd);
          return -EINVAL;
        }

      count = nread / sizeof(samples[0]);
      for (i = 0; i < count; i++)
        {
          int32_t sample = samples[i];
          uint32_t magnitude = sample < 0 ? -sample : sample;

          if (magnitude > frame_peak)
            {
              frame_peak = magnitude;
            }

          frame_energy += (uint64_t)(sample * sample);
        }

      if (frame_peak > metrics->peak)
        {
          metrics->peak = frame_peak;
        }

      if (frame_peak >= KWS_CAPTURE_MIN_PEAK &&
          frame_energy / count >= KWS_CAPTURE_MIN_RMS * KWS_CAPTURE_MIN_RMS)
        {
          metrics->active_frames++;
        }

      metrics->sum_squares += frame_energy;
      metrics->samples += count;
      metrics->frames++;
      metrics->bytes += nread;
    }

  if (nread < 0)
    {
      int ret = -errno;
      close(fd);
      return ret;
    }

  close(fd);
  if (metrics->samples == 0)
    {
      return -ENODATA;
    }

  metrics->rms = kws_capture_isqrt(metrics->sum_squares / metrics->samples);
  return OK;
}

static int kws_capture_record(FAR const char *device,
                              FAR const char *path,
                              uint32_t record_ms)
{
  FAR struct nxrecorder_s *recorder;
  struct timespec delay;
  uint32_t elapsed;
  int ret;

  recorder = nxrecorder_create();
  if (recorder == NULL)
    {
      return -ENOMEM;
    }

  ret = nxrecorder_setdevice(recorder, device);
  if (ret < 0)
    {
      nxrecorder_release(recorder);
      return ret;
    }

  ret = nxrecorder_recordinternal(recorder, path, AUDIO_FMT_PCM,
                                  KWS_CAPTURE_CHANNELS,
                                  KWS_CAPTURE_BITS_PER_SAMPLE,
                                  KWS_CAPTURE_SAMPLE_RATE,
                                  KWS_CAPTURE_CHANNEL_MAP);
  if (ret < 0)
    {
      nxrecorder_release(recorder);
      return ret;
    }

  delay.tv_sec = 0;
  delay.tv_nsec = KWS_CAPTURE_POLL_MS * 1000000;
  for (elapsed = 0; elapsed < record_ms && !g_kws_capture_cancelled;
       elapsed += KWS_CAPTURE_POLL_MS)
    {
      nanosleep(&delay, NULL);
    }

  ret = nxrecorder_stop(recorder);
  nxrecorder_release(recorder);
  if (g_kws_capture_cancelled)
    {
      return -ECANCELED;
    }

  return ret;
}

static int kws_capture_playback(
  FAR const struct kws_capture_options_s *options, FAR const char *path,
  uint64_t duration_ms)
{
  struct nxplayer_status_s status =
  {
    .state = NXPLAYER_STATE_STARTING,
    .result = -EINPROGRESS
  };
  FAR struct nxplayer_s *player;
  struct timespec delay;
  uint64_t elapsed;
  int ret;

  player = nxplayer_create();
  if (player == NULL)
    {
      return -ENOMEM;
    }

  ret = nxplayer_setdevice(player, options->playback_device);
  if (ret < 0)
    {
      nxplayer_release(player);
      return ret;
    }

#ifndef CONFIG_AUDIO_EXCLUDE_VOLUME
  ret = nxplayer_setvolume(player, options->playback_volume * 10);
  if (ret < 0)
    {
      nxplayer_release(player);
      return ret;
    }
#endif

  ret = nxplayer_playraw(player, path, AUDIO_FMT_PCM, AUDIO_FMT_UNDEF,
                         KWS_CAPTURE_CHANNELS,
                         KWS_CAPTURE_BITS_PER_SAMPLE,
                         KWS_CAPTURE_SAMPLE_RATE,
                         KWS_CAPTURE_CHANNEL_MAP);
  if (ret < 0)
    {
      nxplayer_release(player);
      return ret;
    }

  delay.tv_sec = 0;
  delay.tv_nsec = KWS_CAPTURE_POLL_MS * 1000000;
  for (elapsed = 0; elapsed <= duration_ms + 1000;
       elapsed += KWS_CAPTURE_POLL_MS)
    {
      if (g_kws_capture_cancelled)
        {
          nxplayer_stop(player);
          break;
        }

      ret = nxplayer_getstatus(player, &status);
      if (ret < 0 || status.state == NXPLAYER_STATE_IDLE)
        {
          break;
        }

      nanosleep(&delay, NULL);
    }

  if (status.state != NXPLAYER_STATE_IDLE && !g_kws_capture_cancelled)
    {
      nxplayer_stop(player);
      ret = -ETIMEDOUT;
    }
  else if (g_kws_capture_cancelled)
    {
      ret = -ECANCELED;
    }
  else if (ret >= 0)
    {
      ret = nxplayer_wait(player);
      if (ret >= 0)
        {
          ret = status.result;
        }
    }

  nxplayer_release(player);
  return ret;
}

static int kws_capture_write_metadata(
  FAR const char *path, FAR const struct kws_capture_options_s *options,
  FAR const char *sample_id, uint16_t take,
  FAR const struct kws_capture_metrics_s *metrics)
{
  struct tm utc;
  char timestamp[32];
  time_t now;
  bool clock_valid;
  int fd;
  int length;
  int ret = OK;
  char json[1024];

  now = time(NULL);
  clock_valid = now >= 1577836800 && gmtime_r(&now, &utc) != NULL &&
                strftime(timestamp, sizeof(timestamp),
                         "%Y-%m-%dT%H:%M:%SZ", &utc) > 0;
  if (!clock_valid)
    {
      strcpy(timestamp, "null");
    }

  length = snprintf(json, sizeof(json),
    "{\n"
    "  \"schema_version\": 1,\n"
    "  \"sample_id\": \"%s\",\n"
    "  \"device_id\": \"%s\",\n"
    "  \"speaker_id\": \"%s\",\n"
    "  \"session_id\": \"%s\",\n"
    "  \"label\": \"%s\",\n"
    "  \"take\": %u,\n"
    "  \"capture_device\": \"%s\",\n"
    "  \"encoding\": \"pcm_s16le\",\n"
    "  \"sample_rate_hz\": %u,\n"
    "  \"channels\": %u,\n"
    "  \"bits_per_sample\": %u,\n"
    "  \"requested_ms\": %lu,\n"
    "  \"actual_bytes\": %llu,\n"
    "  \"frames\": %llu,\n"
    "  \"duration_ms\": %llu,\n"
    "  \"peak\": %lu,\n"
    "  \"rms\": %lu,\n"
    "  \"active_frames\": %lu,\n"
    "  \"analysis_frames\": %lu,\n"
    "  \"clock_valid\": %s,\n"
    "  \"captured_at_utc\": %s%s%s,\n"
    "  \"status\": \"complete\"\n"
    "}\n",
    sample_id, options->device_id, options->speaker_id,
    options->session_id, options->label, take, options->capture_device,
    KWS_CAPTURE_SAMPLE_RATE, KWS_CAPTURE_CHANNELS,
    KWS_CAPTURE_BITS_PER_SAMPLE, (unsigned long)options->record_ms,
    (unsigned long long)metrics->bytes,
    (unsigned long long)metrics->samples,
    (unsigned long long)(metrics->samples * 1000 /
                         KWS_CAPTURE_SAMPLE_RATE),
    (unsigned long)metrics->peak, (unsigned long)metrics->rms,
    (unsigned long)metrics->active_frames, (unsigned long)metrics->frames,
    clock_valid ? "true" : "false", clock_valid ? "\"" : "",
    timestamp, clock_valid ? "\"" : "");
  if (length < 0 || (size_t)length >= sizeof(json))
    {
      return -EOVERFLOW;
    }

  fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0)
    {
      return -errno;
    }

  if (write(fd, json, length) != length || fsync(fd) < 0)
    {
      ret = -errno;
    }

  if (close(fd) < 0 && ret == OK)
    {
      ret = -errno;
    }

  if (ret < 0)
    {
      unlink(path);
    }

  return ret;
}

static int kws_capture_reserve_part(FAR const char *path)
{
  int fd;

  fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0)
    {
      return -errno;
    }

  if (close(fd) < 0)
    {
      int ret = -errno;
      unlink(path);
      return ret;
    }

  return OK;
}

static int kws_capture_publish_pair(FAR const char *pcm_part,
                                    FAR const char *json_part,
                                    FAR const char *pcm_path,
                                    FAR const char *json_path)
{
  int ret;

  if (access(pcm_path, F_OK) == 0 || access(json_path, F_OK) == 0)
    {
      return -EEXIST;
    }

  if (rename(pcm_part, pcm_path) < 0)
    {
      ret = -errno;
      fprintf(stderr, "kws capture: publish PCM failed: %d\n", ret);
      return ret;
    }

  if (rename(json_part, json_path) < 0)
    {
      ret = -errno;
      fprintf(stderr, "kws capture: publish JSON failed: %d\n", ret);
      if (rename(pcm_path, pcm_part) < 0)
        {
          fprintf(stderr,
                  "kws capture: PCM rollback failed; remove %s manually\n",
                  pcm_path);
        }

      return ret;
    }

  return OK;
}

static int kws_capture_read_choice(FAR const char *prompt)
{
  int choice;
  int extra;

  printf("%s", prompt);
  fflush(stdout);
  choice = getchar();
  if (choice == EOF)
    {
      return g_kws_capture_cancelled ? 'q' : -EIO;
    }

  while (choice != '\n' && (extra = getchar()) != '\n' && extra != EOF)
    {
    }

  if (choice == '\n')
    {
      return 'k';
    }

  return tolower(choice);
}

static int kws_capture_one(FAR const struct kws_capture_options_s *options,
                           FAR const char *directory, uint16_t take)
{
  struct kws_capture_metrics_s metrics;
  char json_part[PATH_MAX];
  char pcm_part[PATH_MAX];
  char json_path[PATH_MAX];
  char pcm_path[PATH_MAX];
  char sample_id[160];
  int choice;
  int ret;

  ret = kws_capture_sample_id(options, take, sample_id, sizeof(sample_id));
  if (ret < 0)
    {
      return ret;
    }

  if (snprintf(pcm_path, sizeof(pcm_path), "%s/%s.pcm", directory,
               sample_id) >= sizeof(pcm_path) ||
      snprintf(json_path, sizeof(json_path), "%s/%s.json", directory,
               sample_id) >= sizeof(json_path) ||
      snprintf(pcm_part, sizeof(pcm_part), "%s/.p%lx.%04u",
               directory, (unsigned long)getpid(), take) >= sizeof(pcm_part) ||
      snprintf(json_part, sizeof(json_part), "%s/.j%lx.%04u",
               directory, (unsigned long)getpid(), take) >= sizeof(json_part))
    {
      return -ENAMETOOLONG;
    }

  if (access(pcm_path, F_OK) == 0 || access(json_path, F_OK) == 0)
    {
      return -EEXIST;
    }

  choice = kws_capture_read_choice(
    "按 Enter 开始录音，输入 q 结束本批次：");
  if (choice == 'q' || g_kws_capture_cancelled)
    {
      return -ECANCELED;
    }
  else if (choice < 0)
    {
      return choice;
    }

  printf("3\n");
  sleep(1);
  printf("2\n");
  sleep(1);
  printf("1\n");
  sleep(1);
  if (g_kws_capture_cancelled)
    {
      return -ECANCELED;
    }

  ret = kws_capture_reserve_part(pcm_part);
  if (ret < 0)
    {
      return ret;
    }

  printf("正在录制 %s ...\n", sample_id);
  ret = kws_capture_record(options->capture_device, pcm_part,
                           options->record_ms);
  if (ret < 0)
    {
      unlink(pcm_part);
      return ret;
    }

  ret = kws_capture_measure(pcm_part, &metrics);
  if (ret < 0)
    {
      unlink(pcm_part);
      return ret;
    }

  if (metrics.samples * 1000 <
        (uint64_t)(options->record_ms - KWS_CAPTURE_DURATION_TOLERANCE) *
        KWS_CAPTURE_SAMPLE_RATE ||
      metrics.samples * 1000 >
        ((uint64_t)options->record_ms + KWS_CAPTURE_DURATION_TOLERANCE) *
        KWS_CAPTURE_SAMPLE_RATE)
    {
      printf("录音长度超出容差：%llu ms\n",
             (unsigned long long)(metrics.samples * 1000 /
                                  KWS_CAPTURE_SAMPLE_RATE));
      unlink(pcm_part);
      return -EIO;
    }

  printf("bytes=%llu duration=%llu ms peak=%lu rms=%lu active=%lu/%lu\n",
         (unsigned long long)metrics.bytes,
         (unsigned long long)(metrics.samples * 1000 /
                              KWS_CAPTURE_SAMPLE_RATE),
         (unsigned long)metrics.peak, (unsigned long)metrics.rms,
         (unsigned long)metrics.active_frames, (unsigned long)metrics.frames);
  printf("正在回放本次录音，请试听后选择。\n");
  fflush(stdout);
  ret = kws_capture_playback(options, pcm_part,
                             metrics.samples * 1000 /
                             KWS_CAPTURE_SAMPLE_RATE);
  if (ret < 0)
    {
      fprintf(stderr, "kws capture: playback failed: %d\n", ret);
      unlink(pcm_part);
      return ret;
    }

  choice = kws_capture_read_choice(
    "Enter/k 保留，r 重录，q 丢弃并退出：");
  if (choice == 'r')
    {
      unlink(pcm_part);
      return -EAGAIN;
    }
  else if (choice == 'q' || g_kws_capture_cancelled)
    {
      unlink(pcm_part);
      return -ECANCELED;
    }
  else if (choice != 'k')
    {
      printf("无效选择，当前样本未保存。\n");
      unlink(pcm_part);
      return -EAGAIN;
    }

  ret = kws_capture_write_metadata(json_part, options, sample_id, take,
                                   &metrics);
  if (ret < 0)
    {
      unlink(pcm_part);
      return ret;
    }

  ret = kws_capture_publish_pair(pcm_part, json_part, pcm_path, json_path);
  if (ret < 0)
    {
      unlink(pcm_part);
      unlink(json_part);
      return ret;
    }

  printf("已保存：%s.pcm + .json\n", sample_id);
  return OK;
}

static uint16_t kws_capture_first_take(FAR const struct kws_capture_options_s *options,
                                       FAR const char *directory)
{
  char sample_id[32];
  char path[PATH_MAX];
  uint16_t take;

  for (take = 1; take < UINT16_MAX; take++)
    {
      int length;

      if (kws_capture_sample_id(options, take, sample_id,
                                sizeof(sample_id)) < 0)
        {
          return 0;
        }

      length = snprintf(path, sizeof(path), "%s/%s.pcm", directory,
                        sample_id);
      if (length < 0 || (size_t)length >= sizeof(path))
        {
          fprintf(stderr, "kws capture: sample path exceeds PATH_MAX=%u\n",
                  (unsigned int)PATH_MAX);
          return 0;
        }

      if (access(path, F_OK) < 0 && errno == ENOENT)
        {
          return take;
        }
    }

  return 0;
}

int kws_capture_run(FAR const struct kws_capture_options_s *options)
{
  struct sigaction action;
  struct sigaction old_int;
  struct sigaction old_term;
  char directory[PATH_MAX];
  uint16_t completed = 0;
  uint16_t take;
  int ret;

  printf("kws capture: starting label=%s speaker=%s session=%s\n",
         options != NULL && options->label != NULL ? options->label : "(null)",
         options != NULL && options->speaker_id != NULL ?
           options->speaker_id : "(null)",
         options != NULL && options->session_id != NULL ?
           options->session_id : "(null)");
  fflush(stdout);

  ret = kws_capture_validate_options(options);
  if (ret < 0)
    {
      fprintf(stderr, "kws capture: invalid options: %d\n", ret);
      return ret;
    }

  ret = kws_capture_wait_for_storage(options);
  if (ret < 0)
    {
      return ret;
    }

  ret = kws_capture_make_directories(options->output_root);
  if (ret < 0)
    {
      fprintf(stderr, "kws capture: cannot prepare %s: %d\n",
              options->output_root, ret);
      return ret;
    }

  if (snprintf(directory, sizeof(directory), "%s/%s", options->output_root,
               options->session_id) >= sizeof(directory))
    {
      return -ENAMETOOLONG;
    }

  ret = kws_capture_make_directory(directory);
  if (ret < 0)
    {
      fprintf(stderr, "kws capture: cannot prepare %s: %d\n", directory, ret);
      return ret;
    }

  ret = kws_capture_check_storage(directory, options->record_ms,
                                  options->count);
  if (ret < 0)
    {
      return ret;
    }

  take = kws_capture_first_take(options, directory);
  if (take == 0)
    {
      fprintf(stderr, "kws capture: no unused take number in %s\n", directory);
      return -ENOSPC;
    }

  memset(&action, 0, sizeof(action));
  action.sa_handler = kws_capture_signal_handler;
  sigemptyset(&action.sa_mask);
  g_kws_capture_cancelled = 0;
  sigaction(SIGINT, &action, &old_int);
  sigaction(SIGTERM, &action, &old_term);

  printf("R528 中文 KWS 语料采集\n");
  printf("label=%s speaker=%s session=%s count=%u record_ms=%lu\n",
         options->label, options->speaker_id, options->session_id,
         options->count, (unsigned long)options->record_ms);
  printf("输出目录：%s\n", directory);

  while (completed < options->count && !g_kws_capture_cancelled)
    {
      ret = kws_capture_one(options, directory, take);
      if (ret == -EAGAIN)
        {
          continue;
        }
      else if (ret == -EEXIST)
        {
          take++;
          continue;
        }
      else if (ret == -ECANCELED)
        {
          break;
        }
      else if (ret < 0)
        {
          fprintf(stderr, "kws capture: sample %u failed: %d\n", take, ret);
          break;
        }

      completed++;
      take++;
    }

  sigaction(SIGINT, &old_int, NULL);
  sigaction(SIGTERM, &old_term, NULL);
  printf("采集结束：已保存 %u/%u 条。\n", completed, options->count);
  if (g_kws_capture_cancelled || ret == -ECANCELED)
    {
      return completed > 0 ? OK : -ECANCELED;
    }

  return ret < 0 ? ret : OK;
}

int kws_capture_selftest(void)
{
  struct kws_capture_options_s options =
  {
    .label = "query_temperature",
    .speaker_id = "s001",
    .session_id = "session_a",
    .device_id = "r528a",
    .capture_device = "/dev/audio/pcm1c",
    .playback_device = "/dev/audio/pcm0p",
    .output_root = "/sdcard/kws_corpus",
    .storage_mountpoint = "/sdcard",
    .record_ms = 3500,
    .playback_volume = 100,
    .count = 20
  };

  if (kws_capture_validate_options(&options) < 0 ||
      !kws_capture_label_valid("background") ||
      kws_capture_label_valid("unsupported") ||
      !kws_capture_id_valid("speaker-01") ||
      kws_capture_id_valid("../speaker") ||
      kws_capture_isqrt(0) != 0 || kws_capture_isqrt(14400) != 120)
    {
      return -EINVAL;
    }

  options.label = "temperature";
  if (kws_capture_validate_options(&options) >= 0)
    {
      return -EINVAL;
    }

  options.label = "query_temperature";
  options.record_ms = 999;
  if (kws_capture_validate_options(&options) >= 0)
    {
      return -EINVAL;
    }

  return OK;
}
