/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/audio_service.c
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/audio/audio.h>

#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <system/nxplayer.h>
#include <system/nxrecorder.h>

#include "audio_service.h"
#include "environment_service.h"
#include "voice_recognizer.h"

#define AUDIO_SERVICE_CAPTURE_CHANNELS  1
#define AUDIO_SERVICE_PLAY_CHANNELS     2
#define AUDIO_SERVICE_BITS_PER_SAMPLE   16
#define AUDIO_SERVICE_SAMPLE_RATE       16000
#define AUDIO_SERVICE_CAPTURE_CHMAP     1
#define AUDIO_SERVICE_PLAY_CHMAP        3
#define AUDIO_SERVICE_POLL_MS           20
#define AUDIO_SERVICE_VOLUME_SCALE      10

static void audio_service_publish(FAR struct audio_service_s *service,
                                  uint32_t operation,
                                  enum audio_service_state_e state,
                                  enum audio_service_error_e error)
{
  pthread_mutex_lock(&service->lock);
  if (service->worker_active && service->active_operation == operation &&
      service->cancelled_operation != operation)
    {
      service->state = state;
      service->error = error;
      if (state == AUDIO_SERVICE_ERROR)
        {
          service->outcome = AUDIO_SERVICE_OUTCOME_FAILED;
        }

      service->sequence++;
    }

  pthread_mutex_unlock(&service->lock);
}

static bool audio_service_cancelled(FAR struct audio_service_s *service,
                                    uint32_t operation)
{
  bool cancelled;

  pthread_mutex_lock(&service->lock);
  cancelled = service->shutdown_requested || !service->worker_active ||
              service->active_operation != operation ||
              service->cancelled_operation == operation;
  pthread_mutex_unlock(&service->lock);
  return cancelled;
}

struct audio_service_cancel_context_s
{
  FAR struct audio_service_s *service;
  uint32_t operation;
};

static bool audio_service_reply_cancel(FAR void *arg)
{
  FAR struct audio_service_cancel_context_s *context = arg;

  return audio_service_cancelled(context->service, context->operation);
}

static void audio_service_finish_operation(
  FAR struct audio_service_s *service, uint32_t operation,
  enum audio_service_outcome_e outcome)
{
  pthread_mutex_lock(&service->lock);
  if (service->worker_active && service->active_operation == operation)
    {
      service->error = AUDIO_SERVICE_ERROR_NONE;
      service->outcome = outcome;
      service->sequence++;
      pthread_cond_signal(&service->cond);
    }

  pthread_mutex_unlock(&service->lock);
}

static int audio_service_capture(FAR struct audio_service_s *service,
                                 uint32_t generation)
{
  struct timespec delay;
  uint32_t elapsed;
  int ret;

  service->recorder = nxrecorder_create();
  if (service->recorder == NULL)
    {
      return -ENOMEM;
    }

  ret = nxrecorder_setdevice(service->recorder,
                             service->config.capture_device);
  if (ret < 0)
    {
      nxrecorder_release(service->recorder);
      service->recorder = NULL;
      return ret;
    }

  ret = nxrecorder_recordinternal(service->recorder,
                                  service->config.record_path,
                                  AUDIO_FMT_PCM,
                                  AUDIO_SERVICE_CAPTURE_CHANNELS,
                                  AUDIO_SERVICE_BITS_PER_SAMPLE,
                                  AUDIO_SERVICE_SAMPLE_RATE,
                                  AUDIO_SERVICE_CAPTURE_CHMAP);
  if (ret < 0)
    {
      nxrecorder_release(service->recorder);
      service->recorder = NULL;
      return ret;
    }

  delay.tv_sec = 0;
  delay.tv_nsec = AUDIO_SERVICE_POLL_MS * 1000000;
  for (elapsed = 0; elapsed < service->config.record_ms;
       elapsed += AUDIO_SERVICE_POLL_MS)
    {
      if (audio_service_cancelled(service, generation))
        {
          break;
        }

      nanosleep(&delay, NULL);
    }

  ret = nxrecorder_stop(service->recorder);
  nxrecorder_release(service->recorder);
  service->recorder = NULL;
  return ret;
}

static bool audio_service_reminder_equal(
  FAR const struct audio_service_reminder_s *left,
  FAR const struct audio_service_reminder_s *right)
{
  return left->sequence == right->sequence &&
         left->schedule_id == right->schedule_id;
}

static uint16_t audio_service_nxplayer_volume(uint16_t percent)
{
  return percent * AUDIO_SERVICE_VOLUME_SCALE;
}

