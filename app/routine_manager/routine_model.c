/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/routine_model.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "routine_model.h"

static const int32_t g_temperature_profile[ROUTINE_TREND_POINTS] =
{
  2180, 2160, 2140, 2130, 2120, 2140, 2180, 2230,
  2290, 2350, 2400, 2440, 2470, 2490, 2500, 2490,
  2470, 2440, 2400, 2360, 2320, 2280, 2240, 2210
};

static const uint16_t g_humidity_profile[ROUTINE_TREND_POINTS] =
{
  5700, 5800, 5900, 6000, 6000, 5900, 5700, 5500,
  5300, 5100, 4900, 4700, 4600, 4500, 4400, 4500,
  4600, 4800, 5000, 5200, 5400, 5500, 5600, 5700
};

static const uint16_t g_eco2_profile[ROUTINE_TREND_POINTS] =
{
  510, 500, 490, 485, 480, 490, 520, 610,
  720, 820, 910, 960, 890, 840, 880, 940,
  990, 920, 780, 680, 620, 580, 550, 530
};

static const uint16_t g_tvoc_profile[ROUTINE_TREND_POINTS] =
{
  8, 7, 6, 5, 5, 6, 9, 16, 24, 31, 38, 42,
  35, 32, 36, 40, 44, 37, 27, 20, 15, 12, 10, 9
};

static const uint32_t g_light_profile[ROUTINE_TREND_POINTS] =
{
  80, 60, 50, 40, 40, 120, 700, 1800,
  3400, 4800, 5900, 6500, 6900, 6400, 5600, 4500,
  3300, 2100, 1200, 650, 350, 220, 140, 100
};

static const uint8_t g_schedule_hour[ROUTINE_SCHEDULE_MAX] = {9, 14, 18};
static const uint8_t g_schedule_minute[ROUTINE_SCHEDULE_MAX] = {0, 30, 0};
static const char *g_schedule_text[ROUTINE_SCHEDULE_MAX] =
{
  "喝水", "专注", "通风"
};

bool routine_history_interval_valid(uint32_t interval_seconds)
{
  return interval_seconds == 5 || interval_seconds == 10 ||
         (interval_seconds >= 60 &&
          interval_seconds <= ROUTINE_HISTORY_INTERVAL_MAX_SECONDS &&
          interval_seconds % 60 == 0);
}

static int64_t routine_clock_minutes(int32_t day, uint8_t hour,
                                     uint8_t minute)
{
  return (int64_t)day * 24 * 60 + hour * 60 + minute;
}

static int64_t routine_item_minutes(
  const struct routine_schedule_item_s *item)
{
  return routine_clock_minutes(item->due_day, item->hour, item->minute);
}

static void routine_set_default_item(struct routine_model_s *model,
                                     struct routine_schedule_item_s *item,
                                     uint8_t id, int32_t day)
{
  FAR const struct routine_schedule_setting_s *setting =
    &model->settings.schedule[id];

  memset(item, 0, sizeof(*item));
  item->id = id;
  item->text_id = id;
  item->due_day = day;
  item->hour = setting->hour;
  item->minute = setting->minute;
  memcpy(item->text, setting->text, sizeof(item->text));
}

static void routine_sort_schedule(struct routine_snapshot_s *snapshot)
{
  struct routine_schedule_item_s item;
  uint8_t i;
  uint8_t j;

  for (i = 1; i < snapshot->schedule_count; i++)
    {
      item = snapshot->schedule[i];
      j = i;
      while (j > 0 &&
             routine_item_minutes(&snapshot->schedule[j - 1]) >
             routine_item_minutes(&item))
        {
          snapshot->schedule[j] = snapshot->schedule[j - 1];
          j--;
        }

      snapshot->schedule[j] = item;
    }
}

static int routine_find_schedule_id(const struct routine_snapshot_s *snapshot,
                                    uint8_t id)
{
  uint8_t i;

  for (i = 0; i < snapshot->schedule_count; i++)
    {
      if (snapshot->schedule[i].id == id)
        {
          return i;
        }
    }

  return -ENOENT;
}

static void routine_reset_schedule(struct routine_model_s *model,
                                   int32_t day, bool preserve_carry_over)
{
  struct routine_snapshot_s *snapshot = &model->snapshot;
  struct routine_schedule_item_s next[ROUTINE_SCHEDULE_MAX];
  int index;
  uint8_t id;

  for (id = 0; id < ROUTINE_SCHEDULE_MAX; id++)
    {
      index = routine_find_schedule_id(snapshot, id);
      if (preserve_carry_over && index >= 0 &&
          snapshot->schedule[index].carry_over &&
          snapshot->schedule[index].due_day >= day)
        {
          next[id] = snapshot->schedule[index];
        }
      else
        {
          routine_set_default_item(model, &next[id], id, day);
        }
    }

  memcpy(snapshot->schedule, next, sizeof(next));
  snapshot->schedule_count = ROUTINE_SCHEDULE_MAX;
  routine_sort_schedule(snapshot);
}

