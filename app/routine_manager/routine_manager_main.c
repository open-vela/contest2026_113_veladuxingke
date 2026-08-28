/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/routine_manager_main.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#include "audio_service.h"
#include "environment_service.h"
#ifdef CONFIG_ROUTINE_MANAGER_KWS_CAPTURE
#  include "kws_capture.h"
#endif
#include "routine_model.h"
#include "routine_config.h"
#include "routine_storage.h"
#include "routine_ui.h"

#ifndef CONFIG_ROUTINE_MANAGER_FONT_PATH
#  define CONFIG_ROUTINE_MANAGER_FONT_PATH "/etc/fonts/font_puhui_20_4.bin"
#endif

#ifndef CONFIG_ROUTINE_MANAGER_DEMO_RATE
#  define CONFIG_ROUTINE_MANAGER_DEMO_RATE 5
#endif

#ifndef CONFIG_ROUTINE_MANAGER_UTC_OFFSET_MINUTES
#  define CONFIG_ROUTINE_MANAGER_UTC_OFFSET_MINUTES 480
#endif

#ifndef CONFIG_ROUTINE_MANAGER_I2C_PATH
#  define CONFIG_ROUTINE_MANAGER_I2C_PATH "/dev/i2c3"
#endif

#ifndef CONFIG_ROUTINE_MANAGER_VOICE_CAPTURE_DEVICE
#  define CONFIG_ROUTINE_MANAGER_VOICE_CAPTURE_DEVICE "/dev/audio/pcm1c"
#endif

#ifndef CONFIG_ROUTINE_MANAGER_VOICE_PLAYBACK_DEVICE
#  define CONFIG_ROUTINE_MANAGER_VOICE_PLAYBACK_DEVICE "/dev/audio/pcm0p"
#endif

#ifndef CONFIG_ROUTINE_MANAGER_VOICE_RECORD_MS
#  define CONFIG_ROUTINE_MANAGER_VOICE_RECORD_MS 3500
#endif

#ifndef CONFIG_ROUTINE_MANAGER_VOICE_VOLUME
#  define CONFIG_ROUTINE_MANAGER_VOICE_VOLUME 100
#endif

#ifndef CONFIG_ROUTINE_MANAGER_VOICE_ASSET_DIR
#  define CONFIG_ROUTINE_MANAGER_VOICE_ASSET_DIR "/etc/routine_voice"
#endif

#ifndef CONFIG_ROUTINE_MANAGER_KWS_CAPTURE_ROOT
#  define CONFIG_ROUTINE_MANAGER_KWS_CAPTURE_ROOT "/sdcard/kws_corpus"
#endif

#ifndef CONFIG_ROUTINE_MANAGER_KWS_STORAGE_MOUNT
#  define CONFIG_ROUTINE_MANAGER_KWS_STORAGE_MOUNT "/sdcard"
#endif

#ifndef CONFIG_ROUTINE_MANAGER_KWS_DEVICE_ID
#  define CONFIG_ROUTINE_MANAGER_KWS_DEVICE_ID "r528a"
#endif

struct routine_app_s
{
  struct routine_model_s model;
  struct routine_settings_s settings;
  struct routine_storage_s storage;
  bool storage_ready;
  struct environment_service_s environment;
#ifdef CONFIG_ROUTINE_MANAGER_VOICE
  struct audio_service_s audio;
#endif
  lv_timer_t *timer;
  lv_timer_t *voice_timer;
  bool environment_started;
  bool audio_started;
  uint32_t last_audio_sequence;
  int last_config_error;
  uint8_t temperature_zone;
  time_t last_wall_time;
  volatile sig_atomic_t stop;
};

enum routine_temperature_zone_e
{
  ROUTINE_TEMPERATURE_NORMAL = 0,
  ROUTINE_TEMPERATURE_LOW,
  ROUTINE_TEMPERATURE_HIGH
};

struct routine_options_s
{
  const char *font_path;
  unsigned int duration;
  uint8_t rate;
  enum routine_ui_language_e language;
  bool language_set;
  bool rate_set;
  bool font_set;
  bool demo;
  bool selftest;
#ifdef CONFIG_ROUTINE_MANAGER_KWS_CAPTURE
  struct kws_capture_options_s capture;
  bool kws_capture;
#endif
};

static struct routine_app_s *g_app;
static pthread_mutex_t g_instance_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_instance_running;