static int audio_service_play(FAR struct audio_service_s *service,
                              uint32_t generation,
                              uint32_t duration_ms)
{
  struct nxplayer_status_s status =
  {
    .state = NXPLAYER_STATE_STARTING,
    .result = -EINPROGRESS
  };
  struct timespec deadline;
  uint16_t nxplayer_volume;
  uint32_t elapsed;
  bool cancelled;
  bool timed_out;
  int ret;

  service->player = nxplayer_create();
  if (service->player == NULL)
    {
      return -ENOMEM;
    }

  ret = nxplayer_setdevice(service->player,
                           service->config.playback_device);
  if (ret < 0)
    {
      nxplayer_release(service->player);
      service->player = NULL;
      return ret;
    }

#ifndef CONFIG_AUDIO_EXCLUDE_VOLUME
  nxplayer_volume = audio_service_nxplayer_volume(service->config.volume);
  syslog(LOG_INFO,
         "routine_mgr: playback volume=%u%% nxplayer_volume=%u\n",
         service->config.volume, nxplayer_volume);
  ret = nxplayer_setvolume(service->player, nxplayer_volume);
  if (ret < 0)
    {
      nxplayer_release(service->player);
      service->player = NULL;
      return ret;
    }
#endif

  if (audio_service_cancelled(service, generation))
    {
      nxplayer_release(service->player);
      service->player = NULL;
      return -ECANCELED;
    }

  ret = nxplayer_playraw(service->player, service->config.reply_path,
                         AUDIO_FMT_PCM, AUDIO_FMT_UNDEF,
                         AUDIO_SERVICE_PLAY_CHANNELS,
                         AUDIO_SERVICE_BITS_PER_SAMPLE,
                         AUDIO_SERVICE_SAMPLE_RATE,
                         AUDIO_SERVICE_PLAY_CHMAP);
  if (ret < 0)
    {
      nxplayer_release(service->player);
      service->player = NULL;
      return ret;
    }

  cancelled = false;
  timed_out = true;
  for (elapsed = 0; elapsed <= duration_ms + 1000;
       elapsed += AUDIO_SERVICE_POLL_MS)
    {
      cancelled = audio_service_cancelled(service, generation);
      ret = nxplayer_getstatus(service->player, &status);
      if (ret < 0)
        {
          break;
        }

      if (cancelled || status.state == NXPLAYER_STATE_IDLE)
        {
          timed_out = false;
          break;
        }

      clock_gettime(CLOCK_REALTIME, &deadline);
      deadline.tv_nsec += AUDIO_SERVICE_POLL_MS * 1000000;
      if (deadline.tv_nsec >= 1000000000)
        {
          deadline.tv_sec++;
          deadline.tv_nsec -= 1000000000;
        }

      pthread_mutex_lock(&service->lock);
      if (!service->shutdown_requested &&
          service->cancelled_operation != generation)
        {
          pthread_cond_timedwait(&service->cond, &service->lock, &deadline);
        }

      pthread_mutex_unlock(&service->lock);
    }

  if (cancelled || timed_out || status.state != NXPLAYER_STATE_IDLE)
    {
      syslog(LOG_INFO, "routine_mgr: stopping playback (%s)\n",
             cancelled ? "cancelled" : "timeout");
      ret = nxplayer_stop(service->player);
      if (ret < 0)
        {
          nxplayer_release(service->player);
          service->player = NULL;
          return ret;
        }
    }

  ret = nxplayer_getstatus(service->player, &status);
  if (ret >= 0)
    {
      ret = nxplayer_wait(service->player);
    }

  nxplayer_release(service->player);
  service->player = NULL;
  if (cancelled)
    {
      return -ECANCELED;
    }

  if (timed_out)
    {
      return -ETIMEDOUT;
    }

  return ret < 0 ? ret : status.result;
}

static int audio_service_sensor_request_spec(
  enum audio_service_request_e request, FAR uint32_t *required,
  FAR enum voice_reply_type_e *reply_type)
{
  switch (request)
    {
      case AUDIO_SERVICE_REQUEST_TEMPERATURE:
        if (required != NULL)
          {
            *required = ENV_METRIC_TEMPERATURE;
          }

        if (reply_type != NULL)
          {
            *reply_type = VOICE_REPLY_TEMPERATURE;
          }

        return OK;

      case AUDIO_SERVICE_REQUEST_HUMIDITY:
        if (required != NULL)
          {
            *required = ENV_METRIC_HUMIDITY;
          }

        if (reply_type != NULL)
          {
            *reply_type = VOICE_REPLY_HUMIDITY;
          }

        return OK;

      case AUDIO_SERVICE_REQUEST_BH1750:
        if (required != NULL)
          {
            *required = ENV_METRIC_LIGHT;
          }

        if (reply_type != NULL)
          {
            *reply_type = VOICE_REPLY_BH1750;
          }

        return OK;

      case AUDIO_SERVICE_REQUEST_SGP30:
        if (required != NULL)
          {
            *required = ENV_METRIC_ECO2 | ENV_METRIC_TVOC;
          }

        if (reply_type != NULL)
          {
            *reply_type = VOICE_REPLY_SGP30;
          }

        return OK;

      default:
        return -EINVAL;
    }
}

