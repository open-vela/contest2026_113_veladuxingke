/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/environment_service.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "environment_service.h"

#ifndef CONFIG_ROUTINE_MANAGER_ENV_PRIORITY
#  define CONFIG_ROUTINE_MANAGER_ENV_PRIORITY 90
#endif

#ifndef CONFIG_ROUTINE_MANAGER_ENV_STACKSIZE
#  define CONFIG_ROUTINE_MANAGER_ENV_STACKSIZE 4096
#endif

#define ENV_SENSOR_SHT40  0
#define ENV_SENSOR_BH1750 1
#define ENV_SENSOR_SGP30  2
#define ENV_SENSOR_COUNT  3
#define ENV_STALE_SECONDS 3
#define ENV_FAILURE_LIMIT 3

#define ENV_STATE_UNKNOWN     0
#define ENV_STATE_UNAVAILABLE 1
#define ENV_STATE_WARMUP      2
#define ENV_STATE_LIVE        3
#define ENV_STATE_STALE       4
#define ENV_STATE_RECOVERING  5

static const char *g_sensor_name[ENV_SENSOR_COUNT] =
{
  "SHT40", "BH1750", "SGP30"
};

static const char *g_state_name[] =
{
  "UNKNOWN", "UNAVAILABLE", "WARMUP", "LIVE", "STALE", "RECOVERING"
};

static void environment_add_seconds(FAR struct timespec *time,
                                    unsigned int seconds)
{
  time->tv_sec += seconds;
}

static int environment_compare_time(FAR const struct timespec *left,
                                    FAR const struct timespec *right)
{
  if (left->tv_sec != right->tv_sec)
    {
      return left->tv_sec < right->tv_sec ? -1 : 1;
    }

  if (left->tv_nsec != right->tv_nsec)
    {
      return left->tv_nsec < right->tv_nsec ? -1 : 1;
    }

  return 0;
}

static bool environment_is_stale(FAR const struct timespec *now,
                                 FAR const struct timespec *last)
{
  return last->tv_sec == 0 || now->tv_sec - last->tv_sec > ENV_STALE_SECONDS;
}

static bool environment_should_stop(
  FAR struct environment_service_s *service)
{
  bool stop;

  pthread_mutex_lock(&service->control_lock);
  stop = service->stop;
  pthread_mutex_unlock(&service->control_lock);
  return stop;
}

static void environment_log_state(FAR struct environment_service_s *service,
                                  int sensor, uint8_t state, int error)
{
  if (service->last_state[sensor] == state &&
      service->last_error[sensor] == error)
    {
      return;
    }

  printf("envmon: %s %s", g_sensor_name[sensor], g_state_name[state]);
  if (error < 0)
    {
      printf(" error=%d", error);
    }

  if (sensor == ENV_SENSOR_BH1750 && service->bh1750.address != 0)
    {
      printf(" address=0x%02x", service->bh1750.address);
    }

  printf("\n");
  service->last_state[sensor] = state;
  service->last_error[sensor] = error;
}

static void environment_schedule_retry(
  FAR struct environment_service_s *service, int sensor,
  FAR const struct timespec *now, int error)
{
  uint8_t delay = service->backoff[sensor];

  service->next_retry[sensor] = *now;
  environment_add_seconds(&service->next_retry[sensor], delay);
  service->backoff[sensor] = delay < 60 ? delay * 2 : 60;
  if (service->backoff[sensor] > 60)
    {
      service->backoff[sensor] = 60;
    }

  environment_log_state(service, sensor, ENV_STATE_RECOVERING, error);
}

static bool environment_retry_due(
  FAR struct environment_service_s *service, int sensor,
  FAR const struct timespec *now)
{
  return service->next_retry[sensor].tv_sec == 0 ||
         environment_compare_time(now, &service->next_retry[sensor]) >= 0;
}

static void environment_record_success(
  FAR struct environment_service_s *service, int sensor,
  FAR const struct timespec *now)
{
  service->failures[sensor] = 0;
  service->backoff[sensor] = 5;
  service->next_retry[sensor].tv_sec = 0;
  service->next_retry[sensor].tv_nsec = 0;
  environment_log_state(service, sensor, ENV_STATE_LIVE, 0);
}