static void routine_usage(void)
{
  printf("Usage: routine_mgr [options]\n");
  printf("  --demo          Use an accelerated simulated clock\n");
  printf("  --chinese       Use the Chinese interface (default)\n");
  printf("  --english       Use the English interface\n");
  printf("  --ascii         Alias for --english\n");
  printf("  --font PATH     Set the Chinese LVGL binary font path\n");
  printf("  --rate SEC      Demo seconds per simulated hour (1-60)\n");
  printf("  --duration SEC  Exit after the requested duration\n");
  printf("  --selftest      Test the data model without using /dev/fb0\n");
#ifdef CONFIG_ROUTINE_MANAGER_KWS_CAPTURE
  printf("  -K, --kws-capture  Capture labeled 16 kHz DMIC corpus\n");
  printf("  -l, --label LABEL  Corpus label (required with -K)\n");
  printf("  -p, --speaker ID   Anonymous speaker ID (required)\n");
  printf("  -s, --session ID   Corpus session ID (required)\n");
  printf("  -i, --device-id ID Board ID (default: %s)\n",
         CONFIG_ROUTINE_MANAGER_KWS_DEVICE_ID);
  printf("  -n, --count N      Samples to keep (default: 20)\n");
  printf("  -m, --record-ms N  Milliseconds per sample\n");
  printf("  NSH has a 64-byte line limit; use the short one-line form.\n");
#endif
  printf("  --help          Show this help\n");
}

static int routine_parse_uint(const char *text, unsigned int minimum,
                              unsigned int maximum, unsigned int *value)
{
  char *end;
  unsigned long parsed;

  errno = 0;
  parsed = strtoul(text, &end, 10);
  if (errno != 0 || *text == '\0' || *end != '\0' ||
      parsed < minimum || parsed > maximum)
    {
      return -EINVAL;
    }

  *value = parsed;
  return 0;
}

static int routine_set_language(struct routine_options_s *options,
                                enum routine_ui_language_e language)
{
  if (options->language_set && options->language != language)
    {
      return -EINVAL;
    }

  options->language = language;
  options->language_set = true;
  return 0;
}

static int routine_parse_options(int argc, FAR char *argv[],
                                 struct routine_options_s *options)
{
  unsigned int value;
  int i;
  int ret;

  memset(options, 0, sizeof(*options));
  options->font_path = CONFIG_ROUTINE_MANAGER_FONT_PATH;
  options->rate = CONFIG_ROUTINE_MANAGER_DEMO_RATE;
  options->language = ROUTINE_UI_LANGUAGE_CHINESE;
#ifdef CONFIG_ROUTINE_MANAGER_KWS_CAPTURE
  options->capture.device_id = CONFIG_ROUTINE_MANAGER_KWS_DEVICE_ID;
  options->capture.capture_device =
    CONFIG_ROUTINE_MANAGER_VOICE_CAPTURE_DEVICE;
  options->capture.playback_device =
    CONFIG_ROUTINE_MANAGER_VOICE_PLAYBACK_DEVICE;
  options->capture.output_root = CONFIG_ROUTINE_MANAGER_KWS_CAPTURE_ROOT;
  options->capture.storage_mountpoint = CONFIG_ROUTINE_MANAGER_KWS_STORAGE_MOUNT;
  options->capture.record_ms = CONFIG_ROUTINE_MANAGER_VOICE_RECORD_MS;
  options->capture.playback_volume = CONFIG_ROUTINE_MANAGER_VOICE_VOLUME;
  options->capture.count = 20;
#endif

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "--demo") == 0)
        {
          options->demo = true;
        }
      else if (strcmp(argv[i], "--chinese") == 0)
        {
          ret = routine_set_language(options, ROUTINE_UI_LANGUAGE_CHINESE);
          if (ret < 0)
            {
              fprintf(stderr,
                      "routine_mgr: conflicting language option: %s\n",
                      argv[i]);
              return ret;
            }
        }
      else if (strcmp(argv[i], "--english") == 0 ||
               strcmp(argv[i], "--ascii") == 0)
        {
          ret = routine_set_language(options, ROUTINE_UI_LANGUAGE_ENGLISH);
          if (ret < 0)
            {
              fprintf(stderr,
                      "routine_mgr: conflicting language option: %s\n",
                      argv[i]);
              return ret;
            }
        }
      else if (strcmp(argv[i], "--selftest") == 0)
        {
          options->selftest = true;
        }
      else if (strcmp(argv[i], "--help") == 0)
        {
          routine_usage();
          return 1;
        }
      else if (strcmp(argv[i], "--font") == 0 && i + 1 < argc)
        {
          options->font_path = argv[++i];
          options->font_set = true;
        }
      else if (strcmp(argv[i], "--rate") == 0 && i + 1 < argc)
        {
          ret = routine_parse_uint(argv[++i], 1, 60, &value);
          if (ret < 0)
            {
              return ret;
            }

          options->rate = value;
          options->rate_set = true;
        }
      else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc)
        {
          ret = routine_parse_uint(argv[++i], 1, 86400, &value);
          if (ret < 0)
            {
              return ret;
            }

          options->duration = value;
        }