static int audio_service_query_request_spec(
  enum voice_command_e command, FAR enum audio_service_request_e *request,
  FAR uint32_t *required, FAR enum voice_reply_type_e *reply_type)
{
  switch (command)
    {
      case VOICE_COMMAND_QUERY_TEMPERATURE:
        *request = AUDIO_SERVICE_REQUEST_TEMPERATURE;
        break;
      case VOICE_COMMAND_QUERY_HUMIDITY:
        *request = AUDIO_SERVICE_REQUEST_HUMIDITY;
        break;
      case VOICE_COMMAND_QUERY_LIGHT:
        *request = AUDIO_SERVICE_REQUEST_BH1750;
        break;
      case VOICE_COMMAND_QUERY_AIR_QUALITY:
        *request = AUDIO_SERVICE_REQUEST_SGP30;
        break;
      default:
        return -ENODATA;
    }

  return audio_service_sensor_request_spec(*request, required, reply_type);
}

static void audio_service_read_sensors(
  FAR struct audio_service_s *service,
  FAR struct audio_service_sensor_values_s *sensor)
{
  pthread_mutex_lock(&service->lock);
  *sensor = service->sensor;
  pthread_mutex_unlock(&service->lock);
}

static void audio_service_store_recognition(
  FAR struct audio_service_s *service,
  FAR const struct voice_recognition_result_s *recognition,
  enum audio_service_request_e result_request)
{
  pthread_mutex_lock(&service->lock);
  service->recognition_score = recognition->score;
  service->semantic_verified = recognition->semantic_verified;
  service->result_request = result_request;
  service->has_result = true;
  service->sequence++;
  pthread_mutex_unlock(&service->lock);
}

static int audio_service_speak_reply(
  FAR struct audio_service_s *service, uint32_t generation,
  enum voice_reply_type_e reply_type,
  FAR const struct voice_reply_values_s *values,
  FAR struct audio_service_cancel_context_s *cancel_context)
{
  uint32_t duration_ms;
  int ret;

  audio_service_publish(service, generation, AUDIO_SERVICE_PROCESSING,
                        AUDIO_SERVICE_ERROR_NONE);
  ret = voice_reply_build_cancelable(service->config.asset_dir,
                                     service->config.reply_path,
                                     reply_type, values, &duration_ms,
                                     audio_service_reply_cancel,
                                     cancel_context);
  if (ret < 0)
    {
      if (ret == -ECANCELED || audio_service_cancelled(service, generation))
        {
          audio_service_finish_operation(service, generation,
                                         AUDIO_SERVICE_OUTCOME_CANCELLED);
          return -ECANCELED;
        }

      syslog(LOG_ERR, "routine_mgr: reply type=%u build failed: %d\n",
             reply_type, ret);
      audio_service_publish(service, generation, AUDIO_SERVICE_ERROR,
                            AUDIO_SERVICE_ERROR_REPLY_RESOURCE);
      return ret;
    }

  if (audio_service_cancelled(service, generation))
    {
      unlink(service->config.reply_path);
      audio_service_finish_operation(service, generation,
                                     AUDIO_SERVICE_OUTCOME_CANCELLED);
      return -ECANCELED;
    }

  audio_service_publish(service, generation, AUDIO_SERVICE_SPEAKING,
                        AUDIO_SERVICE_ERROR_NONE);
  ret = audio_service_play(service, generation, duration_ms);
  unlink(service->config.reply_path);
  if (audio_service_cancelled(service, generation) || ret == -ECANCELED)
    {
      audio_service_finish_operation(service, generation,
                                     AUDIO_SERVICE_OUTCOME_CANCELLED);
      return -ECANCELED;
    }

  if (ret < 0)
    {
      audio_service_publish(service, generation, AUDIO_SERVICE_ERROR,
                            AUDIO_SERVICE_ERROR_PLAYBACK);
      return ret;
    }

  return OK;
}