static uint8_t routine_find_next_schedule(
  const struct routine_snapshot_s *snapshot)
{
  int64_t now = routine_clock_minutes(snapshot->day, snapshot->hour,
                                      snapshot->minute);
  uint8_t i;

  for (i = 0; i < snapshot->schedule_count; i++)
    {
      if (!snapshot->schedule[i].completed &&
          routine_item_minutes(&snapshot->schedule[i]) >= now)
        {
          return i;
        }
    }

  return ROUTINE_NO_SCHEDULE;
}

static void routine_update_fallback_values(struct routine_model_s *model)
{
  struct routine_snapshot_s *snapshot = &model->snapshot;
  uint8_t hour = snapshot->hour;

  if ((snapshot->live_mask & ENV_METRIC_TEMPERATURE) == 0)
    {
      snapshot->temperature_x100 = g_temperature_profile[hour];
    }

  if ((snapshot->live_mask & ENV_METRIC_HUMIDITY) == 0)
    {
      snapshot->humidity_x100 = g_humidity_profile[hour];
    }

  if ((snapshot->live_mask & ENV_METRIC_LIGHT) == 0)
    {
      snapshot->light_x10 = g_light_profile[hour];
    }

  if ((snapshot->live_mask & ENV_METRIC_ECO2) == 0 &&
      (snapshot->warmup_mask & ENV_METRIC_ECO2) == 0)
    {
      snapshot->eco2_ppm = g_eco2_profile[hour];
      snapshot->tvoc_ppb = g_tvoc_profile[hour];
    }

  snapshot->next_schedule = snapshot->clock_valid ?
                            routine_find_next_schedule(snapshot) :
                            ROUTINE_NO_SCHEDULE;
}

static void routine_history_append(struct routine_snapshot_s *snapshot,
                                   int32_t temperature, uint8_t hour,
                                   uint8_t minute, uint8_t second)
{
  uint8_t index;

  if (snapshot->temperature_history_count < ROUTINE_TREND_POINTS)
    {
      index = snapshot->temperature_history_count++;
    }
  else
    {
      memmove(&snapshot->temperature_history[0],
              &snapshot->temperature_history[1],
              sizeof(snapshot->temperature_history[0]) *
              (ROUTINE_TREND_POINTS - 1));
      memmove(&snapshot->temperature_history_hour[0],
              &snapshot->temperature_history_hour[1],
              sizeof(snapshot->temperature_history_hour[0]) *
              (ROUTINE_TREND_POINTS - 1));
      memmove(&snapshot->temperature_history_minute[0],
              &snapshot->temperature_history_minute[1],
              sizeof(snapshot->temperature_history_minute[0]) *
              (ROUTINE_TREND_POINTS - 1));
      memmove(&snapshot->temperature_history_second[0],
              &snapshot->temperature_history_second[1],
              sizeof(snapshot->temperature_history_second[0]) *
              (ROUTINE_TREND_POINTS - 1));
      index = ROUTINE_TREND_POINTS - 1;
    }

  snapshot->temperature_history[index] = temperature;
  snapshot->temperature_history_hour[index] = hour;
  snapshot->temperature_history_minute[index] = minute;
  snapshot->temperature_history_second[index] = second;
}

static void routine_history_sample_current(struct routine_model_s *model,
                                           int32_t temperature)
{
  struct routine_snapshot_s *snapshot = &model->snapshot;
  uint32_t interval = snapshot->history_interval_seconds;
  int64_t timestamp;
  int64_t slot;
  uint8_t index;

  if (!snapshot->clock_valid || !routine_history_interval_valid(interval))
    {
      return;
    }

  timestamp = (int64_t)snapshot->day * 24 * 60 * 60 +
              (int64_t)snapshot->hour * 60 * 60 +
              (int64_t)snapshot->minute * 60 + snapshot->second;
  slot = timestamp / interval;
  if (model->temperature_history_slot_valid &&
      slot == model->temperature_history_slot &&
      snapshot->temperature_history_count > 0)
    {
      index = snapshot->temperature_history_count - 1;
      snapshot->temperature_history[index] = temperature;
      snapshot->temperature_history_hour[index] = snapshot->hour;
      snapshot->temperature_history_minute[index] = snapshot->minute;
      snapshot->temperature_history_second[index] = snapshot->second;
      return;
    }

  if (model->temperature_history_slot_valid &&
      slot < model->temperature_history_slot)
    {
      snapshot->temperature_history_count = 0;
      model->temperature_history_slot_valid = false;
    }

  if (model->temperature_history_slot_valid)
    {
      int64_t missing = slot - model->temperature_history_slot - 1;

      if (missing >= ROUTINE_TREND_POINTS)
        {
          snapshot->temperature_history_count = 0;
        }
      else
        {
          int64_t point;

          for (point = model->temperature_history_slot + 1;
               point < slot; point++)
            {
              int64_t seconds = point * interval;
              routine_history_append(
                snapshot, ROUTINE_HISTORY_INVALID,
                (uint8_t)((seconds / 3600) % 24),
                (uint8_t)((seconds / 60) % 60),
                (uint8_t)(seconds % 60));
            }
        }
    }

  routine_history_append(snapshot, temperature, snapshot->hour,
                         snapshot->minute, snapshot->second);
  model->temperature_history_slot = slot;
  model->temperature_history_slot_valid = true;
}