static void environment_sample_sht40(
  FAR struct environment_service_s *service, FAR const struct timespec *now)
{
  int32_t temperature;
  uint16_t humidity;
  int ret;

  if (!environment_retry_due(service, ENV_SENSOR_SHT40, now))
    {
      return;
    }

  ret = envmon_sht40_measure(&service->sht40, &temperature, &humidity);
  if (ret < 0)
    {
      if (++service->failures[ENV_SENSOR_SHT40] >= ENV_FAILURE_LIMIT)
        {
          environment_schedule_retry(service, ENV_SENSOR_SHT40, now, ret);
        }
      else
        {
          environment_log_state(service, ENV_SENSOR_SHT40,
                                service->last_good[0].tv_sec == 0 ?
                                  ENV_STATE_UNAVAILABLE : ENV_STATE_STALE,
                                ret);
        }

      return;
    }

  pthread_mutex_lock(&service->sample_lock);
  service->sample.temperature_x100 = temperature;
  service->sample.humidity_x100 = humidity;
  service->sample.valid_mask |= ENV_METRIC_TEMPERATURE |
                                ENV_METRIC_HUMIDITY;
  service->last_good[0] = *now;
  service->last_good[1] = *now;
  pthread_mutex_unlock(&service->sample_lock);
  environment_record_success(service, ENV_SENSOR_SHT40, now);
}

static void environment_init_bh1750(
  FAR struct environment_service_s *service, FAR const struct timespec *now)
{
  int ret;

  if (service->bh1750.initialized ||
      !environment_retry_due(service, ENV_SENSOR_BH1750, now))
    {
      return;
    }

  ret = envmon_bh1750_start(&service->bh1750);
  if (ret < 0)
    {
      environment_schedule_retry(service, ENV_SENSOR_BH1750, now, ret);
      return;
    }

  service->failures[ENV_SENSOR_BH1750] = 0;
  service->backoff[ENV_SENSOR_BH1750] = 5;
  environment_log_state(service, ENV_SENSOR_BH1750, ENV_STATE_LIVE, 0);
}

static void environment_sample_bh1750(
  FAR struct environment_service_s *service, FAR const struct timespec *now)
{
  uint32_t light;
  int ret;

  environment_init_bh1750(service, now);
  if (!service->bh1750.initialized)
    {
      return;
    }

  ret = envmon_bh1750_read(&service->bh1750, &light);
  if (ret < 0)
    {
      if (++service->failures[ENV_SENSOR_BH1750] >= ENV_FAILURE_LIMIT)
        {
          service->bh1750.initialized = false;
          environment_schedule_retry(service, ENV_SENSOR_BH1750, now, ret);
        }
      else
        {
          environment_log_state(service, ENV_SENSOR_BH1750,
                                ENV_STATE_STALE, ret);
        }

      return;
    }

  pthread_mutex_lock(&service->sample_lock);
  service->sample.light_x10 = light;
  service->sample.valid_mask |= ENV_METRIC_LIGHT;
  service->last_good[2] = *now;
  pthread_mutex_unlock(&service->sample_lock);
  environment_record_success(service, ENV_SENSOR_BH1750, now);
}

static void environment_init_sgp30(
  FAR struct environment_service_s *service, FAR const struct timespec *now)
{
  int ret;

  if (service->sgp30.initialized ||
      !environment_retry_due(service, ENV_SENSOR_SGP30, now))
    {
      return;
    }

  ret = envmon_sgp30_init_air_quality(&service->sgp30);
  if (ret < 0)
    {
      environment_schedule_retry(service, ENV_SENSOR_SGP30, now, ret);
      return;
    }

  service->failures[ENV_SENSOR_SGP30] = 0;
  service->backoff[ENV_SENSOR_SGP30] = 5;
  environment_log_state(service, ENV_SENSOR_SGP30, ENV_STATE_WARMUP, 0);
}