static void audio_service_process(
  FAR struct audio_service_s *service,
  FAR const struct audio_service_operation_s *operation)
{
  struct audio_service_cancel_context_s cancel_context;
  struct audio_service_sensor_values_s sensor;
  struct voice_recognition_result_s recognition;
  struct voice_reply_values_s values;
  enum audio_service_request_e request;
  uint32_t generation;
  enum voice_reply_type_e reply_type;
  uint32_t required;
  int ret;

  generation = operation->id;
  request = operation->request;
  values = operation->values;
  cancel_context.service = service;
  cancel_context.operation = generation;

  if (request != AUDIO_SERVICE_REQUEST_QUERY)
    {
      if (request == AUDIO_SERVICE_REQUEST_SCHEDULE_REMINDER)
        {
          required = 0;
          reply_type = VOICE_REPLY_SCHEDULE_REMINDER;
          ret = OK;
        }
      else if (request == AUDIO_SERVICE_REQUEST_TEMPERATURE_LOW_WARNING ||
               request == AUDIO_SERVICE_REQUEST_TEMPERATURE_HIGH_WARNING)
        {
          required = 0;
          reply_type = request == AUDIO_SERVICE_REQUEST_TEMPERATURE_LOW_WARNING ?
                       VOICE_REPLY_TEMPERATURE_LOW_WARNING :
                       VOICE_REPLY_TEMPERATURE_HIGH_WARNING;
          ret = OK;
        }
      else
        {
          ret = audio_service_sensor_request_spec(request, &required,
                                                  &reply_type);
        }

      if (ret < 0)
        {
          audio_service_publish(service, generation, AUDIO_SERVICE_ERROR,
                                AUDIO_SERVICE_ERROR_REPLY_RESOURCE);
          return;
        }

      ret = audio_service_speak_reply(service, generation, reply_type, &values,
                                      &cancel_context);
      if (ret == OK)
        {
          audio_service_finish_operation(service, generation,
                                         AUDIO_SERVICE_OUTCOME_COMPLETED);
        }
      return;
    }

  audio_service_publish(service, generation, AUDIO_SERVICE_LISTENING,
                        AUDIO_SERVICE_ERROR_NONE);
  ret = audio_service_capture(service, generation);
  if (audio_service_cancelled(service, generation))
    {
      unlink(service->config.record_path);
      audio_service_finish_operation(service, generation, AUDIO_SERVICE_OUTCOME_CANCELLED);
      return;
    }

  if (ret < 0)
    {
      audio_service_publish(service, generation, AUDIO_SERVICE_ERROR,
                            AUDIO_SERVICE_ERROR_CAPTURE);
      return;
    }

  audio_service_publish(service, generation, AUDIO_SERVICE_PROCESSING,
                        AUDIO_SERVICE_ERROR_NONE);
  ret = voice_recognizer_recognize(service->config.record_path,
                                   &recognition);
  unlink(service->config.record_path);
  if (audio_service_cancelled(service, generation))
    {
      audio_service_finish_operation(service, generation, AUDIO_SERVICE_OUTCOME_CANCELLED);
      return;
    }

  if (ret < 0 || audio_service_query_request_spec(
                   recognition.command, &request, &required, &reply_type) < 0)
    {
      audio_service_publish(service, generation, AUDIO_SERVICE_ERROR,
                            AUDIO_SERVICE_ERROR_NO_VOICE);
      return;
    }

  audio_service_read_sensors(service, &sensor);
  if ((sensor.offline_mask & required) != 0 ||
      (sensor.warmup_mask & required) != 0 ||
      (sensor.live_mask & required) != required)
    {
      audio_service_publish(service, generation, AUDIO_SERVICE_ERROR,
                            AUDIO_SERVICE_ERROR_SENSOR_UNAVAILABLE);
      return;
    }

  values = sensor.values;
  audio_service_store_recognition(service, &recognition, request);
  ret = audio_service_speak_reply(service, generation, reply_type, &values,
                                  &cancel_context);
  if (ret == OK)
    {
      audio_service_finish_operation(service, generation,
                                     AUDIO_SERVICE_OUTCOME_COMPLETED);
    }
}

static void audio_service_claim_locked(FAR struct audio_service_s *service)
{
  FAR struct audio_service_operation_s *operation = &service->active;

  memset(operation, 0, sizeof(*operation));
  if (service->reminder_count > 0)
    {
      operation->reminder = service->reminder_queue[service->reminder_head];
      service->reminder_head =
        (service->reminder_head + 1) % AUDIO_SERVICE_REMINDER_MAX;
      service->reminder_count--;
      operation->request = AUDIO_SERVICE_REQUEST_SCHEDULE_REMINDER;
      operation->values = service->sensor.values;
      operation->values.schedule_text_id = operation->reminder.text_id;
      operation->values.schedule_hour = operation->reminder.hour;
      operation->values.schedule_minute = operation->reminder.minute;
      operation->values.schedule_second = operation->reminder.second;
      service->current_reminder = operation->reminder;
      service->generation++;
      operation->id = service->generation;
    }
  else
    {
      *operation = service->pending_operation;
      memset(&service->pending_operation, 0,
             sizeof(service->pending_operation));
      service->start_requested = false;
    }

  service->worker_active = true;
  service->active_operation = operation->id;
  service->request = operation->request;
  if (operation->request != AUDIO_SERVICE_REQUEST_QUERY)
    {
      service->result_request = AUDIO_SERVICE_REQUEST_NONE;
      service->has_result = false;
    }

  service->state = operation->request == AUDIO_SERVICE_REQUEST_QUERY ?
                   AUDIO_SERVICE_LISTENING : AUDIO_SERVICE_PROCESSING;
  service->error = AUDIO_SERVICE_ERROR_NONE;
  service->outcome = AUDIO_SERVICE_OUTCOME_NONE;
  service->sequence++;
  syslog(LOG_INFO,
         "routine_mgr: audio claim op=%lu request=%u reminder=%u/%lu pending=%u\n",
         (unsigned long)operation->id, operation->request,
         operation->reminder.schedule_id,
         (unsigned long)operation->reminder.sequence,
         service->reminder_count);
}

