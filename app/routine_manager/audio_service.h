/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/audio_service.h
 ****************************************************************************/

#ifndef __AUDIO_SERVICE_H
#define __AUDIO_SERVICE_H

#include <nuttx/config.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "voice_reply.h"

struct nxplayer_s;
struct nxrecorder_s;

#define AUDIO_SERVICE_REMINDER_MAX 3

enum audio_service_state_e
{
  AUDIO_SERVICE_IDLE = 0,
  AUDIO_SERVICE_LISTENING,
  AUDIO_SERVICE_PROCESSING,
  AUDIO_SERVICE_SPEAKING,
  AUDIO_SERVICE_ERROR
};

enum audio_service_error_e
{
  AUDIO_SERVICE_ERROR_NONE = 0,
  AUDIO_SERVICE_ERROR_CAPTURE,
  AUDIO_SERVICE_ERROR_NO_VOICE,
  AUDIO_SERVICE_ERROR_REPLY_RESOURCE,
  AUDIO_SERVICE_ERROR_PLAYBACK,
  AUDIO_SERVICE_ERROR_SENSOR_UNAVAILABLE
};

enum audio_service_outcome_e
{
  AUDIO_SERVICE_OUTCOME_NONE = 0,
  AUDIO_SERVICE_OUTCOME_COMPLETED,
  AUDIO_SERVICE_OUTCOME_CANCELLED,
  AUDIO_SERVICE_OUTCOME_FAILED
};

enum audio_service_request_e
{
  AUDIO_SERVICE_REQUEST_NONE = 0,
  AUDIO_SERVICE_REQUEST_QUERY,
  AUDIO_SERVICE_REQUEST_TEMPERATURE,
  AUDIO_SERVICE_REQUEST_HUMIDITY,
  AUDIO_SERVICE_REQUEST_BH1750,
  AUDIO_SERVICE_REQUEST_SGP30,
  AUDIO_SERVICE_REQUEST_TEMPERATURE_LOW_WARNING,
  AUDIO_SERVICE_REQUEST_TEMPERATURE_HIGH_WARNING,
  AUDIO_SERVICE_REQUEST_SCHEDULE_REMINDER
};

struct audio_service_reminder_s
{
  uint32_t sequence;
  uint8_t schedule_id;
  uint8_t text_id;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t attempts;
};

struct audio_service_config_s
{
  FAR const char *capture_device;
  FAR const char *playback_device;
  FAR const char *record_path;
  FAR const char *reply_path;
  FAR const char *asset_dir;
  uint32_t record_ms;
  uint16_t volume;
  int priority;
  size_t stack_size;
};

struct audio_service_sensor_values_s
{
  struct voice_reply_values_s values;
  uint32_t live_mask;
  uint32_t warmup_mask;
  uint32_t offline_mask;
};

struct audio_service_operation_s
{
  uint32_t id;
  enum audio_service_request_e request;
  struct voice_reply_values_s values;
  struct audio_service_reminder_s reminder;
};

struct audio_service_result_s
{
  enum audio_service_error_e error;
  enum audio_service_outcome_e outcome;
};

struct audio_service_snapshot_s
{
  enum audio_service_state_e state;
  enum audio_service_error_e error;
  enum audio_service_outcome_e outcome;
  enum audio_service_request_e request;
  enum audio_service_request_e result_request;
  uint32_t sequence;
  uint32_t operation_id;
  uint8_t pending_reminders;
  struct audio_service_reminder_s reminder;
  struct voice_reply_values_s values;
  uint16_t recognition_score;
  bool semantic_verified;
  bool has_result;
};

struct audio_service_s
{
  pthread_t thread;
  pthread_mutex_t lock;
  pthread_cond_t cond;
  struct audio_service_config_s config;
  struct nxrecorder_s *recorder;
  struct nxplayer_s *player;
  enum audio_service_state_e state;
  enum audio_service_error_e error;
  enum audio_service_outcome_e outcome;
  enum audio_service_request_e request;
  enum audio_service_request_e result_request;
  struct audio_service_sensor_values_s sensor;
  struct audio_service_operation_s pending_operation;
  struct audio_service_operation_s active;
  struct audio_service_reminder_s current_reminder;
  struct audio_service_reminder_s reminder_queue[AUDIO_SERVICE_REMINDER_MAX];
  uint8_t reminder_head;
  uint8_t reminder_count;
  uint32_t generation;
  uint32_t active_operation;
  uint32_t cancelled_operation;
  uint32_t sequence;
  uint16_t recognition_score;
  bool semantic_verified;
  bool has_result;
  bool initialized;
  bool thread_created;
  bool start_requested;
  bool worker_active;
  bool shutdown_requested;
};

int audio_service_initialize(FAR struct audio_service_s *service,
                             FAR const struct audio_service_config_s *config);
int audio_service_begin(FAR struct audio_service_s *service);
int audio_service_play_sensor(FAR struct audio_service_s *service,
                              enum audio_service_request_e request);
int audio_service_play_temperature_warning(
  FAR struct audio_service_s *service, bool high);
int audio_service_enqueue_reminder(
  FAR struct audio_service_s *service,
  FAR const struct audio_service_reminder_s *reminder);
int audio_service_cancel(FAR struct audio_service_s *service);
int audio_service_cancel_reminder(
  FAR struct audio_service_s *service,
  FAR const struct audio_service_reminder_s *reminder);
int audio_service_clear_reminders(FAR struct audio_service_s *service);
int audio_service_update_sensors(
  FAR struct audio_service_s *service,
  FAR const struct audio_service_sensor_values_s *sensor);
int audio_service_get_snapshot(
  FAR struct audio_service_s *service,
  FAR struct audio_service_snapshot_s *snapshot);
int audio_service_shutdown(FAR struct audio_service_s *service);
int audio_service_selftest(void);

#endif /* __AUDIO_SERVICE_H */