#ifdef CONFIG_ROUTINE_MANAGER_KWS_CAPTURE
      else if (strcmp(argv[i], "-K") == 0 ||
               strcmp(argv[i], "--kws-capture") == 0)
        {
          options->kws_capture = true;
        }
      else if ((strcmp(argv[i], "-l") == 0 ||
                strcmp(argv[i], "--label") == 0) && i + 1 < argc)
        {
          options->capture.label = argv[++i];
        }
      else if ((strcmp(argv[i], "-p") == 0 ||
                strcmp(argv[i], "--speaker") == 0) && i + 1 < argc)
        {
          options->capture.speaker_id = argv[++i];
        }
      else if ((strcmp(argv[i], "-s") == 0 ||
                strcmp(argv[i], "--session") == 0) && i + 1 < argc)
        {
          options->capture.session_id = argv[++i];
        }
      else if ((strcmp(argv[i], "-i") == 0 ||
                strcmp(argv[i], "--device-id") == 0) && i + 1 < argc)
        {
          options->capture.device_id = argv[++i];
        }
      else if ((strcmp(argv[i], "-n") == 0 ||
                strcmp(argv[i], "--count") == 0) && i + 1 < argc)
        {
          ret = routine_parse_uint(argv[++i], 1, 1000, &value);
          if (ret < 0)
            {
              return ret;
            }

          options->capture.count = value;
        }
      else if ((strcmp(argv[i], "-m") == 0 ||
                strcmp(argv[i], "--record-ms") == 0) && i + 1 < argc)
        {
          ret = routine_parse_uint(argv[++i], 1000, 10000, &value);
          if (ret < 0)
            {
              return ret;
            }

          options->capture.record_ms = value;
        }
#endif
      else
        {
          fprintf(stderr, "routine_mgr: unknown or incomplete option: %s\n",
                  argv[i]);
          return -EINVAL;
        }
    }

  if (options->rate_set && !options->demo)
    {
      fprintf(stderr, "routine_mgr: --rate requires --demo\n");
      return -EINVAL;
    }

#ifdef CONFIG_ROUTINE_MANAGER_KWS_CAPTURE
  if (options->kws_capture)
    {
      if (options->demo || options->selftest || options->duration > 0 ||
          options->rate_set || options->language_set || options->font_set ||
          kws_capture_validate_options(&options->capture) < 0)
        {
          fprintf(stderr, "routine_mgr: invalid --kws-capture options\n");
          return -EINVAL;
        }
    }
  else if (options->capture.label != NULL ||
           options->capture.speaker_id != NULL ||
           options->capture.session_id != NULL ||
           options->capture.count != 20 ||
           options->capture.record_ms != CONFIG_ROUTINE_MANAGER_VOICE_RECORD_MS ||
           strcmp(options->capture.device_id,
                  CONFIG_ROUTINE_MANAGER_KWS_DEVICE_ID) != 0)
    {
      fprintf(stderr, "routine_mgr: KWS options require --kws-capture\n");
      return -EINVAL;
    }
#endif

  return 0;
}

static int routine_options_selftest(void)
{
  struct routine_options_s options;
  char *default_argv[] =
  {
    "routine_mgr"
  };
  char *chinese_argv[] =
  {
    "routine_mgr", "--chinese"
  };
  char *english_argv[] =
  {
    "routine_mgr", "--english"
  };
  char *ascii_argv[] =
  {
    "routine_mgr", "--ascii"
  };
  char *aliases_argv[] =
  {
    "routine_mgr", "--english", "--ascii"
  };
  char *live_rate_argv[] =
  {
    "routine_mgr", "--rate", "5"
  };
  char *demo_rate_argv[] =
  {
    "routine_mgr", "--demo", "--rate", "5"
  };
#ifdef CONFIG_ROUTINE_MANAGER_KWS_CAPTURE
  char *capture_argv[] =
  {
    "routine_mgr", "--kws-capture", "--label", "query_temperature",
    "--speaker", "s001", "--session", "session_a"
  };
  char *short_capture_argv[] =
  {
    "routine_mgr", "-K", "-l", "query_temperature", "-p", "s001",
    "-s", "smoke_a", "-n", "1", "-m", "3500"
  };
  char *orphan_capture_argv[] =
  {
    "routine_mgr", "--label", "query_temperature"
  };
#endif

  if (routine_parse_options(1, default_argv, &options) < 0 ||
      options.language != ROUTINE_UI_LANGUAGE_CHINESE ||
      routine_parse_options(2, chinese_argv, &options) < 0 ||
      options.language != ROUTINE_UI_LANGUAGE_CHINESE ||
      routine_parse_options(2, english_argv, &options) < 0 ||
      options.language != ROUTINE_UI_LANGUAGE_ENGLISH ||
      routine_parse_options(2, ascii_argv, &options) < 0 ||
      options.language != ROUTINE_UI_LANGUAGE_ENGLISH ||
      routine_parse_options(3, aliases_argv, &options) < 0 ||
      options.language != ROUTINE_UI_LANGUAGE_ENGLISH ||
      routine_parse_options(3, live_rate_argv, &options) >= 0 ||
      routine_parse_options(4, demo_rate_argv, &options) < 0 ||
      !options.demo || options.rate != 5
#ifdef CONFIG_ROUTINE_MANAGER_KWS_CAPTURE
      || routine_parse_options(8, capture_argv, &options) < 0 ||
      !options.kws_capture || options.capture.count != 20 ||
      strcmp(options.capture.speaker_id, "s001") != 0 ||
      routine_parse_options(12, short_capture_argv, &options) < 0 ||
      !options.kws_capture || options.capture.count != 1 ||
      options.capture.record_ms != 3500 ||
      strcmp(options.capture.session_id, "smoke_a") != 0 ||
      routine_parse_options(3, orphan_capture_argv, &options) >= 0
#endif
      )
    {
      return -EINVAL;
    }

  memset(&options, 0, sizeof(options));
  if (routine_set_language(&options, ROUTINE_UI_LANGUAGE_CHINESE) < 0 ||
      routine_set_language(&options, ROUTINE_UI_LANGUAGE_ENGLISH) >= 0)
    {
      return -EINVAL;
    }

  return 0;
}