static int routine_enqueue_due(struct routine_model_s *model,
                               struct routine_schedule_item_s *item)
{
  struct routine_due_event_s *event;
  uint8_t tail;
  uint8_t bit = 1u << item->id;

  if ((model->due_queued_mask & bit) != 0 || item->notified ||
      item->completed || item->reminder_count >= ROUTINE_REMINDER_LIMIT)
    {
      return OK;
    }

  if (model->due_count >= ROUTINE_DUE_QUEUE_MAX)
    {
      return -ENOSPC;
    }

  tail = (model->due_head + model->due_count) % ROUTINE_DUE_QUEUE_MAX;
  event = &model->due_queue[tail];
  memset(event, 0, sizeof(*event));
  item->notified = true;
  item->reminder_count++;
  model->snapshot.schedule_due_sequence++;
  if (model->snapshot.schedule_due_sequence == 0)
    {
      model->snapshot.schedule_due_sequence++;
    }

  event->sequence = model->snapshot.schedule_due_sequence;
  item->active_due_sequence = event->sequence;
  event->due_day = item->due_day;
  event->schedule_id = item->id;
  event->text_id = item->text_id;
  event->hour = item->hour;
  event->minute = item->minute;
  event->second = model->snapshot.second;
  event->reminder_count = item->reminder_count;
  memcpy(event->text, item->text, sizeof(event->text));
  model->due_count++;
  model->due_queued_mask |= bit;
  return OK;
}

static void routine_detect_due(struct routine_model_s *model,
                               int64_t old_time, int64_t new_time)
{
  struct routine_snapshot_s *snapshot = &model->snapshot;
  uint8_t i;

  if (new_time <= old_time)
    {
      return;
    }

  for (i = 0; i < snapshot->schedule_count; i++)
    {
      int64_t due = routine_item_minutes(&snapshot->schedule[i]);

      if (due > old_time && due <= new_time)
        {
          routine_enqueue_due(model, &snapshot->schedule[i]);
        }
    }
}

static void routine_initialize_common(struct routine_model_s *model,
                                      enum routine_clock_mode_e mode,
                                      int32_t day, uint8_t hour,
                                      uint8_t minute, uint8_t second,
                                      bool clock_valid)
{
  memset(model, 0, sizeof(*model));
  model->clock_mode = mode;
  model->snapshot.day = day;
  model->snapshot.hour = hour;
  model->snapshot.minute = minute;
  model->snapshot.second = second;
  model->snapshot.clock_valid = clock_valid;
  model->snapshot.temperature_low_x100 =
    ROUTINE_DEFAULT_TEMPERATURE_LOW_X100;
  model->snapshot.temperature_high_x100 =
    ROUTINE_DEFAULT_TEMPERATURE_HIGH_X100;
  model->snapshot.history_interval_seconds =
    ROUTINE_DEFAULT_HISTORY_INTERVAL_SECONDS;
  model->settings.temperature_low_x100 =
    ROUTINE_DEFAULT_TEMPERATURE_LOW_X100;
  model->settings.temperature_high_x100 =
    ROUTINE_DEFAULT_TEMPERATURE_HIGH_X100;
  model->settings.history_interval_seconds =
    ROUTINE_DEFAULT_HISTORY_INTERVAL_SECONDS;
  for (uint8_t id = 0; id < ROUTINE_SCHEDULE_MAX; id++)
    {
      model->settings.schedule[id].id = id;
      model->settings.schedule[id].hour = g_schedule_hour[id];
      model->settings.schedule[id].minute = g_schedule_minute[id];
      strlcpy(model->settings.schedule[id].text, g_schedule_text[id],
              sizeof(model->settings.schedule[id].text));
    }
  model->snapshot.demo_mask = ENV_METRIC_ALL;
  routine_reset_schedule(model, day, false);
  routine_update_fallback_values(model);
}

int routine_model_init_live(struct routine_model_s *model,
                            const struct routine_clock_s *clock)
{
  if (model == NULL || clock == NULL || !clock->valid ||
      clock->hour > 23 || clock->minute > 59 || clock->second > 59)
    {
      return -EINVAL;
    }

  routine_initialize_common(model, ROUTINE_CLOCK_LIVE, clock->day,
                            clock->hour, clock->minute, clock->second,
                            clock->valid);
  model->snapshot.temperature_history_count = 0;
  return OK;
}

void routine_model_init_live_pending(struct routine_model_s *model)
{
  routine_initialize_common(model, ROUTINE_CLOCK_LIVE, 0, 0, 0, 0, false);
  model->snapshot.temperature_history_count = 0;
  model->snapshot.next_schedule = ROUTINE_NO_SCHEDULE;
}

void routine_model_init_demo(struct routine_model_s *model,
                             uint8_t seconds_per_hour)
{
  routine_initialize_common(model, ROUTINE_CLOCK_DEMO, 0, 8, 0, 0, true);
  model->seconds_per_hour = seconds_per_hour == 0 ? 1 : seconds_per_hour;
  model->snapshot.temperature_history_count = 0;
}