static FAR void *audio_service_thread(FAR void *arg)
{
  FAR struct audio_service_s *service = arg;

  while (true)
    {
      struct audio_service_operation_s operation;

      pthread_mutex_lock(&service->lock);
      while (!service->start_requested && service->reminder_count == 0 &&
             !service->shutdown_requested)
        {
          service->worker_active = false;
          service->active_operation = 0;
          memset(&service->active, 0, sizeof(service->active));
          memset(&service->current_reminder, 0,
                 sizeof(service->current_reminder));
          service->request = AUDIO_SERVICE_REQUEST_NONE;
          if (service->state != AUDIO_SERVICE_IDLE)
            {
              service->state = AUDIO_SERVICE_IDLE;
              service->sequence++;
            }

          pthread_cond_wait(&service->cond, &service->lock);
        }

      if (service->shutdown_requested)
        {
          pthread_mutex_unlock(&service->lock);
          break;
        }

      /* Due reminders have priority immediately after the current operation.
       * The transition is atomic and never exposes an intermediate IDLE.
       */

      audio_service_claim_locked(service);
      operation = service->active;
      pthread_mutex_unlock(&service->lock);
      audio_service_process(service, &operation);

      pthread_mutex_lock(&service->lock);
      if (service->active_operation == operation.id)
        {
          syslog(LOG_INFO,
                 "routine_mgr: audio finish op=%lu outcome=%u error=%u pending=%u\n",
                 (unsigned long)operation.id, service->outcome,
                 service->error, service->reminder_count);
          if (service->cancelled_operation == operation.id)
            {
              service->cancelled_operation = 0;
            }
          else if (operation.request == AUDIO_SERVICE_REQUEST_SCHEDULE_REMINDER &&
                   service->outcome == AUDIO_SERVICE_OUTCOME_FAILED &&
                   operation.reminder.attempts == 0 &&
                   service->reminder_count < AUDIO_SERVICE_REMINDER_MAX)
            {
              struct audio_service_reminder_s queue[AUDIO_SERVICE_REMINDER_MAX];
              uint8_t i;

              operation.reminder.attempts = 1;
              queue[0] = operation.reminder;
              for (i = 0; i < service->reminder_count; i++)
                {
                  uint8_t index = (service->reminder_head + i) %
                                  AUDIO_SERVICE_REMINDER_MAX;
                  queue[i + 1] = service->reminder_queue[index];
                }

              memcpy(service->reminder_queue, queue,
                     (service->reminder_count + 1) * sizeof(queue[0]));
              service->reminder_head = 0;
              service->reminder_count++;
              syslog(LOG_WARNING,
                     "routine_mgr: retry reminder=%u/%lu error=%u\n",
                     operation.reminder.schedule_id,
                     (unsigned long)operation.reminder.sequence,
                     service->error);
            }

          service->worker_active = false;
          service->active_operation = 0;
          memset(&service->active, 0, sizeof(service->active));
          service->sequence++;
        }

      pthread_mutex_unlock(&service->lock);
    }

  return NULL;
}

int audio_service_initialize(FAR struct audio_service_s *service,
                             FAR const struct audio_service_config_s *config)
{
  struct sched_param sched;
  pthread_attr_t attr;
  int ret;

  if (service == NULL || config == NULL || config->capture_device == NULL ||
      config->playback_device == NULL || config->record_path == NULL ||
      config->reply_path == NULL || config->asset_dir == NULL ||
      config->record_ms == 0 || config->volume < 1 || config->volume > 100)
    {
      return -EINVAL;
    }

  memset(service, 0, sizeof(*service));
  service->config = *config;
  service->state = AUDIO_SERVICE_IDLE;
  ret = pthread_mutex_init(&service->lock, NULL);
  if (ret != 0)
    {
      return -ret;
    }

  ret = pthread_cond_init(&service->cond, NULL);
  if (ret != 0)
    {
      pthread_mutex_destroy(&service->lock);
      return -ret;
    }

  pthread_attr_init(&attr);
  if (config->stack_size > 0)
    {
      pthread_attr_setstacksize(&attr, config->stack_size);
    }

  if (config->priority > 0)
    {
      memset(&sched, 0, sizeof(sched));
      sched.sched_priority = config->priority;
      pthread_attr_setschedparam(&attr, &sched);
    }

  ret = pthread_create(&service->thread, &attr, audio_service_thread, service);
  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      pthread_cond_destroy(&service->cond);
      pthread_mutex_destroy(&service->lock);
      return -ret;
    }

  service->thread_created = true;
  service->initialized = true;
  return OK;
}