static void routine_signal_handler(int signo)
{
  if (g_app != NULL)
    {
      g_app->stop = 1;
    }
}

static void routine_update_audio_sensors(struct routine_app_s *app)
{
#ifdef CONFIG_ROUTINE_MANAGER_VOICE
  const struct routine_snapshot_s *snapshot;
  struct audio_service_sensor_values_s sensor;

  if (!app->audio_started)
    {
      return;
    }

  snapshot = routine_model_get_snapshot(&app->model);
  memset(&sensor, 0, sizeof(sensor));
  sensor.values.temperature_x100 = snapshot->temperature_x100;
  sensor.values.humidity_x100 = snapshot->humidity_x100;
  sensor.values.light_x10 = snapshot->light_x10;
  sensor.values.eco2_ppm = snapshot->eco2_ppm;
  sensor.values.tvoc_ppb = snapshot->tvoc_ppb;
  sensor.live_mask = snapshot->live_mask;
  sensor.warmup_mask = snapshot->warmup_mask;
  sensor.offline_mask = snapshot->offline_mask;
  audio_service_update_sensors(&app->audio, &sensor);
#endif
}

static int routine_get_local_clock(struct routine_clock_s *clock)
{
  struct tm utc_time;
  struct tm civil_time;
  time_t civil_wall_time;
  time_t wall_time;

  if (clock == NULL)
    {
      return -EINVAL;
    }

  wall_time = time(NULL);
  if (wall_time < 0 || gmtime_r(&wall_time, &utc_time) == NULL ||
      utc_time.tm_year + 1900 < 2020)
    {
      memset(clock, 0, sizeof(*clock));
      return -ENODATA;
    }

  civil_wall_time = wall_time +
                    CONFIG_ROUTINE_MANAGER_UTC_OFFSET_MINUTES * 60;
  if (gmtime_r(&civil_wall_time, &civil_time) == NULL)
    {
      memset(clock, 0, sizeof(*clock));
      return -ENODATA;
    }

  clock->day = civil_wall_time / (24 * 60 * 60);
  clock->hour = civil_time.tm_hour;
  clock->minute = civil_time.tm_min;
  clock->second = civil_time.tm_sec;
  clock->valid = true;
  return OK;
}

static int routine_reload_config(struct routine_app_s *app, bool notify)
{
  struct routine_settings_s settings;
  int ret;

  routine_config_defaults(&settings);
  ret = routine_config_load(&settings);
  if (ret < 0 && ret != -ENOENT)
    {
      if (ret != app->last_config_error)
        {
          syslog(LOG_WARNING, "routine_mgr: config ignored: %d\n", ret);
          app->last_config_error = ret;
        }
      return ret;
    }

  app->last_config_error = 0;
  ret = routine_model_apply_settings(&app->model, &settings);
  if (ret < 0)
    {
      return ret;
    }

  app->settings = settings;
  if (ret == 0)
    {
      return 0;
    }

  app->temperature_zone = ROUTINE_TEMPERATURE_NORMAL;
  if (notify)
    {
      routine_ui_clear_due();
#ifdef CONFIG_ROUTINE_MANAGER_VOICE
      if (app->audio_started)
        {
          audio_service_clear_reminders(&app->audio);
        }
#endif
    }

  if (app->storage_ready)
    {
      routine_storage_save(&app->storage, &app->model);
    }

  syslog(LOG_INFO,
         "routine_mgr: config generation=%lu low=%ld high=%ld\n",
         (unsigned long)settings.generation,
         (long)settings.temperature_low_x100,
         (long)settings.temperature_high_x100);
  return 1;
}