void routine_model_clear_due(struct routine_model_s *model)
{
  uint8_t i;

  if (model == NULL)
    {
      return;
    }

  memset(model->due_queue, 0, sizeof(model->due_queue));
  model->due_head = 0;
  model->due_count = 0;
  model->due_queued_mask = 0;
  for (i = 0; i < model->snapshot.schedule_count; i++)
    {
      model->snapshot.schedule[i].notified = false;
      model->snapshot.schedule[i].active_due_sequence = 0;
    }
}

int routine_model_update_clock(struct routine_model_s *model,
                               const struct routine_clock_s *clock)
{
  struct routine_snapshot_s *snapshot;
  int64_t old_time;
  int64_t new_time;
  int64_t elapsed;
  bool day_changed;

  if (model == NULL || clock == NULL || !clock->valid ||
      clock->hour > 23 || clock->minute > 59 || clock->second > 59)
    {
      return -EINVAL;
    }

  snapshot = &model->snapshot;
  new_time = routine_clock_minutes(clock->day, clock->hour, clock->minute);
  if (!snapshot->clock_valid)
    {
      routine_model_clear_due(model);
      snapshot->day = clock->day;
      snapshot->hour = clock->hour;
      snapshot->minute = clock->minute;
      snapshot->second = clock->second;
      snapshot->clock_valid = true;
      routine_reset_schedule(model, clock->day, false);
      routine_update_fallback_values(model);
      return ROUTINE_CLOCK_REBASED;
    }

  old_time = routine_clock_minutes(snapshot->day, snapshot->hour,
                                   snapshot->minute);
  elapsed = new_time - old_time;
  day_changed = clock->day != snapshot->day;
  snapshot->second = clock->second;
  if (model->clock_mode == ROUTINE_CLOCK_LIVE && elapsed < 0)
    {
      routine_model_clear_due(model);
      snapshot->day = clock->day;
      snapshot->hour = clock->hour;
      snapshot->minute = clock->minute;
      snapshot->second = clock->second;
      routine_reset_schedule(model, clock->day, false);
      routine_update_fallback_values(model);
      return ROUTINE_CLOCK_REBASED;
    }

  if (elapsed > 0)
    {
      if (!day_changed)
        {
          routine_detect_due(model, old_time, new_time);
        }

    }

  snapshot->day = clock->day;
  snapshot->hour = clock->hour;
  snapshot->minute = clock->minute;
  snapshot->second = clock->second;
  if (day_changed && elapsed > 0)
    {
      routine_reset_schedule(model, clock->day, true);
      routine_detect_due(model,
                         routine_clock_minutes(clock->day, 0, 0) - 1,
                         new_time);
    }

  routine_update_fallback_values(model);
  if (model->environment_attached)
    {
      routine_history_sample_current(
        model, (snapshot->live_mask & ENV_METRIC_TEMPERATURE) != 0 ?
               snapshot->temperature_x100 : ROUTINE_HISTORY_INVALID);
    }
  return OK;
}

void routine_model_tick_demo(struct routine_model_s *model)
{
  struct routine_clock_s clock;
  uint64_t total_seconds;

  if (model == NULL || model->clock_mode != ROUTINE_CLOCK_DEMO)
    {
      return;
    }

  model->elapsed_seconds++;
  total_seconds = (uint64_t)8 * 60 * 60 +
                  (uint64_t)model->elapsed_seconds * 60 * 60 /
                  model->seconds_per_hour;
  clock.day = total_seconds / (24 * 60 * 60);
  clock.hour = total_seconds / (60 * 60) % 24;
  clock.minute = total_seconds / 60 % 60;
  clock.second = total_seconds % 60;
  clock.valid = true;
  routine_model_update_clock(model, &clock);
}