int audio_service_begin(FAR struct audio_service_s *service)
{
  if (service == NULL || !service->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&service->lock);
  if (service->worker_active || service->start_requested ||
      service->reminder_count > 0)
    {
      pthread_mutex_unlock(&service->lock);
      return -EBUSY;
    }

  service->generation++;
  memset(&service->pending_operation, 0,
         sizeof(service->pending_operation));
  service->pending_operation.id = service->generation;
  service->pending_operation.request = AUDIO_SERVICE_REQUEST_QUERY;
  service->pending_operation.values = service->sensor.values;
  service->request = AUDIO_SERVICE_REQUEST_QUERY;
  service->start_requested = true;
  service->cancelled_operation = 0;
  service->state = AUDIO_SERVICE_LISTENING;
  service->error = AUDIO_SERVICE_ERROR_NONE;
  service->outcome = AUDIO_SERVICE_OUTCOME_NONE;
  service->has_result = false;
  service->result_request = AUDIO_SERVICE_REQUEST_NONE;
  service->recognition_score = 0;
  service->semantic_verified = false;
  service->sequence++;
  pthread_cond_signal(&service->cond);
  pthread_mutex_unlock(&service->lock);
  return OK;
}

int audio_service_play_sensor(FAR struct audio_service_s *service,
                              enum audio_service_request_e request)
{
  uint32_t required;

  if (service == NULL || !service->initialized ||
      audio_service_sensor_request_spec(request, &required, NULL) < 0)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&service->lock);
  if (service->worker_active || service->start_requested ||
      service->reminder_count > 0)
    {
      pthread_mutex_unlock(&service->lock);
      return -EBUSY;
    }

  if ((service->sensor.offline_mask & required) != 0 ||
      (service->sensor.warmup_mask & required) != 0 ||
      (service->sensor.live_mask & required) != required)
    {
      service->request = request;
      service->state = AUDIO_SERVICE_ERROR;
      service->error = AUDIO_SERVICE_ERROR_SENSOR_UNAVAILABLE;
      service->sequence++;
      pthread_mutex_unlock(&service->lock);
      return -ENODATA;
    }

  service->generation++;
  memset(&service->pending_operation, 0,
         sizeof(service->pending_operation));
  service->pending_operation.id = service->generation;
  service->pending_operation.request = request;
  service->pending_operation.values = service->sensor.values;
  service->request = request;
  service->start_requested = true;
  service->cancelled_operation = 0;
  service->state = AUDIO_SERVICE_PROCESSING;
  service->error = AUDIO_SERVICE_ERROR_NONE;
  service->outcome = AUDIO_SERVICE_OUTCOME_NONE;
  service->has_result = false;
  service->result_request = AUDIO_SERVICE_REQUEST_NONE;
  service->sequence++;
  pthread_cond_signal(&service->cond);
  pthread_mutex_unlock(&service->lock);
  return OK;
}

int audio_service_play_temperature_warning(
  FAR struct audio_service_s *service, bool high)
{
  enum audio_service_request_e request =
    high ? AUDIO_SERVICE_REQUEST_TEMPERATURE_HIGH_WARNING :
           AUDIO_SERVICE_REQUEST_TEMPERATURE_LOW_WARNING;

  if (service == NULL || !service->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&service->lock);
  if (service->start_requested ||
      (service->worker_active &&
       service->request != AUDIO_SERVICE_REQUEST_QUERY))
    {
      pthread_mutex_unlock(&service->lock);
      return -EBUSY;
    }

  service->generation++;
  memset(&service->pending_operation, 0,
         sizeof(service->pending_operation));
  service->pending_operation.id = service->generation;
  service->pending_operation.request = request;
  service->pending_operation.values = service->sensor.values;
  service->start_requested = true;
  if (service->worker_active)
    {
      service->cancelled_operation = service->active_operation;
    }
  else
    {
      service->request = request;
      service->state = AUDIO_SERVICE_PROCESSING;
    }

  service->error = AUDIO_SERVICE_ERROR_NONE;
  service->outcome = AUDIO_SERVICE_OUTCOME_NONE;
  service->sequence++;
  pthread_cond_signal(&service->cond);
  pthread_mutex_unlock(&service->lock);
  return OK;
}

int audio_service_enqueue_reminder(
  FAR struct audio_service_s *service,
  FAR const struct audio_service_reminder_s *reminder)
{
  uint8_t tail;
  uint8_t i;

  if (service == NULL || reminder == NULL || !service->initialized ||
      reminder->sequence == 0 ||
      reminder->schedule_id >= AUDIO_SERVICE_REMINDER_MAX ||
      reminder->text_id >= AUDIO_SERVICE_REMINDER_MAX ||
      reminder->hour > 23 || reminder->minute > 59 ||
      reminder->second > 59)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&service->lock);
  for (i = 0; i < service->reminder_count; i++)
    {
      uint8_t index = (service->reminder_head + i) %
                      AUDIO_SERVICE_REMINDER_MAX;
      if (audio_service_reminder_equal(&service->reminder_queue[index],
                                       reminder))
        {
          pthread_mutex_unlock(&service->lock);
          return -EALREADY;
        }
    }

  if (service->worker_active &&
      service->request == AUDIO_SERVICE_REQUEST_SCHEDULE_REMINDER &&
      audio_service_reminder_equal(&service->current_reminder, reminder))
    {
      pthread_mutex_unlock(&service->lock);
      return -EALREADY;
    }

  if (service->reminder_count >= AUDIO_SERVICE_REMINDER_MAX)
    {
      pthread_mutex_unlock(&service->lock);
      return -ENOSPC;
    }

  tail = (service->reminder_head + service->reminder_count) %
         AUDIO_SERVICE_REMINDER_MAX;
  service->reminder_queue[tail] = *reminder;
  service->reminder_count++;
  service->sequence++;
  syslog(LOG_INFO,
         "routine_mgr: audio enqueue reminder=%u/%lu pending=%u active=%lu\n",
         reminder->schedule_id, (unsigned long)reminder->sequence,
         service->reminder_count, (unsigned long)service->active_operation);
  pthread_cond_signal(&service->cond);
  pthread_mutex_unlock(&service->lock);
  return OK;
}