#ifdef CONFIG_ROUTINE_MANAGER_VOICE
static void routine_check_temperature_warning(struct routine_app_s *app)
{
  FAR const struct routine_snapshot_s *snapshot =
    routine_model_get_snapshot(&app->model);
  const int32_t hysteresis_x100 = 100;
  int ret;

  if (!app->audio_started ||
      (snapshot->live_mask & ENV_METRIC_TEMPERATURE) == 0 ||
      (snapshot->stale_mask & ENV_METRIC_TEMPERATURE) != 0 ||
      (snapshot->warmup_mask & ENV_METRIC_TEMPERATURE) != 0 ||
      (snapshot->offline_mask & ENV_METRIC_TEMPERATURE) != 0)
    {
      return;
    }

  if (app->temperature_zone == ROUTINE_TEMPERATURE_LOW)
    {
      if (snapshot->temperature_x100 >= snapshot->temperature_high_x100)
        {
          ret = audio_service_play_temperature_warning(&app->audio, true);
          if (ret == OK)
            {
              app->temperature_zone = ROUTINE_TEMPERATURE_HIGH;
            }
        }
      else if (snapshot->temperature_x100 >=
               snapshot->temperature_low_x100 + hysteresis_x100)
        {
          app->temperature_zone = ROUTINE_TEMPERATURE_NORMAL;
        }
      return;
    }

  if (app->temperature_zone == ROUTINE_TEMPERATURE_HIGH)
    {
      if (snapshot->temperature_x100 <= snapshot->temperature_low_x100)
        {
          ret = audio_service_play_temperature_warning(&app->audio, false);
          if (ret == OK)
            {
              app->temperature_zone = ROUTINE_TEMPERATURE_LOW;
            }
        }
      else if (snapshot->temperature_x100 <=
               snapshot->temperature_high_x100 - hysteresis_x100)
        {
          app->temperature_zone = ROUTINE_TEMPERATURE_NORMAL;
        }
      return;
    }

  if (snapshot->temperature_x100 < snapshot->temperature_low_x100)
    {
      ret = audio_service_play_temperature_warning(&app->audio, false);
      if (ret == OK)
        {
          app->temperature_zone = ROUTINE_TEMPERATURE_LOW;
        }
    }
  else if (snapshot->temperature_x100 > snapshot->temperature_high_x100)
    {
      ret = audio_service_play_temperature_warning(&app->audio, true);
      if (ret == OK)
        {
          app->temperature_zone = ROUTINE_TEMPERATURE_HIGH;
        }
    }
}
#endif

static void routine_dispatch_due(struct routine_app_s *app)
{
  struct routine_due_event_s event;

  while (routine_model_take_due(&app->model, &event) == OK)
    {
      int ui_ret = routine_ui_enqueue_due(&event);

      if (ui_ret < 0 && ui_ret != -EALREADY)
        {
          syslog(LOG_ERR,
                 "routine_mgr: due UI queue id=%u failed: %d\n",
                 event.schedule_id, ui_ret);
        }

#ifdef CONFIG_ROUTINE_MANAGER_VOICE
      if (app->audio_started)
        {
          struct audio_service_reminder_s reminder;
          int audio_ret;

          memset(&reminder, 0, sizeof(reminder));
          reminder.sequence = event.sequence;
          reminder.schedule_id = event.schedule_id;
          reminder.text_id = event.text_id;
          reminder.hour = event.hour;
          reminder.minute = event.minute;
          reminder.second = event.second;
          audio_ret = audio_service_enqueue_reminder(&app->audio, &reminder);
          if (audio_ret < 0 && audio_ret != -EALREADY)
            {
              syslog(LOG_ERR,
                     "routine_mgr: due audio queue id=%u failed: %d\n",
                     event.schedule_id, audio_ret);
            }
        }
#endif
    }
}

static void routine_timer_cb(lv_timer_t *timer)
{
  struct routine_app_s *app = lv_timer_get_user_data(timer);
  struct environment_sample_s environment;
  struct routine_clock_s clock;

  if (app->model.clock_mode == ROUTINE_CLOCK_DEMO)
    {
      routine_model_tick_demo(&app->model);
    }
  else if (routine_get_local_clock(&clock) == OK)
    {
      time_t wall_time = time(NULL);
      int clock_ret;

      if (app->last_wall_time != 0 &&
          (wall_time < app->last_wall_time ||
           wall_time - app->last_wall_time > ROUTINE_CLOCK_REBASE_SECONDS))
        {
          routine_model_clear_due(&app->model);
          routine_model_init_live(&app->model, &clock);
          clock_ret = ROUTINE_CLOCK_REBASED;
        }
      else
        {
          clock_ret = routine_model_update_clock(&app->model, &clock);
        }

      app->last_wall_time = wall_time;
      if (clock_ret == ROUTINE_CLOCK_REBASED)
        {
          routine_ui_clear_due();
#ifdef CONFIG_ROUTINE_MANAGER_VOICE
          if (app->audio_started)
            {
              audio_service_clear_reminders(&app->audio);
            }
#endif
        }
    }
  if (app->environment_started &&
      environment_service_get_sample(&app->environment, &environment) == 0)
    {
      routine_model_apply_environment(&app->model, &environment);
    }

  routine_reload_config(app, true);
  routine_dispatch_due(app);
  routine_update_audio_sensors(app);
#ifdef CONFIG_ROUTINE_MANAGER_VOICE
  routine_check_temperature_warning(app);
#endif
  if (app->storage_ready)
    {
      routine_storage_append_history(&app->storage,
                                     routine_model_get_snapshot(&app->model),
                                     app->settings.history_interval_seconds);
      routine_storage_publish_status(&app->storage,
                                    routine_model_get_snapshot(&app->model));
    }

  routine_ui_update(routine_model_get_snapshot(&app->model));
}