int routine_model_apply_settings(
  struct routine_model_s *model,
  FAR const struct routine_settings_s *settings)
{
  struct routine_snapshot_s *snapshot;
  bool ids[ROUTINE_SCHEDULE_MAX] = {false};
  bool schedule_changed = false;
  bool history_interval_changed;
  bool same_generation;
  bool changed = false;
  int64_t now;
  uint8_t i;

  if (model == NULL || settings == NULL ||
      settings->temperature_low_x100 < -4000 ||
      settings->temperature_high_x100 > 12500 ||
      settings->temperature_high_x100 - settings->temperature_low_x100 < 100 ||
      !routine_history_interval_valid(settings->history_interval_seconds))
    {
      return -EINVAL;
    }

  for (i = 0; i < ROUTINE_SCHEDULE_MAX; i++)
    {
      FAR const struct routine_schedule_setting_s *setting =
        &settings->schedule[i];
      size_t length = strnlen(setting->text,
                              ROUTINE_SCHEDULE_TEXT_MAX + 1);

      if (setting->id >= ROUTINE_SCHEDULE_MAX || ids[setting->id] ||
          setting->hour > 23 || setting->minute > 59 ||
          length == 0 || length > ROUTINE_SCHEDULE_TEXT_MAX)
        {
          return -EINVAL;
        }

      ids[setting->id] = true;
    }

  snapshot = &model->snapshot;
  history_interval_changed = snapshot->history_interval_seconds !=
                             settings->history_interval_seconds;
  same_generation = snapshot->config_generation == settings->generation &&
                    settings->generation != 0;
  if (snapshot->temperature_low_x100 != settings->temperature_low_x100 ||
      snapshot->temperature_high_x100 != settings->temperature_high_x100 ||
      snapshot->history_interval_seconds !=
        settings->history_interval_seconds ||
      snapshot->config_generation != settings->generation)
    {
      snapshot->temperature_low_x100 = settings->temperature_low_x100;
      snapshot->temperature_high_x100 = settings->temperature_high_x100;
      snapshot->history_interval_seconds = settings->history_interval_seconds;
      snapshot->config_generation = settings->generation;
      changed = true;
    }

  if (history_interval_changed)
    {
      snapshot->temperature_history_count = 0;
      model->temperature_history_slot = 0;
      model->temperature_history_slot_valid = false;
    }

  model->settings.generation = settings->generation;
  model->settings.temperature_low_x100 = settings->temperature_low_x100;
  model->settings.temperature_high_x100 = settings->temperature_high_x100;
  model->settings.history_interval_seconds =
    settings->history_interval_seconds;

  now = routine_clock_minutes(snapshot->day, snapshot->hour,
                              snapshot->minute);
  for (i = 0; i < ROUTINE_SCHEDULE_MAX; i++)
    {
      FAR const struct routine_schedule_setting_s *setting =
        &settings->schedule[i];
      FAR struct routine_schedule_setting_s *configured =
        &model->settings.schedule[setting->id];
      int index = routine_find_schedule_id(snapshot, setting->id);
      struct routine_schedule_item_s *item;

      if (index < 0)
        {
          return index;
        }

      if (same_generation)
        {
          *configured = *setting;
          item = &snapshot->schedule[index];
          if (strcmp(item->text, setting->text) != 0)
            {
              memcpy(item->text, setting->text, sizeof(item->text));
              changed = true;
            }
          continue;
        }

      if (configured->hour == setting->hour &&
          configured->minute == setting->minute &&
          strcmp(configured->text, setting->text) == 0)
        {
          continue;
        }

      *configured = *setting;
      item = &snapshot->schedule[index];
      item->due_day = snapshot->day;
      item->hour = setting->hour;
      item->minute = setting->minute;
      memcpy(item->text, setting->text, sizeof(item->text));
      item->text[ROUTINE_SCHEDULE_TEXT_MAX] = '\0';
      item->reminder_count = 0;
      item->active_due_sequence = 0;
      item->notified = false;
      item->carry_over = false;
      item->completed = snapshot->clock_valid &&
                        routine_item_minutes(item) < now;
      schedule_changed = true;
      changed = true;
    }

  if (schedule_changed)
    {
      routine_model_clear_due(model);
      routine_sort_schedule(snapshot);
      snapshot->next_schedule = routine_find_next_schedule(snapshot);
    }

  return changed ? 1 : OK;
}

void routine_model_apply_environment(
  struct routine_model_s *model,
  FAR const struct environment_sample_s *environment)
{
  struct routine_snapshot_s *snapshot;

  if (model == NULL || environment == NULL)
    {
      return;
    }

  snapshot = &model->snapshot;
  if (!model->environment_attached)
    {
      memset(snapshot->temperature_history, 0,
             sizeof(snapshot->temperature_history));
      memset(snapshot->temperature_history_hour, 0,
             sizeof(snapshot->temperature_history_hour));
      memset(snapshot->temperature_history_minute, 0,
             sizeof(snapshot->temperature_history_minute));
      memset(snapshot->temperature_history_second, 0,
             sizeof(snapshot->temperature_history_second));
      snapshot->temperature_history_count = 0;
      model->temperature_history_slot = 0;
      model->temperature_history_slot_valid = false;
      model->environment_attached = true;
    }

  snapshot->live_mask = environment->live_mask;
  snapshot->demo_mask = environment->demo_mask;
  snapshot->stale_mask = environment->stale_mask;
  snapshot->warmup_mask = environment->warmup_mask;
  snapshot->offline_mask = environment->offline_mask;

  if ((environment->valid_mask & ENV_METRIC_TEMPERATURE) != 0)
    {
      snapshot->temperature_x100 = environment->temperature_x100;
    }

  if ((environment->valid_mask & ENV_METRIC_HUMIDITY) != 0)
    {
      snapshot->humidity_x100 = environment->humidity_x100;
    }

  if ((environment->valid_mask & ENV_METRIC_LIGHT) != 0)
    {
      snapshot->light_x10 = environment->light_x10;
    }

  if ((environment->valid_mask & ENV_METRIC_ECO2) != 0)
    {
      snapshot->eco2_ppm = environment->eco2_ppm;
      snapshot->tvoc_ppb = environment->tvoc_ppb;
    }

  if ((environment->live_mask & ENV_METRIC_TEMPERATURE) != 0)
    {
      routine_history_sample_current(model, environment->temperature_x100);
    }
  else
    {
      routine_history_sample_current(model, ROUTINE_HISTORY_INVALID);
    }

  routine_update_fallback_values(model);
}