static void environment_sample_sgp30(
  FAR struct environment_service_s *service, FAR const struct timespec *now)
{
  uint16_t eco2;
  uint16_t tvoc;
  bool warmup;
  int ret;

  environment_init_sgp30(service, now);
  if (!service->sgp30.initialized)
    {
      return;
    }

  ret = envmon_sgp30_measure(&service->sgp30, &eco2, &tvoc);
  if (ret < 0)
    {
      if (++service->failures[ENV_SENSOR_SGP30] >= ENV_FAILURE_LIMIT)
        {
          service->sgp30.initialized = false;
          environment_schedule_retry(service, ENV_SENSOR_SGP30, now, ret);
        }
      else
        {
          environment_log_state(service, ENV_SENSOR_SGP30,
                                ENV_STATE_STALE, ret);
        }

      return;
    }

  warmup = envmon_sgp30_warming_up(&service->sgp30, now);
  service->failures[ENV_SENSOR_SGP30] = 0;
  if (warmup)
    {
      environment_log_state(service, ENV_SENSOR_SGP30,
                            ENV_STATE_WARMUP, 0);
      return;
    }

  pthread_mutex_lock(&service->sample_lock);
  service->sample.eco2_ppm = eco2;
  service->sample.tvoc_ppb = tvoc;
  service->sample.valid_mask |= ENV_METRIC_ECO2 | ENV_METRIC_TVOC;
  service->last_good[3] = *now;
  service->last_good[4] = *now;
  pthread_mutex_unlock(&service->sample_lock);
  environment_record_success(service, ENV_SENSOR_SGP30, now);
}

static void environment_publish_masks(
  FAR struct environment_service_s *service, FAR const struct timespec *now)
{
  static const uint32_t metric[5] =
  {
    ENV_METRIC_TEMPERATURE, ENV_METRIC_HUMIDITY, ENV_METRIC_LIGHT,
    ENV_METRIC_ECO2, ENV_METRIC_TVOC
  };
  uint32_t live = 0;
  uint32_t stale = 0;
  uint32_t warmup = 0;
  uint32_t offline = 0;
  int i;

  pthread_mutex_lock(&service->sample_lock);
  for (i = 0; i < 5; i++)
    {
      if ((service->sample.valid_mask & metric[i]) == 0)
        {
          continue;
        }

      if (environment_is_stale(now, &service->last_good[i]))
        {
          stale |= metric[i];
        }
      else
        {
          live |= metric[i];
        }
    }

  if (service->sgp30.initialized &&
      envmon_sgp30_warming_up(&service->sgp30, now))
    {
      warmup = ENV_METRIC_ECO2 | ENV_METRIC_TVOC;
    }

  if (service->last_state[ENV_SENSOR_SHT40] == ENV_STATE_UNAVAILABLE ||
      service->last_state[ENV_SENSOR_SHT40] == ENV_STATE_RECOVERING)
    {
      offline |= ENV_METRIC_TEMPERATURE | ENV_METRIC_HUMIDITY;
    }

  if (service->last_state[ENV_SENSOR_BH1750] == ENV_STATE_UNAVAILABLE ||
      service->last_state[ENV_SENSOR_BH1750] == ENV_STATE_RECOVERING)
    {
      offline |= ENV_METRIC_LIGHT;
    }

  if (service->last_state[ENV_SENSOR_SGP30] == ENV_STATE_UNAVAILABLE ||
      service->last_state[ENV_SENSOR_SGP30] == ENV_STATE_RECOVERING)
    {
      offline |= ENV_METRIC_ECO2 | ENV_METRIC_TVOC;
    }

  service->sample.live_mask = live & ~offline;
  service->sample.stale_mask = stale & ~offline;
  service->sample.warmup_mask = warmup & ~offline;
  service->sample.offline_mask = offline;
  service->sample.demo_mask = ENV_METRIC_ALL &
                              ~(service->sample.valid_mask | warmup | offline);
  service->sample.captured_at = *now;
  service->sample.sequence++;
  pthread_mutex_unlock(&service->sample_lock);
}