#ifdef CONFIG_ROUTINE_MANAGER_VOICE
static void routine_voice_timer_cb(lv_timer_t *timer)
{
  struct routine_app_s *app = lv_timer_get_user_data(timer);
  struct audio_service_snapshot_s snapshot;

  if (!app->audio_started ||
      audio_service_get_snapshot(&app->audio, &snapshot) < 0)
    {
      return;
    }

  if (snapshot.sequence != app->last_audio_sequence)
    {
      app->last_audio_sequence = snapshot.sequence;
      routine_ui_voice_update(&snapshot);
    }

}

static int routine_voice_action_cb(
  void *user_data, enum audio_service_request_e request, bool start)
{
  struct routine_app_s *app = user_data;

  if (app == NULL || !app->audio_started)
    {
      return -ENODEV;
    }

  if (!start)
    {
      return audio_service_cancel(&app->audio);
    }

  return request == AUDIO_SERVICE_REQUEST_QUERY ?
         audio_service_begin(&app->audio) :
         audio_service_play_sensor(&app->audio, request);
}
#endif

static int routine_history_action_cb(
  void *user_data, enum routine_ui_history_action_e action,
  uint32_t interval_seconds, char *result, size_t result_size)
{
  struct routine_app_s *app = user_data;
  int ret;

  if (app == NULL || result == NULL || result_size == 0)
    {
      return -EINVAL;
    }
  result[0] = '\0';

  if (action == ROUTINE_UI_HISTORY_SET_INTERVAL)
    {
      struct routine_settings_s settings = app->settings;

      if (!routine_history_interval_valid(interval_seconds))
        {
          return -EINVAL;
        }

      settings.history_interval_seconds = interval_seconds;
      settings.generation = settings.generation == UINT32_MAX ?
                            1 : settings.generation + 1;
      ret = routine_config_save(&settings);
      if (ret < 0)
        {
          return ret;
        }

      ret = routine_reload_config(app, true);
      if (ret < 0)
        {
          return ret;
        }
      if (app->storage_ready)
        {
          routine_storage_publish_status(
            &app->storage, routine_model_get_snapshot(&app->model));
        }
      routine_ui_update(routine_model_get_snapshot(&app->model));
      snprintf(result, result_size, "%lu",
               (unsigned long)interval_seconds);
      return OK;
    }

  if (action != ROUTINE_UI_HISTORY_EXPORT_SD || !app->storage_ready)
    {
      return action == ROUTINE_UI_HISTORY_EXPORT_SD ? -ENODEV : -EINVAL;
    }

  ret = routine_storage_export_history(&app->storage, result, result_size);
  routine_storage_append_event(&app->storage, "history_export", ret);
  return ret;
}

static int routine_schedule_action_cb(
  void *user_data, uint8_t schedule_id, uint32_t due_sequence,
  enum routine_schedule_action_e action)
{
  struct routine_due_event_s ended_due;
  struct routine_app_s *app = user_data;
  int ret;

  if (app == NULL)
    {
      return -EINVAL;
    }

  memset(&ended_due, 0, sizeof(ended_due));
  ret = routine_model_apply_schedule_action(&app->model, schedule_id,
                                            due_sequence, action,
                                            &ended_due);
  if (ret == -ESTALE && due_sequence != 0)
    {
      memset(&ended_due, 0, sizeof(ended_due));
      ended_due.sequence = due_sequence;
      ended_due.schedule_id = schedule_id;
      ret = OK;
    }
  else if (ret < 0)
    {
      syslog(LOG_ERR,
             "routine_mgr: schedule action=%u id=%u sequence=%lu failed: %d\n",
             action, schedule_id, (unsigned long)due_sequence, ret);
      return ret;
    }

  if (ended_due.sequence != 0)
    {
      routine_ui_remove_due(&ended_due);
#ifdef CONFIG_ROUTINE_MANAGER_VOICE
      if (app->audio_started)
        {
          struct audio_service_reminder_s reminder;

          memset(&reminder, 0, sizeof(reminder));
          reminder.sequence = ended_due.sequence;
          reminder.schedule_id = ended_due.schedule_id;
          reminder.text_id = ended_due.text_id;
          reminder.hour = ended_due.hour;
          reminder.minute = ended_due.minute;
          audio_service_cancel_reminder(&app->audio, &reminder);
        }
#endif
    }

  if (app->storage_ready)
    {
      if (routine_storage_save(&app->storage, &app->model) < 0)
        {
          syslog(LOG_WARNING, "routine_mgr: schedule persistence failed\n");
        }

      routine_storage_append_event(&app->storage, "schedule_action", ret);
      routine_storage_publish_status(&app->storage,
                                    routine_model_get_snapshot(&app->model));
    }
  routine_ui_update(routine_model_get_snapshot(&app->model));
  return OK;
}