int audio_service_cancel(FAR struct audio_service_s *service)
{
  if (service == NULL || !service->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&service->lock);
  if (service->worker_active)
    {
      service->cancelled_operation = service->active_operation;
      service->sequence++;
    }
  else if (service->start_requested)
    {
      service->start_requested = false;
      memset(&service->pending_operation, 0,
             sizeof(service->pending_operation));
      service->request = AUDIO_SERVICE_REQUEST_NONE;
      service->state = AUDIO_SERVICE_IDLE;
      service->error = AUDIO_SERVICE_ERROR_NONE;
      service->outcome = AUDIO_SERVICE_OUTCOME_CANCELLED;
      service->sequence++;
    }

  pthread_cond_signal(&service->cond);
  pthread_mutex_unlock(&service->lock);
  return OK;
}

int audio_service_cancel_reminder(
  FAR struct audio_service_s *service,
  FAR const struct audio_service_reminder_s *reminder)
{
  struct audio_service_reminder_s queue[AUDIO_SERVICE_REMINDER_MAX];
  uint8_t kept = 0;
  uint8_t i;

  if (service == NULL || reminder == NULL || !service->initialized ||
      reminder->sequence == 0)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&service->lock);
  for (i = 0; i < service->reminder_count; i++)
    {
      uint8_t index = (service->reminder_head + i) %
                      AUDIO_SERVICE_REMINDER_MAX;
      if (!audio_service_reminder_equal(&service->reminder_queue[index],
                                        reminder))
        {
          queue[kept++] = service->reminder_queue[index];
        }
    }

  memset(service->reminder_queue, 0, sizeof(service->reminder_queue));
  memcpy(service->reminder_queue, queue, kept * sizeof(queue[0]));
  service->reminder_head = 0;
  service->reminder_count = kept;
  if (service->worker_active &&
      service->request == AUDIO_SERVICE_REQUEST_SCHEDULE_REMINDER &&
      audio_service_reminder_equal(&service->current_reminder, reminder))
    {
      service->cancelled_operation = service->active_operation;
      service->sequence++;
    }

  pthread_cond_signal(&service->cond);
  pthread_mutex_unlock(&service->lock);
  return OK;
}

int audio_service_clear_reminders(FAR struct audio_service_s *service)
{
  if (service == NULL || !service->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&service->lock);
  memset(service->reminder_queue, 0, sizeof(service->reminder_queue));
  service->reminder_head = 0;
  service->reminder_count = 0;
  if (service->worker_active &&
      service->request == AUDIO_SERVICE_REQUEST_SCHEDULE_REMINDER)
    {
      service->cancelled_operation = service->active_operation;
      service->sequence++;
    }

  pthread_cond_signal(&service->cond);
  pthread_mutex_unlock(&service->lock);
  return OK;
}

int audio_service_update_sensors(
  FAR struct audio_service_s *service,
  FAR const struct audio_service_sensor_values_s *sensor)
{
  if (service == NULL || sensor == NULL || !service->initialized ||
      sensor->values.temperature_x100 < -4500 ||
      sensor->values.temperature_x100 > 13000 ||
      sensor->values.humidity_x100 > 10000)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&service->lock);
  service->sensor = *sensor;
  pthread_mutex_unlock(&service->lock);
  return OK;
}

int audio_service_get_snapshot(
  FAR struct audio_service_s *service,
  FAR struct audio_service_snapshot_s *snapshot)
{
  if (service == NULL || snapshot == NULL || !service->initialized)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&service->lock);
  snapshot->state = service->state;
  snapshot->error = service->error;
  snapshot->outcome = service->outcome;
  snapshot->sequence = service->sequence;
  snapshot->request = service->request;
  snapshot->result_request = service->result_request;
  snapshot->operation_id = service->active_operation;
  snapshot->pending_reminders = service->reminder_count;
  snapshot->reminder = service->current_reminder;
  snapshot->values = service->sensor.values;
  snapshot->recognition_score = service->recognition_score;
  snapshot->semantic_verified = service->semantic_verified;
  snapshot->has_result = service->has_result;
  pthread_mutex_unlock(&service->lock);
  return OK;
}

