/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/routine_model.h
 ****************************************************************************/

#ifndef __ROUTINE_MODEL_H
#define __ROUTINE_MODEL_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include "environment_service.h"

#define ROUTINE_TREND_POINTS 24
#define ROUTINE_HISTORY_WINDOW 6
#define ROUTINE_HISTORY_INVALID INT32_MIN
#define ROUTINE_SCHEDULE_MAX 3
#define ROUTINE_DUE_QUEUE_MAX ROUTINE_SCHEDULE_MAX
#define ROUTINE_NO_SCHEDULE UINT8_MAX
#define ROUTINE_REMINDER_LIMIT 3
#define ROUTINE_CLOCK_REBASED 1
#define ROUTINE_CLOCK_REBASE_SECONDS 90
#define ROUTINE_SCHEDULE_TEXT_MAX 48
#define ROUTINE_DEFAULT_TEMPERATURE_LOW_X100 1600
#define ROUTINE_DEFAULT_TEMPERATURE_HIGH_X100 3000
#define ROUTINE_HISTORY_INTERVAL_MIN_SECONDS 5
#define ROUTINE_HISTORY_INTERVAL_MAX_SECONDS 3600
#define ROUTINE_DEFAULT_HISTORY_INTERVAL_SECONDS 60

#define ROUTINE_SCHEDULE_ID_DRINK 0
#define ROUTINE_SCHEDULE_ID_FOCUS 1
#define ROUTINE_SCHEDULE_ID_VENTILATE 2

#define ROUTINE_CLOCK_MIN_VALID_DAY 0

enum routine_text_id_e
{
  ROUTINE_TEXT_DRINK = 0,
  ROUTINE_TEXT_FOCUS,
  ROUTINE_TEXT_VENTILATE
};

enum routine_clock_mode_e
{
  ROUTINE_CLOCK_LIVE = 0,
  ROUTINE_CLOCK_DEMO
};

enum routine_schedule_action_e
{
  ROUTINE_SCHEDULE_COMPLETE = 0,
  ROUTINE_SCHEDULE_DELAY_10_MINUTES,
  ROUTINE_SCHEDULE_DISMISS
};

struct routine_clock_s
{
  int32_t day;
  uint8_t hour;
  uint8_t minute;
  bool valid;
  uint8_t second;
};

struct routine_schedule_setting_s
{
  uint8_t id;
  uint8_t hour;
  uint8_t minute;
  char text[ROUTINE_SCHEDULE_TEXT_MAX + 1];
};

struct routine_settings_s
{
  uint32_t generation;
  int32_t temperature_low_x100;
  int32_t temperature_high_x100;
  uint32_t history_interval_seconds;
  struct routine_schedule_setting_s schedule[ROUTINE_SCHEDULE_MAX];
};

struct routine_schedule_item_s
{
  int32_t due_day;
  uint8_t id;
  uint8_t hour;
  uint8_t minute;
  uint8_t text_id;
  char text[ROUTINE_SCHEDULE_TEXT_MAX + 1];
  uint8_t reminder_count;
  uint32_t active_due_sequence;
  bool completed;
  bool notified;
  bool carry_over;
};

struct routine_due_event_s
{
  uint32_t sequence;
  int32_t due_day;
  uint8_t schedule_id;
  uint8_t text_id;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t reminder_count;
  char text[ROUTINE_SCHEDULE_TEXT_MAX + 1];
};

struct routine_snapshot_s
{
  int32_t temperature_x100;
  uint16_t humidity_x100;
  uint32_t light_x10;
  uint16_t eco2_ppm;
  uint16_t tvoc_ppb;
  int32_t temperature_history[ROUTINE_TREND_POINTS];
  uint8_t temperature_history_hour[ROUTINE_TREND_POINTS];
  uint8_t temperature_history_minute[ROUTINE_TREND_POINTS];
  uint8_t temperature_history_second[ROUTINE_TREND_POINTS];
  uint8_t temperature_history_count;
  struct routine_schedule_item_s schedule[ROUTINE_SCHEDULE_MAX];
  uint32_t live_mask;
  uint32_t demo_mask;
  uint32_t stale_mask;
  uint32_t warmup_mask;
  uint32_t offline_mask;
  uint32_t schedule_due_sequence;
  uint32_t config_generation;
  int32_t temperature_low_x100;
  int32_t temperature_high_x100;
  uint32_t history_interval_seconds;
  int32_t day;
  uint8_t schedule_count;
  uint8_t next_schedule;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  bool clock_valid;
};

struct routine_model_s
{
  struct routine_snapshot_s snapshot;
  struct routine_settings_s settings;
  struct routine_due_event_s due_queue[ROUTINE_DUE_QUEUE_MAX];
  uint32_t elapsed_seconds;
  int64_t temperature_history_slot;
  uint8_t due_head;
  uint8_t due_count;
  uint8_t due_queued_mask;
  uint8_t seconds_per_hour;
  enum routine_clock_mode_e clock_mode;
  bool environment_attached;
  bool temperature_history_slot_valid;
};

int routine_model_init_live(struct routine_model_s *model,
                            const struct routine_clock_s *clock);
void routine_model_init_live_pending(struct routine_model_s *model);
void routine_model_init_demo(struct routine_model_s *model,
                             uint8_t seconds_per_hour);
void routine_model_tick_demo(struct routine_model_s *model);
int routine_model_update_clock(struct routine_model_s *model,
                               const struct routine_clock_s *clock);
int routine_model_apply_settings(
  struct routine_model_s *model,
  FAR const struct routine_settings_s *settings);
void routine_model_apply_environment(
  struct routine_model_s *model,
  FAR const struct environment_sample_s *environment);
int routine_model_schedule_action(struct routine_model_s *model,
                                  uint8_t index,
                                  enum routine_schedule_action_e action);
int routine_model_schedule_action_id(struct routine_model_s *model,
                                     uint8_t schedule_id,
                                     enum routine_schedule_action_e action);
int routine_model_apply_schedule_action(
  struct routine_model_s *model, uint8_t schedule_id,
  uint32_t expected_due_sequence, enum routine_schedule_action_e action,
  struct routine_due_event_s *ended_due);
void routine_model_clear_due(struct routine_model_s *model);
int routine_model_take_due(struct routine_model_s *model,
                           struct routine_due_event_s *event);
const struct routine_snapshot_s *
routine_model_get_snapshot(const struct routine_model_s *model);
int routine_model_selftest(void);
bool routine_history_interval_valid(uint32_t interval_seconds);

#endif /* __ROUTINE_MODEL_H */