static void routine_restore_after_carry_over(struct routine_model_s *model,
                                             uint8_t index)
{
  struct routine_snapshot_s *snapshot = &model->snapshot;
  uint8_t id = snapshot->schedule[index].id;
  int64_t now = routine_clock_minutes(snapshot->day, snapshot->hour,
                                      snapshot->minute);
  struct routine_schedule_item_s item;

  routine_set_default_item(model, &item, id, snapshot->day);
  if (routine_item_minutes(&item) < now)
    {
      item.completed = true;
    }

  snapshot->schedule[index] = item;
}

int routine_model_apply_schedule_action(
  struct routine_model_s *model, uint8_t schedule_id,
  uint32_t expected_due_sequence, enum routine_schedule_action_e action,
  struct routine_due_event_s *ended_due)
{
  struct routine_snapshot_s *snapshot;
  struct routine_schedule_item_s *item;
  int64_t minutes;
  int64_t now;
  bool due_action;
  int index;

  if (model == NULL || schedule_id >= ROUTINE_SCHEDULE_MAX)
    {
      return -EINVAL;
    }

  snapshot = &model->snapshot;
  if (!snapshot->clock_valid)
    {
      return -ENODATA;
    }

  index = routine_find_schedule_id(snapshot, schedule_id);
  if (index < 0)
    {
      return index;
    }

  item = &snapshot->schedule[index];
  due_action = expected_due_sequence != 0;
  if (due_action && (!item->notified ||
      item->active_due_sequence != expected_due_sequence))
    {
      return -ESTALE;
    }

  if (ended_due != NULL)
    {
      memset(ended_due, 0, sizeof(*ended_due));
      if (item->notified && item->active_due_sequence != 0)
        {
          ended_due->sequence = item->active_due_sequence;
          ended_due->due_day = item->due_day;
          ended_due->schedule_id = item->id;
          ended_due->text_id = item->text_id;
          ended_due->hour = item->hour;
          ended_due->minute = item->minute;
          ended_due->second = snapshot->second;
          ended_due->reminder_count = item->reminder_count;
          memcpy(ended_due->text, item->text, sizeof(ended_due->text));
        }
    }

  switch (action)
    {
      case ROUTINE_SCHEDULE_COMPLETE:
        if (item->carry_over)
          {
            routine_restore_after_carry_over(model, index);
            item = &snapshot->schedule[index];
          }
        else
          {
            item->completed = true;
          }
        break;

      case ROUTINE_SCHEDULE_DELAY_10_MINUTES:
        if (item->completed)
          {
            return -EALREADY;
          }

        if (!due_action && !item->notified)
          {
            minutes = routine_item_minutes(item);
            now = routine_clock_minutes(snapshot->day, snapshot->hour,
                                        snapshot->minute);
            if (minutes < now)
              {
                minutes = now;
              }

            item->reminder_count = 0;
          }
        else if (item->reminder_count >= ROUTINE_REMINDER_LIMIT)
          {
            break;
          }
        else
          {
            minutes = routine_clock_minutes(snapshot->day, snapshot->hour,
                                            snapshot->minute);
          }

        minutes += 10;
        item->due_day = minutes / (24 * 60);
        item->hour = minutes / 60 % 24;
        item->minute = minutes % 60;
        item->carry_over = item->carry_over ||
                           item->due_day > snapshot->day;
        break;

      case ROUTINE_SCHEDULE_DISMISS:
        if (item->completed || !item->notified)
          {
            return -EALREADY;
          }

        if (item->reminder_count < ROUTINE_REMINDER_LIMIT)
          {
            minutes = routine_clock_minutes(snapshot->day, snapshot->hour,
                                            snapshot->minute) + 10;
            item->due_day = minutes / (24 * 60);
            item->hour = minutes / 60 % 24;
            item->minute = minutes % 60;
            item->carry_over = item->carry_over ||
                               item->due_day > snapshot->day;
          }
        break;

      default:
        return -EINVAL;
    }

  item->notified = false;
  item->active_due_sequence = 0;
  routine_sort_schedule(snapshot);
  snapshot->next_schedule = routine_find_next_schedule(snapshot);
  return OK;
}

int routine_model_schedule_action_id(struct routine_model_s *model,
                                     uint8_t schedule_id,
                                     enum routine_schedule_action_e action)
{
  return routine_model_apply_schedule_action(model, schedule_id, 0, action,
                                             NULL);
}

int routine_model_schedule_action(struct routine_model_s *model,
                                  uint8_t index,
                                  enum routine_schedule_action_e action)
{
  if (model == NULL || index >= model->snapshot.schedule_count)
    {
      return -EINVAL;
    }

  return routine_model_schedule_action_id(model,
                                          model->snapshot.schedule[index].id,
                                          action);
}