int audio_service_shutdown(FAR struct audio_service_s *service)
{
  if (service == NULL || !service->initialized)
    {
      return OK;
    }

  pthread_mutex_lock(&service->lock);
  service->shutdown_requested = true;
  if (service->worker_active)
    {
      service->cancelled_operation = service->active_operation;
      service->sequence++;
    }

  service->start_requested = false;
  memset(&service->pending_operation, 0,
         sizeof(service->pending_operation));
  pthread_cond_signal(&service->cond);
  pthread_mutex_unlock(&service->lock);

  if (service->thread_created)
    {
      pthread_join(service->thread, NULL);
    }

  unlink(service->config.record_path);
  unlink(service->config.reply_path);
  pthread_cond_destroy(&service->cond);
  pthread_mutex_destroy(&service->lock);
  memset(service, 0, sizeof(*service));
  return OK;
}

int audio_service_selftest(void)
{
  struct audio_service_reminder_s reminder;
  struct audio_service_s service;
  enum audio_service_request_e request;
  uint32_t required;
  enum voice_reply_type_e reply;

  memset(&service, 0, sizeof(service));
  memset(&reminder, 0, sizeof(reminder));
  service.sensor.values.temperature_x100 = 2350;
  service.reminder_queue[0].sequence = 10;
  service.reminder_queue[0].schedule_id = 1;
  service.reminder_queue[0].text_id = 2;
  service.reminder_queue[0].hour = 9;
  service.reminder_queue[0].minute = 30;
  service.reminder_count = 1;
  service.pending_operation.id = 7;
  service.pending_operation.request = AUDIO_SERVICE_REQUEST_TEMPERATURE;
  service.start_requested = true;
  audio_service_claim_locked(&service);
  reminder = service.active.reminder;

  if (service.active.request != AUDIO_SERVICE_REQUEST_SCHEDULE_REMINDER ||
      service.active.id == 0 || reminder.sequence != 10 ||
      reminder.schedule_id != 1 || service.reminder_count != 0 ||
      !service.start_requested ||
      service.pending_operation.request != AUDIO_SERVICE_REQUEST_TEMPERATURE)
    {
      return -EINVAL;
    }

  service.worker_active = false;
  service.active_operation = 0;
  memset(&service.active, 0, sizeof(service.active));
  audio_service_claim_locked(&service);
  if (service.active.id != 7 ||
      service.active.request != AUDIO_SERVICE_REQUEST_TEMPERATURE ||
      service.start_requested)
    {
      return -EINVAL;
    }

  if (voice_recognizer_selftest() < 0 || voice_reply_selftest() < 0 ||
      audio_service_sensor_request_spec(AUDIO_SERVICE_REQUEST_TEMPERATURE,
                                        &required, &reply) < 0 ||
      required != ENV_METRIC_TEMPERATURE ||
      reply != VOICE_REPLY_TEMPERATURE ||
      audio_service_sensor_request_spec(AUDIO_SERVICE_REQUEST_HUMIDITY,
                                        &required, &reply) < 0 ||
      required != ENV_METRIC_HUMIDITY || reply != VOICE_REPLY_HUMIDITY ||
      audio_service_sensor_request_spec(AUDIO_SERVICE_REQUEST_NONE,
                                        &required, &reply) >= 0 ||
      audio_service_query_request_spec(VOICE_COMMAND_QUERY_TEMPERATURE,
                                       &request, &required, &reply) < 0 ||
      request != AUDIO_SERVICE_REQUEST_TEMPERATURE ||
      required != ENV_METRIC_TEMPERATURE ||
      reply != VOICE_REPLY_TEMPERATURE ||
      audio_service_query_request_spec(VOICE_COMMAND_QUERY_HUMIDITY,
                                       &request, &required, &reply) < 0 ||
      request != AUDIO_SERVICE_REQUEST_HUMIDITY ||
      required != ENV_METRIC_HUMIDITY || reply != VOICE_REPLY_HUMIDITY ||
      audio_service_query_request_spec(VOICE_COMMAND_QUERY_LIGHT,
                                       &request, &required, &reply) < 0 ||
      request != AUDIO_SERVICE_REQUEST_BH1750 ||
      required != ENV_METRIC_LIGHT || reply != VOICE_REPLY_BH1750 ||
      audio_service_query_request_spec(VOICE_COMMAND_QUERY_AIR_QUALITY,
                                       &request, &required, &reply) < 0 ||
      request != AUDIO_SERVICE_REQUEST_SGP30 ||
      required != (ENV_METRIC_ECO2 | ENV_METRIC_TVOC) ||
      reply != VOICE_REPLY_SGP30 ||
      audio_service_nxplayer_volume(1) != 10 ||
      audio_service_nxplayer_volume(40) != 400 ||
      audio_service_nxplayer_volume(100) != 1000)
    {
      return -EINVAL;
    }

  return OK;
}