int main(int argc, FAR char *argv[])
{
  struct routine_ui_options_s ui_options;
  struct routine_options_s options;
#ifdef CONFIG_ROUTINE_MANAGER_VOICE
  struct audio_service_config_s audio_config;
#endif
  struct routine_app_s app;
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
  struct sigaction action;
  struct timespec started;
  struct timespec now;
  int32_t width;
  int32_t height;
  uint32_t idle;
  struct routine_clock_s model_clock;
  int ret;
  bool lvgl_initialized = false;
  bool nuttx_initialized = false;
  bool ui_initialized = false;

  ret = routine_parse_options(argc, argv, &options);
  if (ret > 0)
    {
      return 0;
    }
  else if (ret < 0)
    {
      routine_usage();
      return EXIT_FAILURE;
    }

#ifdef CONFIG_ROUTINE_MANAGER_KWS_CAPTURE
  if (options.kws_capture)
    {
      pthread_mutex_lock(&g_instance_lock);
      if (g_instance_running)
        {
          pthread_mutex_unlock(&g_instance_lock);
          fprintf(stderr, "routine_mgr: another instance is already running\n");
          return EXIT_FAILURE;
        }

      g_instance_running = true;
      pthread_mutex_unlock(&g_instance_lock);
      ret = kws_capture_run(&options.capture);
      if (ret < 0 && ret != -ECANCELED)
        {
          fprintf(stderr, "routine_mgr: KWS capture failed: %d\n", ret);
        }

      pthread_mutex_lock(&g_instance_lock);
      g_instance_running = false;
      pthread_mutex_unlock(&g_instance_lock);
      return ret == OK || ret == -ECANCELED ? EXIT_SUCCESS : EXIT_FAILURE;
    }
#endif

  if (options.selftest)
    {
      ret = routine_options_selftest();
      if (ret == 0)
        {
          ret = routine_model_selftest();
        }

#ifdef CONFIG_ROUTINE_MANAGER_ENVMON
      if (ret == 0)
        {
          ret = environment_service_selftest();
        }
#endif

#ifdef CONFIG_ROUTINE_MANAGER_VOICE
      if (ret == 0)
        {
          ret = audio_service_selftest();
        }
#endif

#ifdef CONFIG_ROUTINE_MANAGER_KWS_CAPTURE
      if (ret == 0)
        {
          ret = kws_capture_selftest();
        }
#endif

      printf("routine_mgr selftest: %s (%d)\n",
             ret == 0 ? "PASS" : "FAIL", ret);
      return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  pthread_mutex_lock(&g_instance_lock);
  if (g_instance_running)
    {
      pthread_mutex_unlock(&g_instance_lock);
      syslog(LOG_ERR, "routine_mgr: another instance is already running\n");
      return EXIT_FAILURE;
    }

  g_instance_running = true;
  pthread_mutex_unlock(&g_instance_lock);

  memset(&app, 0, sizeof(app));
  memset(&result, 0, sizeof(result));
  memset(&action, 0, sizeof(action));
  action.sa_handler = routine_signal_handler;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGTERM, &action, NULL);
  g_app = &app;

  lv_init();
  lvgl_initialized = true;

  lv_nuttx_dsc_init(&info);
  info.fb_path = "/dev/fb0";
  info.input_path = "/dev/input0";
  info.utouch_path = NULL;
  info.mouse_path = NULL;
  lv_nuttx_init(&info, &result);
  nuttx_initialized = true;

  if (result.disp == NULL)
    {
      syslog(LOG_ERR, "routine_mgr: failed to initialize /dev/fb0\n");
      ret = -ENODEV;
      goto out;
    }

  if (result.indev == NULL)
    {
      syslog(LOG_ERR, "routine_mgr: failed to initialize /dev/input0\n");
      ret = -ENODEV;
      goto out;
    }

  width = lv_display_get_horizontal_resolution(result.disp);
  height = lv_display_get_vertical_resolution(result.disp);
  syslog(LOG_INFO, "routine_mgr: display=%ldx%ld, input=/dev/input0\n",
         (long)width, (long)height);
  if (width != 480 || height != 320)
    {
      syslog(LOG_ERR,
             "routine_mgr: expected 480x320 landscape, got %ldx%ld\n",
             (long)width, (long)height);
      ret = -ENOTSUP;
      goto out;
    }

  ret = routine_storage_init(&app.storage);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "routine_mgr: storage unavailable: %d\n", ret);
    }
  else
    {
      app.storage_ready = true;
    }

  if (options.demo)
    {
      routine_model_init_demo(&app.model, options.rate);
    }
  else
    {
      ret = routine_get_local_clock(&model_clock);
      if (ret < 0)
        {
          syslog(LOG_WARNING,
                 "routine_mgr: UTC clock is invalid; waiting for NTP\n");
          routine_model_init_live_pending(&app.model);
        }
      else if (routine_model_init_live(&app.model, &model_clock) < 0)
        {
          syslog(LOG_ERR, "routine_mgr: failed to initialize live clock\n");
          goto out;
        }
      else
        {
          app.last_wall_time = time(NULL);
        }
    }

  if (app.storage_ready)
    {
      ret = routine_storage_load(&app.storage, &app.model);
      if (ret < 0 && ret != -ENOENT)
        {
          syslog(LOG_WARNING,
                 "routine_mgr: schedule state ignored: %d\n", ret);
          routine_storage_append_event(&app.storage, "schedule_load", ret);
        }
      else if (ret == 0)
        {
          syslog(LOG_INFO, "routine_mgr: schedule state restored\n");
        }

    }

  routine_reload_config(&app, false);
  if (app.storage_ready)
    {
      routine_storage_publish_status(&app.storage,
                                     routine_model_get_snapshot(&app.model));
    }
