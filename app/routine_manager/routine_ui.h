/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/routine_ui.h
 ****************************************************************************/

#ifndef __ROUTINE_UI_H
#define __ROUTINE_UI_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>

#include <lvgl/lvgl.h>

#include "audio_service.h"
#include "routine_model.h"

enum routine_ui_language_e
{
  ROUTINE_UI_LANGUAGE_CHINESE = 0,
  ROUTINE_UI_LANGUAGE_ENGLISH
};

typedef int (*routine_ui_schedule_action_t)(
  void *user_data, uint8_t schedule_id, uint32_t due_sequence,
  enum routine_schedule_action_e action);
typedef int (*routine_ui_voice_action_t)(
  void *user_data, enum audio_service_request_e request, bool start);

enum routine_ui_history_action_e
{
  ROUTINE_UI_HISTORY_SET_INTERVAL = 0,
  ROUTINE_UI_HISTORY_EXPORT_SD
};

typedef int (*routine_ui_history_action_t)(
  void *user_data, enum routine_ui_history_action_e action,
  uint32_t interval_seconds, char *result, size_t result_size);

struct routine_ui_options_s
{
  enum routine_ui_language_e language;
  const char *font_path;
  routine_ui_schedule_action_t schedule_action;
  routine_ui_voice_action_t voice_action;
  routine_ui_history_action_t history_action;
  void *user_data;
};

int routine_ui_init(lv_display_t *display,
                    const struct routine_ui_options_s *options,
                    const struct routine_snapshot_s *snapshot);
void routine_ui_update(const struct routine_snapshot_s *snapshot);
void routine_ui_voice_update(
  const struct audio_service_snapshot_s *snapshot);
int routine_ui_enqueue_due(const struct routine_due_event_s *event);
void routine_ui_remove_due(const struct routine_due_event_s *event);
void routine_ui_clear_due(void);
void routine_ui_deinit(void);

#endif /* __ROUTINE_UI_H */