int routine_model_take_due(struct routine_model_s *model,
                           struct routine_due_event_s *event)
{
  if (model == NULL || event == NULL)
    {
      return -EINVAL;
    }

  if (model->due_count == 0)
    {
      return -EAGAIN;
    }

  *event = model->due_queue[model->due_head];
  model->due_head = (model->due_head + 1) % ROUTINE_DUE_QUEUE_MAX;
  model->due_count--;
  model->due_queued_mask &= ~(1u << event->schedule_id);
  return OK;
}

const struct routine_snapshot_s *
routine_model_get_snapshot(const struct routine_model_s *model)
{
  return &model->snapshot;
}

static int routine_selftest_set_clock(struct routine_model_s *model,
                                      int32_t day, uint8_t hour,
                                      uint8_t minute)
{
  struct routine_clock_s clock;
  int64_t target;
  int64_t current;
  int ret = OK;

  target = routine_clock_minutes(day, hour, minute);
  if (model->snapshot.clock_valid)
    {
      current = routine_clock_minutes(model->snapshot.day,
                                      model->snapshot.hour,
                                      model->snapshot.minute);
      while (current < target)
        {
          current++;
          clock.day = current / (24 * 60);
          clock.hour = current / 60 % 24;
          clock.minute = current % 60;
          clock.second = 0;
          clock.valid = true;
          ret = routine_model_update_clock(model, &clock);
          if (ret < 0)
            {
              return ret;
            }
        }

      return ret;
    }

  clock.day = day;
  clock.hour = hour;
  clock.minute = minute;
  clock.second = 0;
  clock.valid = true;
  return routine_model_update_clock(model, &clock);
}