#ifdef CONFIG_ROUTINE_MANAGER_ENVMON
  ret = environment_service_start(&app.environment,
                                  CONFIG_ROUTINE_MANAGER_I2C_PATH);
  if (ret < 0)
    {
      syslog(LOG_WARNING,
             "routine_mgr: environment service unavailable: %d; demo fallback\n",
             ret);
    }
  else
    {
      app.environment_started = true;
    }
#endif

#ifdef CONFIG_ROUTINE_MANAGER_VOICE
  memset(&audio_config, 0, sizeof(audio_config));
  audio_config.capture_device = CONFIG_ROUTINE_MANAGER_VOICE_CAPTURE_DEVICE;
  audio_config.playback_device = CONFIG_ROUTINE_MANAGER_VOICE_PLAYBACK_DEVICE;
  audio_config.record_path = "/tmp/routine_query.pcm";
  audio_config.reply_path = "/tmp/routine_reply.pcm";
  audio_config.asset_dir = CONFIG_ROUTINE_MANAGER_VOICE_ASSET_DIR;
  audio_config.record_ms = CONFIG_ROUTINE_MANAGER_VOICE_RECORD_MS;
  audio_config.volume = CONFIG_ROUTINE_MANAGER_VOICE_VOLUME;
  audio_config.priority = CONFIG_ROUTINE_MANAGER_VOICE_PRIORITY;
  audio_config.stack_size = CONFIG_ROUTINE_MANAGER_VOICE_STACKSIZE;
  ret = audio_service_initialize(&app.audio, &audio_config);
  if (ret < 0)
    {
      syslog(LOG_WARNING,
             "routine_mgr: voice service unavailable: %d\n", ret);
    }
  else
    {
      app.audio_started = true;
      routine_update_audio_sensors(&app);
    }
#endif

  ui_options.language = options.language;
  ui_options.font_path = options.font_path;
  ui_options.schedule_action = routine_schedule_action_cb;
  ui_options.history_action = routine_history_action_cb;
#ifdef CONFIG_ROUTINE_MANAGER_VOICE
  ui_options.voice_action = routine_voice_action_cb;
#else
  ui_options.voice_action = NULL;
#endif
  ui_options.user_data = &app;
  ret = routine_ui_init(result.disp, &ui_options,
                        routine_model_get_snapshot(&app.model));
  if (ret < 0)
    {
      syslog(LOG_ERR, "routine_mgr: UI initialization failed: %d\n", ret);
      goto out;
    }

  ui_initialized = true;
  app.timer = lv_timer_create(routine_timer_cb, 1000, &app);
  if (app.timer == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

#ifdef CONFIG_ROUTINE_MANAGER_VOICE
  if (app.audio_started)
    {
      app.voice_timer = lv_timer_create(routine_voice_timer_cb, 100, &app);
      if (app.voice_timer == NULL)
        {
          ret = -ENOMEM;
          goto out;
        }
    }
#endif

  clock_gettime(CLOCK_MONOTONIC, &started);
  ret = 0;
  while (!app.stop)
    {
      idle = lv_timer_handler();
      idle = idle == 0 ? 1 : idle;
      usleep(idle * 1000);

      if (options.duration > 0)
        {
          clock_gettime(CLOCK_MONOTONIC, &now);
          if ((unsigned int)(now.tv_sec - started.tv_sec) >=
              options.duration)
            {
              break;
            }
        }
    }

out:
  if (app.voice_timer != NULL)
    {
      lv_timer_delete(app.voice_timer);
      app.voice_timer = NULL;
    }

#ifdef CONFIG_ROUTINE_MANAGER_VOICE
  if (app.audio_started)
    {
      audio_service_cancel(&app.audio);
      audio_service_shutdown(&app.audio);
      app.audio_started = false;
    }
#endif

  if (app.timer != NULL)
    {
      lv_timer_delete(app.timer);
      app.timer = NULL;
    }

  if (app.environment_started)
    {
      environment_service_stop(&app.environment);
      app.environment_started = false;
    }

  if (ui_initialized)
    {
      routine_ui_deinit();
    }

  if (nuttx_initialized)
    {
      lv_nuttx_deinit(&result);
    }

  if (lvgl_initialized)
    {
      lv_deinit();
    }

  g_app = NULL;
  pthread_mutex_lock(&g_instance_lock);
  g_instance_running = false;
  pthread_mutex_unlock(&g_instance_lock);
  return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