static void *environment_thread(FAR void *arg)
{
  FAR struct environment_service_s *service = arg;
  struct timespec next;
  struct timespec now;

  clock_gettime(CLOCK_MONOTONIC, &next);
  while (!environment_should_stop(service))
    {
      clock_gettime(CLOCK_MONOTONIC, &now);
      environment_sample_sgp30(service, &now);
      if (environment_should_stop(service))
        {
          break;
        }

      clock_gettime(CLOCK_MONOTONIC, &now);
      environment_sample_sht40(service, &now);
      if (environment_should_stop(service))
        {
          break;
        }

      clock_gettime(CLOCK_MONOTONIC, &now);
      environment_sample_bh1750(service, &now);
      clock_gettime(CLOCK_MONOTONIC, &now);
      environment_publish_masks(service, &now);

      environment_add_seconds(&next, 1);
      while (environment_compare_time(&next, &now) <= 0)
        {
          environment_add_seconds(&next, 1);
        }

      pthread_mutex_lock(&service->control_lock);
      if (!service->stop)
        {
          pthread_cond_timedwait(&service->control_cond,
                                 &service->control_lock, &next);
        }

      pthread_mutex_unlock(&service->control_lock);
    }

  return NULL;
}

int environment_service_start(FAR struct environment_service_s *service,
                              FAR const char *i2c_path)
{
  pthread_condattr_t condattr;
  pthread_attr_t attr;
  struct sched_param param;
  int ret;
  int i;

  if (service == NULL || i2c_path == NULL)
    {
      return -EINVAL;
    }

  if (service->started)
    {
      return -EALREADY;
    }

  memset(service, 0, sizeof(*service));
  service->bus.fd = -1;
  for (i = 0; i < ENV_SENSOR_COUNT; i++)
    {
      service->backoff[i] = 5;
      service->last_error[i] = INT32_MIN;
    }

  ret = pthread_mutex_init(&service->sample_lock, NULL);
  if (ret != 0)
    {
      return -ret;
    }

  ret = pthread_mutex_init(&service->control_lock, NULL);
  if (ret != 0)
    {
      pthread_mutex_destroy(&service->sample_lock);
      return -ret;
    }

  pthread_condattr_init(&condattr);
  pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
  ret = pthread_cond_init(&service->control_cond, &condattr);
  pthread_condattr_destroy(&condattr);
  if (ret != 0)
    {
      pthread_mutex_destroy(&service->control_lock);
      pthread_mutex_destroy(&service->sample_lock);
      return -ret;
    }

  service->sync_initialized = true;
  ret = envmon_i2c_open(&service->bus, i2c_path);
  if (ret < 0)
    {
      environment_service_stop(service);
      return ret;
    }

  envmon_sht40_init(&service->sht40, &service->bus);
  envmon_bh1750_reset(&service->bh1750, &service->bus);
  envmon_sgp30_reset(&service->sgp30, &service->bus);

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, CONFIG_ROUTINE_MANAGER_ENV_STACKSIZE);
  param.sched_priority = CONFIG_ROUTINE_MANAGER_ENV_PRIORITY;
  pthread_attr_setschedparam(&attr, &param);
  service->started = true;
  ret = pthread_create(&service->thread, &attr, environment_thread, service);
  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      service->started = false;
      environment_service_stop(service);
      return -ret;
    }

  service->thread_created = true;
  return OK;
}

int environment_service_get_sample(
  FAR struct environment_service_s *service,
  FAR struct environment_sample_s *sample)
{
  if (service == NULL || sample == NULL || !service->started)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&service->sample_lock);
  *sample = service->sample;
  pthread_mutex_unlock(&service->sample_lock);
  return OK;
}

int environment_service_stop(FAR struct environment_service_s *service)
{
  if (service == NULL)
    {
      return -EINVAL;
    }

  if (service->sync_initialized)
    {
      pthread_mutex_lock(&service->control_lock);
      service->stop = true;
      pthread_cond_signal(&service->control_cond);
      pthread_mutex_unlock(&service->control_lock);
    }

  if (service->thread_created)
    {
      pthread_join(service->thread, NULL);
      service->thread_created = false;
    }

  envmon_i2c_close(&service->bus);
  if (service->sync_initialized)
    {
      pthread_cond_destroy(&service->control_cond);
      pthread_mutex_destroy(&service->control_lock);
      pthread_mutex_destroy(&service->sample_lock);
      service->sync_initialized = false;
    }

  service->started = false;
  return OK;
}

int environment_service_selftest(void)
{
  if (envmon_i2c_selftest() < 0 || envmon_sht40_selftest() < 0 ||
      envmon_bh1750_selftest() < 0 || envmon_sgp30_selftest() < 0)
    {
      return -EINVAL;
    }

  return OK;
}
