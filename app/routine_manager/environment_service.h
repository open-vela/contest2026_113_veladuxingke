/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/environment_service.h
 ****************************************************************************/

#ifndef __ENVIRONMENT_SERVICE_H
#define __ENVIRONMENT_SERVICE_H

#include <nuttx/config.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "envmon_bh1750.h"
#include "envmon_i2c.h"
#include "envmon_sgp30.h"
#include "envmon_sht40.h"

#define ENV_METRIC_TEMPERATURE (1u << 0)
#define ENV_METRIC_HUMIDITY    (1u << 1)
#define ENV_METRIC_LIGHT       (1u << 2)
#define ENV_METRIC_ECO2        (1u << 3)
#define ENV_METRIC_TVOC        (1u << 4)
#define ENV_METRIC_ALL         ((1u << 5) - 1)

struct environment_sample_s
{
  int32_t temperature_x100;
  uint16_t humidity_x100;
  uint32_t light_x10;
  uint16_t eco2_ppm;
  uint16_t tvoc_ppb;
  uint32_t valid_mask;
  uint32_t live_mask;
  uint32_t demo_mask;
  uint32_t stale_mask;
  uint32_t warmup_mask;
  uint32_t offline_mask;
  uint32_t sequence;
  struct timespec captured_at;
};

struct environment_service_s
{
  struct envmon_i2c_s bus;
  struct envmon_sht40_s sht40;
  struct envmon_bh1750_s bh1750;
  struct envmon_sgp30_s sgp30;
  struct environment_sample_s sample;
  pthread_t thread;
  pthread_mutex_t sample_lock;
  pthread_mutex_t control_lock;
  pthread_cond_t control_cond;
  struct timespec last_good[5];
  struct timespec next_retry[3];
  uint8_t failures[3];
  uint8_t backoff[3];
  int last_error[3];
  uint8_t last_state[3];
  bool started;
  bool thread_created;
  bool stop;
  bool sync_initialized;
};

int environment_service_start(FAR struct environment_service_s *service,
                              FAR const char *i2c_path);
int environment_service_get_sample(
  FAR struct environment_service_s *service,
  FAR struct environment_sample_s *sample);
int environment_service_stop(FAR struct environment_service_s *service);
int environment_service_selftest(void);

#endif /* __ENVIRONMENT_SERVICE_H */