int routine_model_selftest(void)
{
  struct routine_due_event_s event;
  struct routine_due_event_s first_due;
  struct routine_due_event_s ended_due;
  struct environment_sample_s environment;
  struct routine_clock_s clock = {10, 8, 59, true};
  struct routine_model_s model;
  const struct routine_snapshot_s *snapshot;
  int index;
  uint8_t i;

  if (routine_model_init_live(&model, &clock) < 0)
    {
      return -EINVAL;
    }

  snapshot = routine_model_get_snapshot(&model);
  if (snapshot->hour != 8 || snapshot->minute != 59 ||
      snapshot->day != 10 || snapshot->schedule_count != ROUTINE_SCHEDULE_MAX ||
      snapshot->temperature_history_count != 0)
    {
      return -EINVAL;
    }

  if (routine_selftest_set_clock(&model, 10, 9, 0) < 0 ||
      routine_model_take_due(&model, &event) < 0 ||
      event.schedule_id != ROUTINE_SCHEDULE_ID_DRINK ||
      event.reminder_count != 1 ||
      routine_model_take_due(&model, &event) != -EAGAIN)
    {
      return -EINVAL;
    }

  if (routine_selftest_set_clock(&model, 10, 9, 5) < 0 ||
      routine_model_take_due(&model, &event) != -EAGAIN ||
      routine_model_schedule_action_id(&model, ROUTINE_SCHEDULE_ID_DRINK,
                                       ROUTINE_SCHEDULE_DISMISS) < 0 ||
      routine_selftest_set_clock(&model, 10, 9, 15) < 0 ||
      routine_model_take_due(&model, &event) < 0 || event.reminder_count != 2)
    {
      return -EINVAL;
    }

  if (routine_model_schedule_action_id(&model, ROUTINE_SCHEDULE_ID_DRINK,
                                       ROUTINE_SCHEDULE_DISMISS) < 0 ||
      routine_selftest_set_clock(&model, 10, 9, 25) < 0 ||
      routine_model_take_due(&model, &event) < 0 || event.reminder_count != 3 ||
      routine_model_schedule_action_id(&model, ROUTINE_SCHEDULE_ID_DRINK,
                                       ROUTINE_SCHEDULE_DISMISS) < 0 ||
      routine_selftest_set_clock(&model, 10, 10, 0) < 0 ||
      routine_model_take_due(&model, &event) != -EAGAIN)
    {
      return -EINVAL;
    }

  clock.day = 11;
  clock.hour = 8;
  clock.minute = 59;
  routine_model_init_live(&model, &clock);
  if (routine_selftest_set_clock(&model, 11, 9, 0) < 0 ||
      routine_model_take_due(&model, &first_due) < 0 ||
      first_due.reminder_count != 1 ||
      routine_model_apply_schedule_action(
        &model, first_due.schedule_id, first_due.sequence,
        ROUTINE_SCHEDULE_DELAY_10_MINUTES, &ended_due) < 0 ||
      ended_due.sequence != first_due.sequence ||
      routine_selftest_set_clock(&model, 11, 9, 10) < 0 ||
      routine_model_take_due(&model, &event) < 0 ||
      event.reminder_count != 2 || event.sequence == first_due.sequence ||
      routine_model_apply_schedule_action(
        &model, first_due.schedule_id, first_due.sequence,
        ROUTINE_SCHEDULE_COMPLETE, NULL) != -ESTALE ||
      routine_model_apply_schedule_action(
        &model, event.schedule_id, event.sequence,
        ROUTINE_SCHEDULE_DELAY_10_MINUTES, NULL) < 0 ||
      routine_selftest_set_clock(&model, 11, 9, 20) < 0 ||
      routine_model_take_due(&model, &event) < 0 ||
      event.reminder_count != 3 ||
      routine_model_apply_schedule_action(
        &model, event.schedule_id, event.sequence,
        ROUTINE_SCHEDULE_DELAY_10_MINUTES, NULL) < 0 ||
      routine_selftest_set_clock(&model, 11, 9, 40) < 0 ||
      routine_model_take_due(&model, &event) != -EAGAIN)
    {
      return -EINVAL;
    }

  routine_model_init_live_pending(&model);
  if (model.snapshot.clock_valid ||
      model.snapshot.next_schedule != ROUTINE_NO_SCHEDULE ||
      routine_selftest_set_clock(&model, 12, 12, 0) !=
      ROUTINE_CLOCK_REBASED ||
      !model.snapshot.clock_valid ||
      routine_model_take_due(&model, &event) != -EAGAIN)
    {
      return -EINVAL;
    }

  routine_model_init_demo(&model, 60);
  for (i = 0; i < 60; i++)
    {
      routine_model_tick_demo(&model);
    }

  snapshot = routine_model_get_snapshot(&model);
  if (snapshot->hour != 9 || snapshot->minute != 0 ||
      routine_model_take_due(&model, &event) < 0 ||
      event.schedule_id != ROUTINE_SCHEDULE_ID_DRINK)
    {
      return -EINVAL;
    }

  clock.day = 20;
  clock.hour = 23;
  clock.minute = 55;
  routine_model_init_live(&model, &clock);
  index = routine_find_schedule_id(&model.snapshot,
                                   ROUTINE_SCHEDULE_ID_VENTILATE);
  model.snapshot.schedule[index].due_day = 20;
  model.snapshot.schedule[index].hour = 23;
  model.snapshot.schedule[index].minute = 50;
  model.snapshot.schedule[index].notified = true;
  model.snapshot.schedule[index].reminder_count = 1;
  if (routine_model_schedule_action_id(&model, ROUTINE_SCHEDULE_ID_VENTILATE,
                                       ROUTINE_SCHEDULE_DELAY_10_MINUTES) < 0)
    {
      return -EINVAL;
    }

  index = routine_find_schedule_id(&model.snapshot,
                                   ROUTINE_SCHEDULE_ID_VENTILATE);
  if (model.snapshot.schedule[index].due_day != 21 ||
      model.snapshot.schedule[index].hour != 0 ||
      model.snapshot.schedule[index].minute != 5 ||
      !model.snapshot.schedule[index].carry_over ||
      routine_selftest_set_clock(&model, 21, 0, 5) < 0 ||
      routine_model_take_due(&model, &event) < 0 ||
      event.schedule_id != ROUTINE_SCHEDULE_ID_VENTILATE)
    {
      return -EINVAL;
    }

  if (routine_model_schedule_action_id(&model,
                                       ROUTINE_SCHEDULE_ID_VENTILATE,
                                       ROUTINE_SCHEDULE_COMPLETE) < 0)
    {
      return -EINVAL;
    }

  index = routine_find_schedule_id(&model.snapshot,
                                   ROUTINE_SCHEDULE_ID_VENTILATE);
  if (model.snapshot.schedule[index].due_day != 21 ||
      model.snapshot.schedule[index].hour != 18 ||
      model.snapshot.schedule[index].completed)
    {
      return -EINVAL;
    }

  routine_model_init_demo(&model, 60);
  snapshot = routine_model_get_snapshot(&model);
  if (snapshot->temperature_history_count != 0)
    {
      return -EINVAL;
    }

  clock.day = 30;
  clock.hour = 12;
  clock.minute = 0;
  routine_model_init_live(&model, &clock);
  memset(&environment, 0, sizeof(environment));
  environment.temperature_x100 = 2345;
  environment.humidity_x100 = 5678;
  environment.valid_mask = ENV_METRIC_TEMPERATURE | ENV_METRIC_HUMIDITY;
  environment.live_mask = environment.valid_mask;
  environment.demo_mask = ENV_METRIC_ALL & ~environment.valid_mask;
  routine_model_apply_environment(&model, &environment);
  snapshot = routine_model_get_snapshot(&model);
  if (snapshot->temperature_history_count != 1 ||
      snapshot->temperature_history[0] != 2345 ||
      routine_selftest_set_clock(&model, 30, 12, 1) < 0 ||
      snapshot->temperature_history_count != 2)
    {
      return -EINVAL;
    }

  environment.live_mask = 0;
  environment.offline_mask = ENV_METRIC_TEMPERATURE |
                             ENV_METRIC_HUMIDITY;
  routine_model_apply_environment(&model, &environment);
  if (snapshot->temperature_history[1] != ROUTINE_HISTORY_INVALID ||
      snapshot->humidity_x100 > 10000 || snapshot->hour > 23 ||
      snapshot->minute > 59)
    {
      return -EINVAL;
    }

  return OK;
}
