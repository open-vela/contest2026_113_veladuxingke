/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/routine_ui.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <lvgl/lvgl.h>
#include <lvgl/src/font/lv_binfont_loader.h>

#include "routine_ui.h"
#include "routine_ui_layout.h"
#include "wifi_ui.h"
#include "wifi_status_monitor.h"

#define ROUTINE_COLOR_SCREEN    lv_color_hex(0x171a1d)
#define ROUTINE_COLOR_CARD      lv_color_hex(0x23282e)
#define ROUTINE_COLOR_CARD_ALT  lv_color_hex(0x2a3037)
#define ROUTINE_COLOR_TEXT      lv_color_hex(0xf5f7f8)
#define ROUTINE_COLOR_SECONDARY lv_color_hex(0xc4cbd1)
#define ROUTINE_COLOR_MUTED     lv_color_hex(0x87919a)
#define ROUTINE_COLOR_GRID      lv_color_hex(0x39414a)
#define ROUTINE_COLOR_BLUE      lv_color_hex(0x3987e5)
#define ROUTINE_COLOR_ORANGE    lv_color_hex(0xe26f32)
#define ROUTINE_COLOR_GREEN     lv_color_hex(0x24a879)
#define ROUTINE_COLOR_RED       lv_color_hex(0xd94f55)

#define ROUTINE_SENSOR_CARD_COUNT 4
#define ROUTINE_CHART_POINTS      ROUTINE_HISTORY_WINDOW
#define ROUTINE_DUE_UI_QUEUE_MAX  ROUTINE_SCHEDULE_MAX

struct routine_ui_text_s
{
  const char *title;
  const char *page_sensor;
  const char *page_history;
  const char *card_titles[ROUTINE_SENSOR_CARD_COUNT];
  const char *chart_title;
  const char *schedule_title;
  const char *schedule_items[ROUTINE_SCHEDULE_MAX];
  const char *next;
  const char *completed;
  const char *voice_idle;
  const char *voice_listening;
  const char *voice_processing;
  const char *voice_speaking;
  const char *voice_done;
  const char *voice_stopped;
  const char *voice_busy;
  const char *voice_demo;
  const char *voice_start;
  const char *voice_stop;
  const char *voice_errors[6];
  const char *schedule_action;
  const char *schedule_due;
  const char *done;
  const char *delay;
  const char *cancel;
  const char *detail_title;
  const char *play_data;
  const char *detail_temperature;
  const char *detail_humidity;
  const char *detail_light;
  const char *status[5];
  const char *status_help[5];
};

enum routine_sensor_state_e
{
  ROUTINE_SENSOR_DEMO = 0,
  ROUTINE_SENSOR_LIVE,
  ROUTINE_SENSOR_STALE,
  ROUTINE_SENSOR_WARMUP,
  ROUTINE_SENSOR_OFFLINE
};

enum routine_ui_dialog_e
{
  ROUTINE_UI_DIALOG_NONE = 0,
  ROUTINE_UI_DIALOG_SCHEDULE,
  ROUTINE_UI_DIALOG_TEMPERATURE,
  ROUTINE_UI_DIALOG_HUMIDITY,
  ROUTINE_UI_DIALOG_BH1750,
  ROUTINE_UI_DIALOG_SGP30,
  ROUTINE_UI_DIALOG_HISTORY
};

struct routine_ui_context_s
{
  lv_obj_t *root;
  lv_obj_t *time_label;
  lv_obj_t *page_label;
  lv_obj_t *page_prev;
  lv_obj_t *page_next;
  lv_obj_t *wifi_button;
  lv_obj_t *curve_button;
  lv_obj_t *wifi_panel;
  lv_obj_t *tileview;
  lv_obj_t *sensor_page;
  lv_obj_t *history_page;
  lv_obj_t *cards[ROUTINE_SENSOR_CARD_COUNT];
  lv_obj_t *value_labels[ROUTINE_SENSOR_CARD_COUNT];
  lv_obj_t *subvalue_labels[ROUTINE_SENSOR_CARD_COUNT];
  lv_obj_t *card_status[ROUTINE_SENSOR_CARD_COUNT];
  lv_obj_t *voice_status;
  lv_obj_t *voice_button;
  lv_obj_t *voice_button_label;
  lv_obj_t *voice_result;
  lv_obj_t *chart;
  lv_chart_series_t *temperature_series;
  lv_obj_t *chart_range;
  lv_obj_t *chart_selection;
  lv_obj_t *chart_prev;
  lv_obj_t *chart_next;
  lv_obj_t *chart_axis[ROUTINE_CHART_POINTS];
  lv_obj_t *chart_value[ROUTINE_CHART_POINTS];
  lv_obj_t *schedule_rows[ROUTINE_SCHEDULE_MAX];
  lv_obj_t *schedule_bars[ROUTINE_SCHEDULE_MAX];
  lv_obj_t *schedule_time[ROUTINE_SCHEDULE_MAX];
  lv_obj_t *schedule_text[ROUTINE_SCHEDULE_MAX];
  lv_obj_t *schedule_next[ROUTINE_SCHEDULE_MAX];
  lv_obj_t *dialog;
  lv_obj_t *dialog_text;
  lv_obj_t *dialog_play_button;
  lv_obj_t *dialog_play_label;
  lv_obj_t *dialog_play_status;
  lv_font_t *text_font;
  const lv_font_t *small_font;
  const struct routine_ui_text_s *text;
  routine_ui_schedule_action_t schedule_action;
  routine_ui_voice_action_t voice_action;
  routine_ui_history_action_t history_action;
  void *user_data;
  enum routine_ui_language_e language;
  enum routine_ui_dialog_e dialog_type;
  enum audio_service_request_e active_request;
  uint8_t selected_schedule;
  uint8_t history_start;
  uint8_t current_page;
  int32_t chart_min;
  int32_t chart_max;
  uint32_t last_audio_sequence;
  bool history_follow_latest;
  bool history_dirty;
  bool sensor_dirty;
  bool schedule_dirty;
  bool snapshot_initialized;
  bool voice_active;
  char time_buffer[8];
  char value_buffer[ROUTINE_SENSOR_CARD_COUNT][32];
  char subvalue_buffer[ROUTINE_SENSOR_CARD_COUNT][32];
  char chart_range_buffer[24];
  char chart_selection_buffer[32];
  char chart_axis_buffer[ROUTINE_CHART_POINTS][10];
  char chart_value_buffer[ROUTINE_CHART_POINTS][12];
  char schedule_time_buffer[ROUTINE_SCHEDULE_MAX][8];
  char voice_result_buffer[112];
  char dialog_buffer[384];
  struct routine_snapshot_s snapshot;
  struct routine_due_event_s due_queue[ROUTINE_DUE_UI_QUEUE_MAX];
  struct routine_due_event_s active_due;
  uint8_t due_head;
  uint8_t due_count;
  bool dialog_is_due;
};

static const struct routine_ui_text_s g_chinese_text =
{
  .title = "桌面管家",
  .page_sensor = "传感器  1/2",
  .page_history = "历史与日程  2/2",
  .card_titles = {"温度", "湿度", "光照", "空气质量"},
  .chart_title = "运行期温度历史",
  .schedule_title = "今日日程",
  .schedule_items = {"喝水", "专注", "通风"},
  .next = "下一项",
  .completed = "已完成",
  .voice_idle = "语音待机",
  .voice_listening = "正在聆听…",
  .voice_processing = "准备播报…",
  .voice_speaking = "正在播报…",
  .voice_done = "播放完成",
  .voice_stopped = "播放已停止",
  .voice_busy = "语音服务正忙",
  .voice_demo = "点击“开始语音”后直接提问",
  .voice_start = "开始语音",
  .voice_stop = "停止",
  .voice_errors = {"", "录音设备不可用", "未检测到有效人声",
                   "播报资源不可用", "播放设备不可用",
                   "传感器暂无有效数据"},
  .schedule_action = "日程操作",
  .schedule_due = "事项提醒",
  .done = "完成",
  .delay = "延后10分钟",
  .cancel = "关闭",
  .detail_title = "传感器详情",
  .play_data = "播放数据",
  .detail_temperature = "温度",
  .detail_humidity = "湿度",
  .detail_light = "光照",
  .status = {"演示数据", "实时", "数据过期，正在恢复", "预热中",
             "传感器已掉线"},
  .status_help =
  {
    "当前使用演示数据",
    "传感器正在实时更新",
    "保留最后采样，仅用于诊断",
    "SGP30 正在预热，暂无有效读数",
    "连接中断，等待自动恢复"
  },
};

static const struct routine_ui_text_s g_english_text =
{
  .title = "ROUTINE DESK",
  .page_sensor = "SENSORS  1/2",
  .page_history = "HISTORY  2/2",
  .card_titles = {"TEMP", "HUMIDITY", "LIGHT", "AIR QUALITY"},
  .chart_title = "RUN-TIME TEMP HISTORY",
  .schedule_title = "TODAY",
  .schedule_items = {"WATER", "FOCUS", "AIR"},
  .next = "NEXT",
  .completed = "DONE",
  .voice_idle = "VOICE IDLE",
  .voice_listening = "LISTENING...",
  .voice_processing = "PREPARING...",
  .voice_speaking = "SPEAKING...",
  .voice_done = "PLAYED",
  .voice_stopped = "STOPPED",
  .voice_busy = "VOICE BUSY",
  .voice_demo = "TAP START VOICE, THEN ASK",
  .voice_start = "START VOICE",
  .voice_stop = "STOP",
  .voice_errors = {"", "CAPTURE UNAVAILABLE", "NO SPEECH DETECTED",
                   "VOICE ASSET MISSING", "PLAYBACK UNAVAILABLE",
                   "SENSOR DATA UNAVAILABLE"},
  .schedule_action = "SCHEDULE ACTION",
  .schedule_due = "REMINDER",
  .done = "DONE",
  .delay = "+10 MIN",
  .cancel = "CLOSE",
  .detail_title = "SENSOR DETAILS",
  .play_data = "PLAY DATA",
  .detail_temperature = "TEMPERATURE",
  .detail_humidity = "HUMIDITY",
  .detail_light = "LIGHT",
  .status = {"DEMO", "LIVE", "STALE / RECOVERING", "WARMING UP", "OFFLINE"},
  .status_help =
  {
    "Using demonstration data",
    "Sensor is updating live",
    "Last sample retained for diagnostics",
    "SGP30 is warming up; no valid reading",
    "Connection lost; waiting for recovery"
  },
};

static struct routine_ui_context_s g_ui;

static void routine_try_open_due(void);

static lv_obj_t *routine_create_panel(lv_obj_t *parent, int32_t x,
                                      int32_t y, int32_t width,
                                      int32_t height)
{
  lv_obj_t *panel = lv_obj_create(parent);

  lv_obj_remove_style_all(panel);
  lv_obj_set_pos(panel, x, y);
  lv_obj_set_size(panel, width, height);
  lv_obj_set_style_bg_color(panel, ROUTINE_COLOR_CARD, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(panel, 6, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  return panel;
}

static lv_obj_t *routine_create_label(lv_obj_t *parent,
                                      const lv_font_t *font,
                                      lv_color_t color)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  return label;
}

static const lv_font_t *routine_text_font(const lv_font_t *english)
{
  return g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE ? g_ui.text_font :
                                                        english;
}

static void routine_format_signed_x100(FAR char *buffer, size_t size,
                                       int32_t value, FAR const char *unit)
{
  int32_t whole = value / 100;
  int32_t fraction = abs(value % 100);

  if (value < 0 && whole == 0)
    {
      snprintf(buffer, size, "-0.%02ld%s", (long)fraction, unit);
    }
  else
    {
      snprintf(buffer, size, "%ld.%02ld%s", (long)whole,
               (long)fraction, unit);
    }
}

static uint32_t routine_dialog_mask(enum routine_ui_dialog_e type)
{
  if (type == ROUTINE_UI_DIALOG_TEMPERATURE)
    {
      return ENV_METRIC_TEMPERATURE;
    }
  else if (type == ROUTINE_UI_DIALOG_HUMIDITY)
    {
      return ENV_METRIC_HUMIDITY;
    }
  else if (type == ROUTINE_UI_DIALOG_BH1750)
    {
      return ENV_METRIC_LIGHT;
    }
  else if (type == ROUTINE_UI_DIALOG_SGP30)
    {
      return ENV_METRIC_ECO2 | ENV_METRIC_TVOC;
    }

  return 0;
}

static enum routine_sensor_state_e routine_metric_status(
  FAR const struct routine_snapshot_s *snapshot, uint32_t mask)
{
  if ((snapshot->offline_mask & mask) != 0)
    {
      return ROUTINE_SENSOR_OFFLINE;
    }

  if ((snapshot->warmup_mask & mask) != 0)
    {
      return ROUTINE_SENSOR_WARMUP;
    }

  if ((snapshot->stale_mask & mask) != 0)
    {
      return ROUTINE_SENSOR_STALE;
    }

  if ((snapshot->live_mask & mask) == mask)
    {
      return ROUTINE_SENSOR_LIVE;
    }

  return ROUTINE_SENSOR_DEMO;
}

static lv_color_t routine_status_color(enum routine_sensor_state_e status)
{
  if (status == ROUTINE_SENSOR_LIVE)
    {
      return ROUTINE_COLOR_GREEN;
    }
  else if (status == ROUTINE_SENSOR_WARMUP)
    {
      return ROUTINE_COLOR_BLUE;
    }
  else if (status == ROUTINE_SENSOR_OFFLINE)
    {
      return ROUTINE_COLOR_RED;
    }
  else if (status == ROUTINE_SENSOR_STALE)
    {
      return ROUTINE_COLOR_ORANGE;
    }

  return ROUTINE_COLOR_MUTED;
}

static const char *routine_schedule_text(const char *custom, uint8_t text_id)
{
  if (custom != NULL && custom[0] != '\0')
    {
      return custom;
    }

  return g_ui.text->schedule_items[text_id < ROUTINE_SCHEDULE_MAX ? text_id : 0];
}

static bool routine_dialog_is_sensor(enum routine_ui_dialog_e type)
{
  return type == ROUTINE_UI_DIALOG_TEMPERATURE ||
         type == ROUTINE_UI_DIALOG_HUMIDITY ||
         type == ROUTINE_UI_DIALOG_BH1750 ||
         type == ROUTINE_UI_DIALOG_SGP30;
}

static enum audio_service_request_e routine_dialog_request(
  enum routine_ui_dialog_e type)
{
  switch (type)
    {
      case ROUTINE_UI_DIALOG_TEMPERATURE:
        return AUDIO_SERVICE_REQUEST_TEMPERATURE;
      case ROUTINE_UI_DIALOG_HUMIDITY:
        return AUDIO_SERVICE_REQUEST_HUMIDITY;
      case ROUTINE_UI_DIALOG_BH1750:
        return AUDIO_SERVICE_REQUEST_BH1750;
      case ROUTINE_UI_DIALOG_SGP30:
        return AUDIO_SERVICE_REQUEST_SGP30;
      default:
        return AUDIO_SERVICE_REQUEST_NONE;
    }
}

static void routine_close_dialog(void)
{
  lv_obj_t *dialog = g_ui.dialog;

  g_ui.dialog = NULL;
  g_ui.dialog_text = NULL;
  g_ui.dialog_play_button = NULL;
  g_ui.dialog_play_label = NULL;
  g_ui.dialog_play_status = NULL;
  g_ui.dialog_type = ROUTINE_UI_DIALOG_NONE;
  g_ui.dialog_is_due = false;
  if (dialog != NULL)
    {
      lv_msgbox_close(dialog);
    }
}

static void routine_dialog_delete_cb(lv_event_t *event)
{
  if (lv_event_get_target(event) == g_ui.dialog)
    {
      g_ui.dialog = NULL;
      g_ui.dialog_text = NULL;
      g_ui.dialog_play_button = NULL;
      g_ui.dialog_play_label = NULL;
      g_ui.dialog_play_status = NULL;
      g_ui.dialog_type = ROUTINE_UI_DIALOG_NONE;
      g_ui.dialog_is_due = false;
      routine_try_open_due();
    }
}

static void routine_dialog_cancel_cb(lv_event_t *event)
{
  if (g_ui.dialog_is_due && g_ui.schedule_action != NULL)
    {
      if (g_ui.schedule_action(g_ui.user_data, g_ui.selected_schedule,
                               g_ui.dialog_is_due ?
                               g_ui.active_due.sequence : 0,
                               ROUTINE_SCHEDULE_DISMISS) < 0)
        {
          return;
        }
    }

  routine_close_dialog();
  routine_try_open_due();
}

static void routine_style_dialog(lv_obj_t *dialog)
{
  lv_obj_set_width(dialog, 430);
  lv_obj_set_style_bg_color(dialog, ROUTINE_COLOR_CARD, 0);
  lv_obj_set_style_border_color(dialog, ROUTINE_COLOR_GRID, 0);
  lv_obj_set_style_text_color(dialog, ROUTINE_COLOR_TEXT, 0);
}

static lv_obj_t *routine_add_dialog_button(lv_obj_t *dialog,
                                           const char *text,
                                           lv_event_cb_t callback,
                                           void *user_data)
{
  lv_obj_t *button = lv_msgbox_add_footer_button(dialog, text);
  lv_obj_t *label = lv_obj_get_child(button, 0);

  lv_obj_set_height(button, 44);
  if (label != NULL)
    {
      lv_obj_set_style_text_font(label,
                                 routine_text_font(&lv_font_montserrat_12), 0);
    }

  lv_obj_add_event_cb(button, callback, LV_EVENT_SHORT_CLICKED, user_data);
  return button;
}

static void routine_schedule_action_event_cb(lv_event_t *event)
{
  enum routine_schedule_action_e action =
    (enum routine_schedule_action_e)(uintptr_t)lv_event_get_user_data(event);

  if (g_ui.schedule_action != NULL &&
      g_ui.schedule_action(g_ui.user_data, g_ui.selected_schedule,
                           g_ui.dialog_is_due ?
                           g_ui.active_due.sequence : 0, action) < 0)
    {
      return;
    }

  routine_close_dialog();
  routine_try_open_due();
}

static void routine_open_schedule_dialog(uint8_t index)
{
  const struct routine_schedule_item_s *item;
  lv_obj_t *title;
  lv_obj_t *text;

  if (index >= g_ui.snapshot.schedule_count)
    {
      return;
    }

  routine_close_dialog();
  item = &g_ui.snapshot.schedule[index];
  g_ui.selected_schedule = item->id;
  g_ui.dialog_type = ROUTINE_UI_DIALOG_SCHEDULE;
  g_ui.dialog_is_due = false;
  g_ui.dialog = lv_msgbox_create(NULL);
  if (g_ui.dialog == NULL)
    {
      g_ui.dialog_type = ROUTINE_UI_DIALOG_NONE;
      return;
    }

  routine_style_dialog(g_ui.dialog);
  lv_obj_add_event_cb(g_ui.dialog, routine_dialog_delete_cb,
                      LV_EVENT_DELETE, NULL);
  title = lv_msgbox_add_title(g_ui.dialog, g_ui.text->schedule_action);
  lv_obj_set_style_text_font(title,
                             routine_text_font(&lv_font_montserrat_16), 0);
  snprintf(g_ui.dialog_buffer, sizeof(g_ui.dialog_buffer), "%s  %02u:%02u",
           routine_schedule_text(item->text, item->text_id),
           item->hour, item->minute);
  text = lv_msgbox_add_text(g_ui.dialog, g_ui.dialog_buffer);
  lv_obj_set_style_text_font(text,
                             routine_text_font(&lv_font_montserrat_14), 0);

  if (!item->completed)
    {
      routine_add_dialog_button(g_ui.dialog, g_ui.text->done,
                                routine_schedule_action_event_cb,
                                (void *)(uintptr_t)ROUTINE_SCHEDULE_COMPLETE);
      routine_add_dialog_button(
        g_ui.dialog, g_ui.text->delay, routine_schedule_action_event_cb,
        (void *)(uintptr_t)ROUTINE_SCHEDULE_DELAY_10_MINUTES);
    }

  routine_add_dialog_button(g_ui.dialog, g_ui.text->cancel,
                            routine_dialog_cancel_cb, NULL);
}

static void routine_open_due_dialog(const struct routine_due_event_s *event)
{
  lv_obj_t *title;
  lv_obj_t *text;

  g_ui.selected_schedule = event->schedule_id;
  g_ui.active_due = *event;
  g_ui.dialog_type = ROUTINE_UI_DIALOG_SCHEDULE;
  g_ui.dialog_is_due = true;
  g_ui.dialog = lv_msgbox_create(NULL);
  if (g_ui.dialog == NULL)
    {
      g_ui.dialog_type = ROUTINE_UI_DIALOG_NONE;
      g_ui.dialog_is_due = false;
      return;
    }

  routine_style_dialog(g_ui.dialog);
  lv_obj_add_event_cb(g_ui.dialog, routine_dialog_delete_cb,
                      LV_EVENT_DELETE, NULL);
  title = lv_msgbox_add_title(g_ui.dialog, g_ui.text->schedule_due);
  lv_obj_set_style_text_font(title,
                             routine_text_font(&lv_font_montserrat_16), 0);
  snprintf(g_ui.dialog_buffer, sizeof(g_ui.dialog_buffer),
           "%s  %02u:%02u  (%u/%u)",
           routine_schedule_text(event->text, event->text_id),
           event->hour, event->minute,
           event->reminder_count, ROUTINE_REMINDER_LIMIT);
  text = lv_msgbox_add_text(g_ui.dialog, g_ui.dialog_buffer);
  lv_obj_set_style_text_font(text,
                             routine_text_font(&lv_font_montserrat_14), 0);
  routine_add_dialog_button(g_ui.dialog, g_ui.text->done,
                            routine_schedule_action_event_cb,
                            (void *)(uintptr_t)ROUTINE_SCHEDULE_COMPLETE);
  if (event->reminder_count < ROUTINE_REMINDER_LIMIT)
    {
      routine_add_dialog_button(
        g_ui.dialog, g_ui.text->delay, routine_schedule_action_event_cb,
        (void *)(uintptr_t)ROUTINE_SCHEDULE_DELAY_10_MINUTES);
    }
  routine_add_dialog_button(g_ui.dialog, g_ui.text->cancel,
                            routine_dialog_cancel_cb, NULL);
}

static void routine_try_open_due(void)
{
  struct routine_due_event_s event;

  if (g_ui.dialog != NULL || g_ui.due_count == 0)
    {
      return;
    }

  event = g_ui.due_queue[g_ui.due_head];
  g_ui.due_head = (g_ui.due_head + 1) % ROUTINE_DUE_UI_QUEUE_MAX;
  g_ui.due_count--;
  routine_open_due_dialog(&event);
}

static bool routine_due_equal(const struct routine_due_event_s *left,
                              const struct routine_due_event_s *right)
{
  return left->sequence == right->sequence &&
         left->schedule_id == right->schedule_id;
}

int routine_ui_enqueue_due(const struct routine_due_event_s *event)
{
  uint8_t tail;
  uint8_t i;

  if (event == NULL || event->sequence == 0 ||
      event->schedule_id >= ROUTINE_SCHEDULE_MAX)
    {
      return -EINVAL;
    }

  for (i = 0; i < g_ui.due_count; i++)
    {
      uint8_t index = (g_ui.due_head + i) % ROUTINE_DUE_UI_QUEUE_MAX;
      if (routine_due_equal(&g_ui.due_queue[index], event))
        {
          return -EALREADY;
        }
    }

  if (g_ui.dialog_is_due && routine_due_equal(&g_ui.active_due, event))
    {
      return -EALREADY;
    }

  if (g_ui.due_count >= ROUTINE_DUE_UI_QUEUE_MAX)
    {
      return -ENOSPC;
    }

  tail = (g_ui.due_head + g_ui.due_count) % ROUTINE_DUE_UI_QUEUE_MAX;
  g_ui.due_queue[tail] = *event;
  g_ui.due_count++;
  routine_try_open_due();
  return OK;
}

void routine_ui_remove_due(const struct routine_due_event_s *event)
{
  struct routine_due_event_s queue[ROUTINE_DUE_UI_QUEUE_MAX];
  uint8_t kept = 0;
  uint8_t i;

  if (event == NULL || event->sequence == 0)
    {
      return;
    }

  for (i = 0; i < g_ui.due_count; i++)
    {
      uint8_t index = (g_ui.due_head + i) % ROUTINE_DUE_UI_QUEUE_MAX;
      if (!routine_due_equal(&g_ui.due_queue[index], event))
        {
          queue[kept++] = g_ui.due_queue[index];
        }
    }

  memset(g_ui.due_queue, 0, sizeof(g_ui.due_queue));
  memcpy(g_ui.due_queue, queue, kept * sizeof(queue[0]));
  g_ui.due_head = 0;
  g_ui.due_count = kept;
}

void routine_ui_clear_due(void)
{
  memset(g_ui.due_queue, 0, sizeof(g_ui.due_queue));
  g_ui.due_head = 0;
  g_ui.due_count = 0;
  if (g_ui.dialog_is_due)
    {
      routine_close_dialog();
    }
}

static void routine_format_sensor_details(enum routine_ui_dialog_e type)
{
  const struct routine_snapshot_s *snapshot = &g_ui.snapshot;
  uint32_t mask = routine_dialog_mask(type);
  enum routine_sensor_state_e status = routine_metric_status(snapshot, mask);
  char temperature[24];

  routine_format_signed_x100(temperature, sizeof(temperature),
                             snapshot->temperature_x100, " C");
  if (type == ROUTINE_UI_DIALOG_TEMPERATURE)
    {
      snprintf(g_ui.dialog_buffer, sizeof(g_ui.dialog_buffer),
               "SHT40  %s\n%s  %s\n%s",
               g_ui.text->status[status], g_ui.text->detail_temperature,
               temperature, g_ui.text->status_help[status]);
    }
  else if (type == ROUTINE_UI_DIALOG_HUMIDITY)
    {
      snprintf(g_ui.dialog_buffer, sizeof(g_ui.dialog_buffer),
               "SHT40  %s\n%s  %u.%02u%%\n%s",
               g_ui.text->status[status], g_ui.text->detail_humidity,
               snapshot->humidity_x100 / 100,
               snapshot->humidity_x100 % 100,
               g_ui.text->status_help[status]);
    }
  else if (type == ROUTINE_UI_DIALOG_BH1750)
    {
      snprintf(g_ui.dialog_buffer, sizeof(g_ui.dialog_buffer),
               "BH1750  %s\n%s  %lu.%lu lux\n%s",
               g_ui.text->status[status], g_ui.text->detail_light,
               (unsigned long)(snapshot->light_x10 / 10),
               (unsigned long)(snapshot->light_x10 % 10),
               g_ui.text->status_help[status]);
    }
  else
    {
      snprintf(g_ui.dialog_buffer, sizeof(g_ui.dialog_buffer),
               "SGP30  %s\neCO2  %u ppm\nTVOC  %u ppb\n%s",
               g_ui.text->status[status], snapshot->eco2_ppm,
               snapshot->tvoc_ppb, g_ui.text->status_help[status]);
    }
}

static void routine_sync_dialog_play_state(void)
{
  enum audio_service_request_e request;
  enum routine_sensor_state_e status;
  bool owns_active;

  if (g_ui.dialog_play_button == NULL ||
      !routine_dialog_is_sensor(g_ui.dialog_type))
    {
      return;
    }

  request = routine_dialog_request(g_ui.dialog_type);
  owns_active = g_ui.voice_active && g_ui.active_request == request;
  status = routine_metric_status(&g_ui.snapshot,
                                 routine_dialog_mask(g_ui.dialog_type));
  if (owns_active || (!g_ui.voice_active && status == ROUTINE_SENSOR_LIVE))
    {
      lv_obj_clear_state(g_ui.dialog_play_button, LV_STATE_DISABLED);
    }
  else
    {
      lv_obj_add_state(g_ui.dialog_play_button, LV_STATE_DISABLED);
      if (g_ui.dialog_play_status != NULL)
        {
          lv_label_set_text_static(
            g_ui.dialog_play_status,
            g_ui.voice_active ? g_ui.text->voice_busy :
            status == ROUTINE_SENSOR_OFFLINE ?
            g_ui.text->voice_errors[AUDIO_SERVICE_ERROR_SENSOR_UNAVAILABLE] :
            g_ui.text->status_help[status]);
        }
    }
}

static void routine_sensor_play_cb(lv_event_t *event)
{
  enum audio_service_request_e request = routine_dialog_request(g_ui.dialog_type);
  bool stop = g_ui.voice_active && g_ui.active_request == request;
  int ret = -ENODEV;

  if (g_ui.voice_action != NULL)
    {
      ret = g_ui.voice_action(g_ui.user_data, request, !stop);
    }

  if (ret < 0 && g_ui.dialog_play_status != NULL)
    {
      lv_label_set_text_static(
        g_ui.dialog_play_status,
        ret == -EBUSY ? g_ui.text->voice_busy :
        ret == -ENODATA ? g_ui.text->voice_errors[
          AUDIO_SERVICE_ERROR_SENSOR_UNAVAILABLE] :
        g_ui.text->voice_errors[AUDIO_SERVICE_ERROR_PLAYBACK]);
    }
}

static void routine_open_sensor_dialog(enum routine_ui_dialog_e type)
{
  lv_obj_t *title;

  routine_close_dialog();
  g_ui.dialog_type = type;
  g_ui.dialog = lv_msgbox_create(NULL);
  if (g_ui.dialog == NULL)
    {
      g_ui.dialog_type = ROUTINE_UI_DIALOG_NONE;
      return;
    }

  routine_style_dialog(g_ui.dialog);
  lv_obj_add_event_cb(g_ui.dialog, routine_dialog_delete_cb,
                      LV_EVENT_DELETE, NULL);
  title = lv_msgbox_add_title(g_ui.dialog, g_ui.text->detail_title);
  lv_obj_set_style_text_font(title,
                             routine_text_font(&lv_font_montserrat_16), 0);
  routine_format_sensor_details(type);
  g_ui.dialog_text = lv_msgbox_add_text(g_ui.dialog, g_ui.dialog_buffer);
  lv_obj_set_style_text_font(g_ui.dialog_text,
                             routine_text_font(&lv_font_montserrat_12), 0);

  g_ui.dialog_play_status = lv_msgbox_add_text(g_ui.dialog,
                                               g_ui.text->voice_idle);
  lv_obj_set_style_text_font(g_ui.dialog_play_status,
                             routine_text_font(&lv_font_montserrat_12), 0);
  lv_obj_set_style_text_color(g_ui.dialog_play_status,
                              ROUTINE_COLOR_BLUE, 0);
  g_ui.dialog_play_button = routine_add_dialog_button(
    g_ui.dialog, g_ui.text->play_data, routine_sensor_play_cb, NULL);
  g_ui.dialog_play_label = lv_obj_get_child(g_ui.dialog_play_button, 0);

  routine_sync_dialog_play_state();

  routine_add_dialog_button(g_ui.dialog, g_ui.text->cancel,
                            routine_dialog_cancel_cb, NULL);
}

static void routine_schedule_row_cb(lv_event_t *event)
{
  routine_open_schedule_dialog(
    (uint8_t)(uintptr_t)lv_event_get_user_data(event));
}

static void routine_sensor_event_cb(lv_event_t *event)
{
  static const enum routine_ui_dialog_e dialog[ROUTINE_SENSOR_CARD_COUNT] =
  {
    ROUTINE_UI_DIALOG_TEMPERATURE,
    ROUTINE_UI_DIALOG_HUMIDITY,
    ROUTINE_UI_DIALOG_BH1750,
    ROUTINE_UI_DIALOG_SGP30
  };
  uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(event);

  if (index < ROUTINE_SENSOR_CARD_COUNT)
    {
      routine_open_sensor_dialog(dialog[index]);
    }
}

static void routine_format_history_interval(char *buffer, size_t size,
                                            uint32_t seconds)
{
  if (seconds < 60)
    {
      snprintf(buffer, size,
               g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE ? "%lu秒" :
                                                              "%lus",
               (unsigned long)seconds);
    }
  else if (seconds >= 3600 && seconds % 3600 == 0)
    {
      snprintf(buffer, size,
               g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE ? "%lu小时" :
                                                              "%luh",
               (unsigned long)(seconds / 3600));
    }
  else
    {
      snprintf(buffer, size,
               g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE ? "%lu分钟" :
                                                              "%lumin",
               (unsigned long)(seconds / 60));
    }
}

static void routine_refresh_history_dialog(const char *status)
{
  char interval[32];

  if (g_ui.dialog_type != ROUTINE_UI_DIALOG_HISTORY ||
      g_ui.dialog_text == NULL)
    {
      return;
    }

  routine_format_history_interval(interval, sizeof(interval),
                                  g_ui.snapshot.history_interval_seconds);
  if (g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE)
    {
      snprintf(g_ui.dialog_buffer, sizeof(g_ui.dialog_buffer),
               "记录间隔：%s\n“查看”打开温度运行曲线。\n"
               "“导出”把全部历史数据复制到SD卡。%s%s",
               interval, status != NULL && status[0] != '\0' ? "\n" : "",
               status == NULL ? "" : status);
    }
  else
    {
      snprintf(g_ui.dialog_buffer, sizeof(g_ui.dialog_buffer),
               "Record interval: %s\nView opens the temperature curve.\n"
               "Export copies all history to the SD card.%s%s",
               interval, status != NULL && status[0] != '\0' ? "\n" : "",
               status == NULL ? "" : status);
    }
  lv_label_set_text_static(g_ui.dialog_text, g_ui.dialog_buffer);
}

static void routine_history_interval_cb(lv_event_t *event)
{
  static const uint32_t intervals[] = {5, 10, 60, 300, 600, 1800, 3600};
  char result[32];
  char status[96];
  uint32_t current = g_ui.snapshot.history_interval_seconds;
  uint32_t next = intervals[0];
  size_t i;
  int ret = -ENODEV;

  (void)event;
  for (i = 0; i < sizeof(intervals) / sizeof(intervals[0]); i++)
    {
      if (current == intervals[i])
        {
          next = intervals[(i + 1) %
                           (sizeof(intervals) / sizeof(intervals[0]))];
          break;
        }
    }

  if (g_ui.history_action != NULL)
    {
      ret = g_ui.history_action(g_ui.user_data,
                                ROUTINE_UI_HISTORY_SET_INTERVAL, next,
                                result, sizeof(result));
    }
  if (ret == OK)
    {
      g_ui.snapshot.history_interval_seconds = next;
      snprintf(status, sizeof(status),
               g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE ?
               "记录间隔已保存。" : "Record interval saved.");
    }
  else
    {
      snprintf(status, sizeof(status),
               g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE ?
               "保存失败（%d）。" : "Save failed (%d).", -ret);
    }
  routine_refresh_history_dialog(status);
}

static void routine_history_export_cb(lv_event_t *event)
{
  char result[192];
  char status[256];
  int ret = -ENODEV;

  (void)event;
  result[0] = '\0';
  if (g_ui.history_action != NULL)
    {
      ret = g_ui.history_action(g_ui.user_data,
                                ROUTINE_UI_HISTORY_EXPORT_SD,
                                g_ui.snapshot.history_interval_seconds,
                                result, sizeof(result));
    }

  if (ret == OK)
    {
      snprintf(status, sizeof(status),
               g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE ?
               "已导出：%s" : "Exported: %s", result);
    }
  else if (ret == -ENODEV || ret == -ENOTDIR)
    {
      snprintf(status, sizeof(status),
               g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE ?
               "未检测到SD卡。" : "SD card not found.");
    }
  else if (ret == -ENODATA)
    {
      snprintf(status, sizeof(status),
               g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE ?
               "暂无可导出的历史数据。" : "No history to export.");
    }
  else
    {
      snprintf(status, sizeof(status),
               g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE ?
               "导出失败（%d）。" : "Export failed (%d).", -ret);
    }
  routine_refresh_history_dialog(status);
}

static void routine_history_view_cb(lv_event_t *event)
{
  (void)event;
  routine_close_dialog();
  lv_tileview_set_tile(g_ui.tileview, g_ui.history_page, LV_ANIM_OFF);
}

static void routine_open_history_dialog(void)
{
  lv_obj_t *title;

  routine_close_dialog();
  g_ui.dialog_type = ROUTINE_UI_DIALOG_HISTORY;
  g_ui.dialog = lv_msgbox_create(NULL);
  if (g_ui.dialog == NULL)
    {
      g_ui.dialog_type = ROUTINE_UI_DIALOG_NONE;
      return;
    }

  routine_style_dialog(g_ui.dialog);
  lv_obj_add_event_cb(g_ui.dialog, routine_dialog_delete_cb,
                      LV_EVENT_DELETE, NULL);
  title = lv_msgbox_add_title(
    g_ui.dialog, g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE ?
                 "运行曲线" : "RUNNING CURVE");
  lv_obj_set_style_text_font(title,
                             routine_text_font(&lv_font_montserrat_16), 0);
  g_ui.dialog_text = lv_msgbox_add_text(g_ui.dialog, "");
  lv_obj_set_style_text_font(g_ui.dialog_text,
                             routine_text_font(&lv_font_montserrat_12), 0);
  routine_refresh_history_dialog(NULL);
  routine_add_dialog_button(
    g_ui.dialog,
    g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE ? "查看" : "VIEW",
    routine_history_view_cb, NULL);
  routine_add_dialog_button(
    g_ui.dialog,
    g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE ? "间隔" : "INTERVAL",
    routine_history_interval_cb, NULL);
  routine_add_dialog_button(
    g_ui.dialog,
    g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE ? "导出" : "EXPORT",
    routine_history_export_cb, NULL);
  routine_add_dialog_button(g_ui.dialog, g_ui.text->cancel,
                            routine_dialog_cancel_cb, NULL);
}

static void routine_curve_button_cb(lv_event_t *event)
{
  (void)event;
  routine_open_history_dialog();
}

static void routine_page_changed_cb(lv_event_t *event)
{
  lv_obj_t *active = lv_tileview_get_tile_active(g_ui.tileview);

  g_ui.current_page = active == g_ui.history_page ? 1 : 0;
  lv_label_set_text_static(g_ui.page_label,
                           g_ui.current_page ? g_ui.text->page_history :
                                               g_ui.text->page_sensor);
  if (g_ui.current_page == 0)
    {
      lv_obj_add_state(g_ui.page_prev, LV_STATE_DISABLED);
      lv_obj_clear_state(g_ui.page_next, LV_STATE_DISABLED);
    }
  else
    {
      lv_obj_clear_state(g_ui.page_prev, LV_STATE_DISABLED);
      lv_obj_add_state(g_ui.page_next, LV_STATE_DISABLED);
    }
}

static void routine_page_button_cb(lv_event_t *event)
{
  static uint32_t last_page_tick;
  bool next = (bool)(uintptr_t)lv_event_get_user_data(event);
  uint32_t now = lv_tick_get();

  if ((last_page_tick != 0 && now - last_page_tick < 500) ||
      next == (g_ui.current_page != 0))
    {
      return;
    }

  last_page_tick = now;
  lv_tileview_set_tile(g_ui.tileview,
                       next ? g_ui.history_page : g_ui.sensor_page,
                       LV_ANIM_OFF);
}

static lv_obj_t *routine_create_small_button(lv_obj_t *parent, int32_t x,
                                             int32_t y, int32_t width,
                                             int32_t height,
                                             const char *text,
                                             lv_event_cb_t callback,
                                             void *user_data)
{
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_t *label;

  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, width, height);
  lv_obj_set_style_bg_color(button, ROUTINE_COLOR_CARD_ALT, 0);
  lv_obj_set_style_bg_color(button, ROUTINE_COLOR_BLUE, LV_STATE_PRESSED);
  lv_obj_set_style_radius(button, 5, 0);
  label = routine_create_label(button,
                               strcmp(text, LV_SYMBOL_WIFI) == 0 ?
                               &lv_font_montserrat_14 :
                               routine_text_font(&lv_font_montserrat_14),
                               ROUTINE_COLOR_TEXT);
  lv_label_set_text_static(label, text);
  lv_obj_center(label);
  lv_obj_add_event_cb(button, callback, LV_EVENT_SHORT_CLICKED, user_data);
  return button;
}

/* Wi-Fi 按钮点击事件 */
static void routine_wifi_button_cb(lv_event_t *event)
{
  (void)event;

  if (g_ui.wifi_panel != NULL)
    {
      wifi_ui_show(g_ui.wifi_panel);
      /* Re-check on every visit so a standalone device can show its IP and
       * four-digit web token again without requiring a serial console.
       */
      wifi_status_monitor_check_now();
    }
}

/* Wi-Fi 连接回调 */
static void routine_wifi_connect_cb(const char *ssid, const char *password,
                                    void *user_data)
{
  char config_path[] = "/data/wifi.cfg";
  FILE *f;

  (void)user_data;

  /* 写入配置文件 */
  f = fopen(config_path, "w");
  if (f == NULL)
    {
      return;
    }

  fprintf(f, "SSID=\"%s\"\n", ssid);
  fprintf(f, "PASSWORD=\"%s\"\n", password);
  fclose(f);

  /* 更新屏幕状态 */
  if (g_ui.wifi_panel != NULL)
    {
      const char *msg = g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE ?
                        "配置已保存，正在连接..." :
                        "Config saved, connecting...";
      wifi_ui_set_status(g_ui.wifi_panel, msg);
    }

  /* 触发 WiFi 状态立即检查 */
  wifi_status_monitor_check_now();

  syslog(LOG_INFO, "routine_mgr: Wi-Fi 配置已保存 SSID=%s\n", ssid);
}

static void routine_create_header(lv_obj_t *parent)
{
  lv_obj_t *title = routine_create_label(parent,
    routine_text_font(&lv_font_montserrat_16), ROUTINE_COLOR_TEXT);

  lv_label_set_text_static(title, g_ui.text->title);
  lv_obj_set_pos(title, 8, 5);

  /* Wi-Fi 设置按钮 */
  g_ui.wifi_button = routine_create_small_button(
    parent, 120, ROUTINE_UI_NAV_BUTTON_Y,
    30, ROUTINE_UI_NAV_BUTTON_HEIGHT,
    LV_SYMBOL_WIFI, routine_wifi_button_cb, NULL);

  g_ui.curve_button = routine_create_small_button(
    parent, 154, ROUTINE_UI_NAV_BUTTON_Y, 58,
    ROUTINE_UI_NAV_BUTTON_HEIGHT,
    g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE ? "曲线" : "CURVE",
    routine_curve_button_cb, NULL);

  g_ui.page_prev = routine_create_small_button(
    parent, ROUTINE_UI_NAV_PREV_X, ROUTINE_UI_NAV_BUTTON_Y,
    ROUTINE_UI_NAV_BUTTON_WIDTH, ROUTINE_UI_NAV_BUTTON_HEIGHT,
    "<", routine_page_button_cb, (void *)(uintptr_t)false);
  g_ui.page_label = routine_create_label(
    parent, routine_text_font(&lv_font_montserrat_10), ROUTINE_COLOR_SECONDARY);
  lv_obj_set_width(g_ui.page_label, 104);
  lv_obj_set_style_text_align(g_ui.page_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(g_ui.page_label, 316, 12);
  lv_label_set_text_static(g_ui.page_label, g_ui.text->page_sensor);
  g_ui.page_next = routine_create_small_button(
    parent, ROUTINE_UI_NAV_NEXT_X, ROUTINE_UI_NAV_BUTTON_Y,
    ROUTINE_UI_NAV_BUTTON_WIDTH, ROUTINE_UI_NAV_BUTTON_HEIGHT,
    ">", routine_page_button_cb, (void *)(uintptr_t)true);
  g_ui.time_label = routine_create_label(parent, &lv_font_montserrat_14,
                                         ROUTINE_COLOR_TEXT);
  lv_obj_set_pos(g_ui.time_label, 218, 6);
  lv_label_set_text_static(g_ui.time_label, g_ui.time_buffer);
  lv_obj_add_state(g_ui.page_prev, LV_STATE_DISABLED);
}

static void routine_create_sensor_card(lv_obj_t *parent, uint8_t index,
                                       int32_t x, int32_t y)
{
  lv_obj_t *panel = routine_create_panel(parent, x, y,
                                          ROUTINE_UI_CARD_WIDTH,
                                          ROUTINE_UI_CARD_HEIGHT);
  lv_obj_t *title;

  g_ui.cards[index] = panel;
  lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(panel, ROUTINE_COLOR_CARD_ALT, LV_STATE_PRESSED);
  lv_obj_add_event_cb(panel, routine_sensor_event_cb, LV_EVENT_SHORT_CLICKED,
                      (void *)(uintptr_t)index);
  title = routine_create_label(panel, routine_text_font(&lv_font_montserrat_12),
                               ROUTINE_COLOR_SECONDARY);
  lv_label_set_text_static(title, g_ui.text->card_titles[index]);
  lv_obj_set_pos(title, 9, 6);
  g_ui.value_labels[index] = routine_create_label(
    panel, index == 3 ? &lv_font_montserrat_14 : &lv_font_montserrat_20,
    ROUTINE_COLOR_TEXT);
  lv_obj_set_pos(g_ui.value_labels[index], 9, 31);
  lv_label_set_text_static(g_ui.value_labels[index], g_ui.value_buffer[index]);
  g_ui.subvalue_labels[index] = routine_create_label(
    panel, &lv_font_montserrat_10, ROUTINE_COLOR_SECONDARY);
  lv_obj_set_pos(g_ui.subvalue_labels[index], 9, 54);
  lv_label_set_text_static(g_ui.subvalue_labels[index],
                           g_ui.subvalue_buffer[index]);
  g_ui.card_status[index] = routine_create_label(
    panel, routine_text_font(&lv_font_montserrat_10), ROUTINE_COLOR_MUTED);
  lv_obj_set_width(g_ui.card_status[index], 118);
  lv_obj_set_style_text_align(g_ui.card_status[index], LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(g_ui.card_status[index], 101, 7);
}

static void routine_voice_button_cb(lv_event_t *event)
{
  int ret;

  if (g_ui.voice_action != NULL)
    {
      ret = g_ui.voice_action(g_ui.user_data, AUDIO_SERVICE_REQUEST_QUERY,
                              !g_ui.voice_active);
      if (ret == -EBUSY)
        {
          lv_label_set_text_static(g_ui.voice_status, g_ui.text->voice_busy);
        }
      else if (ret < 0)
        {
          lv_label_set_text_static(
            g_ui.voice_status,
            g_ui.text->voice_errors[AUDIO_SERVICE_ERROR_PLAYBACK]);
        }
    }
}

static void routine_create_sensor_page(lv_obj_t *parent)
{
  lv_obj_t *voice;

  routine_create_sensor_card(parent, 0, ROUTINE_UI_CARD_LEFT_X,
                             ROUTINE_UI_CARD_FIRST_Y);
  routine_create_sensor_card(parent, 1, ROUTINE_UI_CARD_RIGHT_X,
                             ROUTINE_UI_CARD_FIRST_Y);
  routine_create_sensor_card(parent, 2, ROUTINE_UI_CARD_LEFT_X,
                             ROUTINE_UI_CARD_SECOND_Y);
  routine_create_sensor_card(parent, 3, ROUTINE_UI_CARD_RIGHT_X,
                             ROUTINE_UI_CARD_SECOND_Y);

  voice = routine_create_panel(parent, 6, ROUTINE_UI_VOICE_Y, 464,
                               ROUTINE_UI_VOICE_HEIGHT);
  g_ui.voice_status = routine_create_label(
    voice, routine_text_font(&lv_font_montserrat_10), ROUTINE_COLOR_SECONDARY);
  lv_label_set_text_static(g_ui.voice_status, g_ui.text->voice_idle);
  lv_obj_set_pos(g_ui.voice_status, 10, 8);
  g_ui.voice_button = lv_button_create(voice);
  lv_obj_set_pos(g_ui.voice_button, 330, 8);
  lv_obj_set_size(g_ui.voice_button, 124, 44);
  lv_obj_set_style_bg_color(g_ui.voice_button, ROUTINE_COLOR_BLUE, 0);
  lv_obj_set_style_radius(g_ui.voice_button, 5, 0);
  lv_obj_add_event_cb(g_ui.voice_button, routine_voice_button_cb,
                      LV_EVENT_SHORT_CLICKED, NULL);
  g_ui.voice_button_label = routine_create_label(
    g_ui.voice_button, routine_text_font(&lv_font_montserrat_10),
    ROUTINE_COLOR_TEXT);
  lv_label_set_text_static(g_ui.voice_button_label, g_ui.text->voice_start);
  lv_obj_center(g_ui.voice_button_label);
  g_ui.voice_result = routine_create_label(
    voice, routine_text_font(&lv_font_montserrat_8), ROUTINE_COLOR_ORANGE);
  lv_obj_set_pos(g_ui.voice_result, 10, 34);
  lv_obj_set_width(g_ui.voice_result, 306);
  lv_label_set_long_mode(g_ui.voice_result, LV_LABEL_LONG_WRAP);
  lv_label_set_text_static(g_ui.voice_result, g_ui.text->voice_demo);
}

static void routine_history_button_cb(lv_event_t *event)
{
  int direction = (int)(intptr_t)lv_event_get_user_data(event);
  uint8_t count = g_ui.snapshot.temperature_history_count;
  uint8_t latest = count > ROUTINE_CHART_POINTS ?
                   count - ROUTINE_CHART_POINTS : 0;

  if (direction < 0)
    {
      g_ui.history_start = g_ui.history_start >= ROUTINE_CHART_POINTS ?
                           g_ui.history_start - ROUTINE_CHART_POINTS : 0;
      g_ui.history_follow_latest = false;
    }
  else
    {
      uint8_t next = g_ui.history_start + ROUTINE_CHART_POINTS;
      g_ui.history_start = next < latest ? next : latest;
      g_ui.history_follow_latest = g_ui.history_start == latest;
    }

  g_ui.history_dirty = true;
  routine_ui_update(&g_ui.snapshot);
}

static void routine_chart_event_cb(lv_event_t *event)
{
  if (lv_event_get_code(event) == LV_EVENT_RELEASED ||
      lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED)
    {
      uint32_t id = lv_chart_get_pressed_point(g_ui.chart);
      uint8_t history_id;
      int32_t value;

      if (id == LV_CHART_POINT_NONE || id >= ROUTINE_CHART_POINTS)
        {
          return;
        }

      history_id = g_ui.history_start + id;
      if (history_id >= g_ui.snapshot.temperature_history_count)
        {
          return;
        }

      value = g_ui.snapshot.temperature_history[history_id];
      if (value == ROUTINE_HISTORY_INVALID)
        {
          snprintf(g_ui.chart_selection_buffer,
                   sizeof(g_ui.chart_selection_buffer),
                   "%02u:%02u:%02u  --",
                   g_ui.snapshot.temperature_history_hour[history_id],
                   g_ui.snapshot.temperature_history_minute[history_id],
                   g_ui.snapshot.temperature_history_second[history_id]);
        }
      else
        {
          char temperature[20];
          routine_format_signed_x100(temperature, sizeof(temperature),
                                     value, " C");
          snprintf(g_ui.chart_selection_buffer,
                   sizeof(g_ui.chart_selection_buffer),
                   "%02u:%02u:%02u  %s",
                   g_ui.snapshot.temperature_history_hour[history_id],
                   g_ui.snapshot.temperature_history_minute[history_id],
                   g_ui.snapshot.temperature_history_second[history_id],
                   temperature);
        }

      lv_label_set_text_static(g_ui.chart_selection,
                               g_ui.chart_selection_buffer);
    }
}

static void routine_create_chart(lv_obj_t *parent)
{
  lv_obj_t *panel = routine_create_panel(parent, 6, ROUTINE_UI_CHART_Y,
                                          464, ROUTINE_UI_CHART_HEIGHT);
  lv_obj_t *title = routine_create_label(
    panel, routine_text_font(&lv_font_montserrat_12), ROUTINE_COLOR_TEXT);
  uint8_t i;

  lv_label_set_text_static(title, g_ui.text->chart_title);
  lv_obj_set_pos(title, 8, 5);
  g_ui.chart_prev = routine_create_small_button(panel, 258, 3, 44, 36,
                                                 "<", routine_history_button_cb,
                                                 (void *)(intptr_t)-1);
  g_ui.chart_range = routine_create_label(
    panel, &lv_font_montserrat_10, ROUTINE_COLOR_SECONDARY);
  lv_obj_set_width(g_ui.chart_range, 100);
  lv_obj_set_style_text_align(g_ui.chart_range, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(g_ui.chart_range, 306, 12);
  g_ui.chart_next = routine_create_small_button(panel, 410, 3, 44, 36,
                                                 ">", routine_history_button_cb,
                                                 (void *)(intptr_t)1);
  g_ui.chart_selection = routine_create_label(
    panel, &lv_font_montserrat_10, ROUTINE_COLOR_SECONDARY);
  lv_obj_set_pos(g_ui.chart_selection, 8, 27);
  lv_obj_set_width(g_ui.chart_selection, 230);

  g_ui.chart = lv_chart_create(panel);
  lv_obj_set_pos(g_ui.chart, 8, 47);
  lv_obj_set_size(g_ui.chart, 446, 68);
  lv_obj_add_flag(g_ui.chart, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_ui.chart, routine_chart_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_set_style_bg_opa(g_ui.chart, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(g_ui.chart, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(g_ui.chart, 2, LV_PART_MAIN);
  lv_obj_set_style_line_color(g_ui.chart, ROUTINE_COLOR_GRID, LV_PART_MAIN);
  lv_obj_set_style_line_width(g_ui.chart, 1, LV_PART_MAIN);
  lv_obj_set_style_line_color(g_ui.chart, ROUTINE_COLOR_BLUE, LV_PART_ITEMS);
  lv_obj_set_style_line_width(g_ui.chart, 2, LV_PART_ITEMS);
  lv_obj_set_style_size(g_ui.chart, 6, 6, LV_PART_INDICATOR);
  lv_chart_set_type(g_ui.chart, LV_CHART_TYPE_LINE);
  lv_chart_set_div_line_count(g_ui.chart, 3, 5);
  lv_chart_set_range(g_ui.chart, LV_CHART_AXIS_PRIMARY_Y, 1800, 3000);
  lv_chart_set_point_count(g_ui.chart, ROUTINE_CHART_POINTS);
  g_ui.temperature_series = lv_chart_add_series(
    g_ui.chart, ROUTINE_COLOR_BLUE, LV_CHART_AXIS_PRIMARY_Y);

  for (i = 0; i < ROUTINE_CHART_POINTS; i++)
    {
      int32_t point_x = 10 +
                        i * 442 / (ROUTINE_CHART_POINTS - 1);
      int32_t label_x = point_x - 28;

      if (label_x < 0)
        {
          label_x = 0;
        }
      else if (label_x > 408)
        {
          label_x = 408;
        }

      g_ui.chart_value[i] = routine_create_label(
        panel, &lv_font_montserrat_8, ROUTINE_COLOR_TEXT);
      lv_obj_set_width(g_ui.chart_value[i], 56);
      lv_obj_set_style_text_align(g_ui.chart_value[i],
                                  LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(g_ui.chart_value[i], label_x, 51);
      lv_label_set_text_static(g_ui.chart_value[i],
                               g_ui.chart_value_buffer[i]);
      lv_obj_add_flag(g_ui.chart_value[i], LV_OBJ_FLAG_HIDDEN);

      g_ui.chart_axis[i] = routine_create_label(
        panel, &lv_font_montserrat_8, ROUTINE_COLOR_MUTED);
      lv_obj_set_width(g_ui.chart_axis[i], 56);
      lv_obj_set_style_text_align(g_ui.chart_axis[i],
                                  LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(g_ui.chart_axis[i], label_x, 117);
      lv_label_set_text_static(g_ui.chart_axis[i], g_ui.chart_axis_buffer[i]);
    }
}

static void routine_create_schedule(lv_obj_t *parent)
{
  lv_obj_t *panel = routine_create_panel(parent, 6, ROUTINE_UI_SCHEDULE_Y,
                                          464, ROUTINE_UI_SCHEDULE_HEIGHT);
  lv_obj_t *title = routine_create_label(
    panel, routine_text_font(&lv_font_montserrat_12), ROUTINE_COLOR_TEXT);
  uint8_t i;

  lv_label_set_text_static(title, g_ui.text->schedule_title);
  lv_obj_set_pos(title, 8, 5);
  for (i = 0; i < ROUTINE_SCHEDULE_MAX; i++)
    {
      lv_obj_t *row = routine_create_panel(panel, 8 + i * 150, 28, 142, 94);
      g_ui.schedule_rows[i] = row;
      lv_obj_set_style_bg_color(row, ROUTINE_COLOR_CARD_ALT, 0);
      lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(row, routine_schedule_row_cb, LV_EVENT_SHORT_CLICKED,
                          (void *)(uintptr_t)i);
      g_ui.schedule_bars[i] = lv_obj_create(row);
      lv_obj_remove_style_all(g_ui.schedule_bars[i]);
      lv_obj_set_size(g_ui.schedule_bars[i], 4, 94);
      lv_obj_set_style_bg_color(g_ui.schedule_bars[i], ROUTINE_COLOR_ORANGE, 0);
      g_ui.schedule_time[i] = routine_create_label(
        row, &lv_font_montserrat_16, ROUTINE_COLOR_TEXT);
      lv_obj_set_pos(g_ui.schedule_time[i], 13, 10);
      g_ui.schedule_text[i] = routine_create_label(
        row, routine_text_font(&lv_font_montserrat_12), ROUTINE_COLOR_SECONDARY);
      lv_obj_set_pos(g_ui.schedule_text[i], 13, 39);
      g_ui.schedule_next[i] = routine_create_label(
        row, routine_text_font(&lv_font_montserrat_10), ROUTINE_COLOR_MUTED);
      lv_obj_set_pos(g_ui.schedule_next[i], 13, 67);
    }
}

static int routine_load_font(const struct routine_ui_options_s *options)
{
  if (options->language == ROUTINE_UI_LANGUAGE_ENGLISH)
    {
      return OK;
    }

#if LV_USE_FS_POSIX
  char path[160];
  int length = snprintf(path, sizeof(path), "%c:%s",
                        (char)LV_FS_POSIX_LETTER, options->font_path);
  if (length < 0 || length >= sizeof(path))
    {
      return -ENAMETOOLONG;
    }

  g_ui.text_font = lv_binfont_create(path);
  if (g_ui.text_font == NULL)
    {
      syslog(LOG_ERR,
             "routine_mgr: cannot load Chinese font %s; use --english\n",
             path);
      return -ENOENT;
    }
#else
  return -ENOSYS;
#endif

  return OK;
}

int routine_ui_init(lv_display_t *display,
                    const struct routine_ui_options_s *options,
                    const struct routine_snapshot_s *snapshot)
{
  int ret;

  if (display == NULL || options == NULL || snapshot == NULL)
    {
      return -EINVAL;
    }

  memset(&g_ui, 0, sizeof(g_ui));
  g_ui.small_font = &lv_font_montserrat_10;
  g_ui.language = options->language;
  g_ui.text = options->language == ROUTINE_UI_LANGUAGE_CHINESE ?
              &g_chinese_text : &g_english_text;
  g_ui.schedule_action = options->schedule_action;
  g_ui.voice_action = options->voice_action;
  g_ui.history_action = options->history_action;
  g_ui.history_follow_latest = true;
  g_ui.chart_min = INT32_MIN;
  g_ui.chart_max = INT32_MIN;
  g_ui.user_data = options->user_data;
  ret = routine_load_font(options);
  if (ret < 0)
    {
      routine_ui_deinit();
      return ret;
    }

  g_ui.root = lv_obj_create(lv_screen_active());
  if (g_ui.root == NULL)
    {
      routine_ui_deinit();
      return -ENOMEM;
    }

  lv_obj_remove_style_all(g_ui.root);
  lv_obj_set_size(g_ui.root, ROUTINE_UI_SCREEN_WIDTH,
                  ROUTINE_UI_SCREEN_HEIGHT);
  lv_obj_set_style_bg_color(g_ui.root, ROUTINE_COLOR_SCREEN, 0);
  lv_obj_set_style_bg_opa(g_ui.root, LV_OPA_COVER, 0);
  lv_obj_clear_flag(g_ui.root, LV_OBJ_FLAG_SCROLLABLE);
  routine_create_header(g_ui.root);

  g_ui.tileview = lv_tileview_create(g_ui.root);
  lv_obj_set_pos(g_ui.tileview, 0, ROUTINE_UI_HEADER_HEIGHT);
  lv_obj_set_size(g_ui.tileview, ROUTINE_UI_SCREEN_WIDTH,
                  ROUTINE_UI_BODY_HEIGHT);
  lv_obj_set_style_bg_color(g_ui.tileview, ROUTINE_COLOR_SCREEN, 0);
  lv_obj_set_style_bg_opa(g_ui.tileview, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_ui.tileview, 0, 0);
  lv_obj_set_style_pad_all(g_ui.tileview, 0, 0);
  g_ui.sensor_page = lv_tileview_add_tile(g_ui.tileview, 0, 0, LV_DIR_RIGHT);
  g_ui.history_page = lv_tileview_add_tile(g_ui.tileview, 1, 0, LV_DIR_LEFT);
  lv_obj_set_style_bg_color(g_ui.sensor_page, ROUTINE_COLOR_SCREEN, 0);
  lv_obj_set_style_bg_color(g_ui.history_page, ROUTINE_COLOR_SCREEN, 0);
  lv_obj_set_style_pad_all(g_ui.sensor_page, 0, 0);
  lv_obj_set_style_pad_all(g_ui.history_page, 0, 0);
  lv_obj_add_event_cb(g_ui.tileview, routine_page_changed_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  routine_create_sensor_page(g_ui.sensor_page);
  routine_create_chart(g_ui.history_page);
  routine_create_schedule(g_ui.history_page);

  /* 创建 Wi-Fi 配置面板 */
  struct wifi_ui_options_s wifi_opts = {
    .connect_callback = routine_wifi_connect_cb,
    .user_data = NULL,
    .chinese = (g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE),
    .font = g_ui.text_font ? g_ui.text_font : &lv_font_montserrat_14
  };
  g_ui.wifi_panel = wifi_ui_create(g_ui.root, &wifi_opts);

  /* 启动 WiFi 状态监控 */
  wifi_status_monitor_start(g_ui.wifi_panel,
                            (g_ui.language == ROUTINE_UI_LANGUAGE_CHINESE));

  routine_ui_update(snapshot);
  return OK;
}

static void routine_chart_visible_range(int32_t *minimum, int32_t *maximum)
{
  int32_t low = 0;
  int32_t high = 0;
  bool found = false;
  uint8_t i;

  for (i = 0; i < ROUTINE_CHART_POINTS; i++)
    {
      uint8_t id = g_ui.history_start + i;
      int32_t value;

      if (id >= g_ui.snapshot.temperature_history_count)
        {
          continue;
        }

      value = g_ui.snapshot.temperature_history[id];
      if (value == ROUTINE_HISTORY_INVALID)
        {
          continue;
        }

      if (!found || value < low)
        {
          low = value;
        }

      if (!found || value > high)
        {
          high = value;
        }

      found = true;
    }

  if (!found)
    {
      *minimum = 1800;
      *maximum = 3000;
      return;
    }

  if (high - low < 200)
    {
      int32_t center = low + (high - low) / 2;
      low = center - 100;
      high = center + 100;
    }
  else
    {
      int32_t padding = (high - low) / 10;
      padding = padding < 100 ? 100 : padding;
      low -= padding;
      high += padding;
    }

  *minimum = low < -4500 ? -4500 : low;
  *maximum = high > 13000 ? 13000 : high;
  if (*minimum >= *maximum)
    {
      *minimum = *maximum - 100;
    }
}

void routine_ui_update(const struct routine_snapshot_s *snapshot)
{
  static const uint32_t mask[ROUTINE_SENSOR_CARD_COUNT] =
  {
    ENV_METRIC_TEMPERATURE,
    ENV_METRIC_HUMIDITY,
    ENV_METRIC_LIGHT,
    ENV_METRIC_ECO2 | ENV_METRIC_TVOC
  };
  enum routine_sensor_state_e status;
  uint8_t count;
  uint8_t latest;
  uint8_t i;

  if (g_ui.root == NULL || snapshot == NULL)
    {
      return;
    }

  g_ui.snapshot = *snapshot;
  if (snapshot->clock_valid)
    {
      snprintf(g_ui.time_buffer, sizeof(g_ui.time_buffer), "%02u:%02u",
               snapshot->hour, snapshot->minute);
    }
  else
    {
      snprintf(g_ui.time_buffer, sizeof(g_ui.time_buffer), "--:--");
    }
  lv_label_set_text_static(g_ui.time_label, g_ui.time_buffer);

  routine_format_signed_x100(g_ui.value_buffer[0],
                             sizeof(g_ui.value_buffer[0]),
                             snapshot->temperature_x100, " C");
  snprintf(g_ui.value_buffer[1], sizeof(g_ui.value_buffer[1]), "%u.%02u%%",
           snapshot->humidity_x100 / 100, snapshot->humidity_x100 % 100);
  snprintf(g_ui.value_buffer[2], sizeof(g_ui.value_buffer[2]), "%lu.%lu lux",
           (unsigned long)(snapshot->light_x10 / 10),
           (unsigned long)(snapshot->light_x10 % 10));
  snprintf(g_ui.value_buffer[3], sizeof(g_ui.value_buffer[3]), "eCO2 %u ppm",
           snapshot->eco2_ppm);
  snprintf(g_ui.subvalue_buffer[0], sizeof(g_ui.subvalue_buffer[0]), "SHT40");
  snprintf(g_ui.subvalue_buffer[1], sizeof(g_ui.subvalue_buffer[1]), "SHT40");
  snprintf(g_ui.subvalue_buffer[2], sizeof(g_ui.subvalue_buffer[2]), "BH1750");
  snprintf(g_ui.subvalue_buffer[3], sizeof(g_ui.subvalue_buffer[3]),
           "TVOC %u ppb", snapshot->tvoc_ppb);

  for (i = 0; i < ROUTINE_SENSOR_CARD_COUNT; i++)
    {
      status = routine_metric_status(snapshot, mask[i]);
      if (status == ROUTINE_SENSOR_OFFLINE ||
          status == ROUTINE_SENSOR_STALE ||
          (status == ROUTINE_SENSOR_WARMUP && i == 3))
        {
          snprintf(g_ui.value_buffer[i], sizeof(g_ui.value_buffer[i]), "--");
          if (i == 3)
            {
              snprintf(g_ui.subvalue_buffer[i],
                       sizeof(g_ui.subvalue_buffer[i]), "SGP30");
            }
        }

      lv_label_set_text_static(g_ui.value_labels[i], g_ui.value_buffer[i]);
      lv_label_set_text_static(g_ui.subvalue_labels[i],
                               g_ui.subvalue_buffer[i]);
      lv_label_set_text_static(g_ui.card_status[i], g_ui.text->status[status]);
      lv_obj_set_style_text_color(g_ui.card_status[i],
                                  routine_status_color(status), 0);
    }

  count = snapshot->temperature_history_count;
  latest = count > ROUTINE_CHART_POINTS ? count - ROUTINE_CHART_POINTS : 0;
  if (g_ui.history_start > latest)
    {
      g_ui.history_start = latest;
    }
  else if (g_ui.history_follow_latest)
    {
      g_ui.history_start = latest;
    }

  for (i = 0; i < ROUTINE_CHART_POINTS; i++)
    {
      uint8_t id = g_ui.history_start + i;
      int32_t value = id < count ? snapshot->temperature_history[id] :
                                   ROUTINE_HISTORY_INVALID;
      lv_chart_set_value_by_id(g_ui.chart, g_ui.temperature_series, i,
                               value == ROUTINE_HISTORY_INVALID ?
                               LV_CHART_POINT_NONE : value);
      if (id < count)
        {
          snprintf(g_ui.chart_axis_buffer[i],
                   sizeof(g_ui.chart_axis_buffer[i]), "%02u:%02u:%02u",
                   snapshot->temperature_history_hour[id],
                   snapshot->temperature_history_minute[id],
                   snapshot->temperature_history_second[id]);
        }
      else
        {
          snprintf(g_ui.chart_axis_buffer[i],
                   sizeof(g_ui.chart_axis_buffer[i]), "--");
        }
      lv_label_set_text_static(g_ui.chart_axis[i], g_ui.chart_axis_buffer[i]);
    }

  {
    int32_t chart_min;
    int32_t chart_max;

    routine_chart_visible_range(&chart_min, &chart_max);
    if (chart_min != g_ui.chart_min || chart_max != g_ui.chart_max)
      {
        g_ui.chart_min = chart_min;
        g_ui.chart_max = chart_max;
        lv_chart_set_range(g_ui.chart, LV_CHART_AXIS_PRIMARY_Y,
                           chart_min, chart_max);
      }
  }

  for (i = 0; i < ROUTINE_CHART_POINTS; i++)
    {
      uint8_t id = g_ui.history_start + i;
      int32_t value = id < count ? snapshot->temperature_history[id] :
                                   ROUTINE_HISTORY_INVALID;

      if (value == ROUTINE_HISTORY_INVALID ||
          g_ui.chart_max <= g_ui.chart_min)
        {
          lv_obj_add_flag(g_ui.chart_value[i], LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          int32_t point_y = 49 +
            (g_ui.chart_max - value) * 62 /
            (g_ui.chart_max - g_ui.chart_min);
          int32_t label_y = point_y - 11;

          if (label_y < 39)
            {
              label_y = point_y + 2;
            }
          if (label_y > 104)
            {
              label_y = 104;
            }

          routine_format_signed_x100(g_ui.chart_value_buffer[i],
                                     sizeof(g_ui.chart_value_buffer[i]),
                                     value, "");
          lv_label_set_text_static(g_ui.chart_value[i],
                                   g_ui.chart_value_buffer[i]);
          lv_obj_set_y(g_ui.chart_value[i], label_y);
          lv_obj_clear_flag(g_ui.chart_value[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

  if (count > 0)
    {
      uint8_t end = g_ui.history_start + ROUTINE_CHART_POINTS;
      if (end > count)
        {
          end = count;
        }
      snprintf(g_ui.chart_range_buffer, sizeof(g_ui.chart_range_buffer),
               "%u-%u / %u", g_ui.history_start + 1, end, count);
    }
  else
    {
      snprintf(g_ui.chart_range_buffer, sizeof(g_ui.chart_range_buffer),
               "0 / 0");
    }
  lv_label_set_text_static(g_ui.chart_range, g_ui.chart_range_buffer);
  if (g_ui.history_start == 0)
    {
      lv_obj_add_state(g_ui.chart_prev, LV_STATE_DISABLED);
    }
  else
    {
      lv_obj_clear_state(g_ui.chart_prev, LV_STATE_DISABLED);
    }
  if (g_ui.history_start >= latest)
    {
      lv_obj_add_state(g_ui.chart_next, LV_STATE_DISABLED);
    }
  else
    {
      lv_obj_clear_state(g_ui.chart_next, LV_STATE_DISABLED);
    }

  for (i = 0; i < ROUTINE_SCHEDULE_MAX; i++)
    {
      if (snapshot->clock_valid)
        {
          lv_obj_clear_state(g_ui.schedule_rows[i], LV_STATE_DISABLED);
        }
      else
        {
          lv_obj_add_state(g_ui.schedule_rows[i], LV_STATE_DISABLED);
        }

      snprintf(g_ui.schedule_time_buffer[i],
               sizeof(g_ui.schedule_time_buffer[i]), "%02u:%02u",
               snapshot->schedule[i].hour, snapshot->schedule[i].minute);
      lv_label_set_text_static(g_ui.schedule_time[i],
                               g_ui.schedule_time_buffer[i]);
      lv_label_set_text(g_ui.schedule_text[i],
                        routine_schedule_text(snapshot->schedule[i].text,
                                              snapshot->schedule[i].text_id));
      if (snapshot->schedule[i].completed)
        {
          lv_obj_set_style_bg_color(g_ui.schedule_bars[i],
                                    ROUTINE_COLOR_GREEN, 0);
          lv_label_set_text_static(g_ui.schedule_next[i],
                                   g_ui.text->completed);
        }
      else if (snapshot->next_schedule == i)
        {
          lv_obj_set_style_bg_color(g_ui.schedule_bars[i],
                                    ROUTINE_COLOR_ORANGE, 0);
          lv_label_set_text_static(g_ui.schedule_next[i], g_ui.text->next);
        }
      else
        {
          lv_obj_set_style_bg_color(g_ui.schedule_bars[i],
                                    ROUTINE_COLOR_GRID, 0);
          lv_label_set_text_static(g_ui.schedule_next[i], "");
        }
    }

  if (g_ui.dialog_text != NULL &&
      routine_dialog_is_sensor(g_ui.dialog_type))
    {
      routine_format_sensor_details(g_ui.dialog_type);
      lv_label_set_text_static(g_ui.dialog_text, g_ui.dialog_buffer);
      routine_sync_dialog_play_state();
    }
}

void routine_ui_voice_update(
  const struct audio_service_snapshot_s *snapshot)
{
  const char *status;
  bool active;

  if (snapshot == NULL || g_ui.voice_status == NULL ||
      snapshot->sequence == g_ui.last_audio_sequence)
    {
      return;
    }

  g_ui.last_audio_sequence = snapshot->sequence;
  active = snapshot->state == AUDIO_SERVICE_LISTENING ||
           snapshot->state == AUDIO_SERVICE_PROCESSING ||
           snapshot->state == AUDIO_SERVICE_SPEAKING;
  if (snapshot->state == AUDIO_SERVICE_LISTENING)
    {
      status = g_ui.text->voice_listening;
    }
  else if (snapshot->state == AUDIO_SERVICE_PROCESSING)
    {
      status = g_ui.text->voice_processing;
    }
  else if (snapshot->state == AUDIO_SERVICE_SPEAKING)
    {
      status = g_ui.text->voice_speaking;
    }
  else if (snapshot->state == AUDIO_SERVICE_ERROR &&
           snapshot->error <= AUDIO_SERVICE_ERROR_SENSOR_UNAVAILABLE)
    {
      status = g_ui.text->voice_errors[snapshot->error];
    }
  else if (snapshot->pending_reminders > 0)
    {
      status = g_ui.text->voice_processing;
    }
  else
    {
      status = g_ui.text->voice_idle;
    }

  g_ui.voice_active = active;
  g_ui.active_request = snapshot->request;
  lv_label_set_text_static(g_ui.voice_status, status);
  lv_label_set_text_static(g_ui.voice_button_label,
                           active ? g_ui.text->voice_stop :
                                    g_ui.text->voice_start);
  lv_obj_set_style_bg_color(g_ui.voice_button,
                            active ? ROUTINE_COLOR_ORANGE :
                                     ROUTINE_COLOR_BLUE, 0);
  lv_obj_center(g_ui.voice_button_label);

  if (snapshot->has_result &&
      snapshot->result_request != AUDIO_SERVICE_REQUEST_NONE &&
      (snapshot->request == AUDIO_SERVICE_REQUEST_QUERY ||
       snapshot->request == AUDIO_SERVICE_REQUEST_NONE))
    {
      switch (snapshot->result_request)
        {
          case AUDIO_SERVICE_REQUEST_TEMPERATURE:
            {
              int32_t magnitude = snapshot->values.temperature_x100 < 0 ?
                                  -snapshot->values.temperature_x100 :
                                  snapshot->values.temperature_x100;
              const char *sign = snapshot->values.temperature_x100 < 0 ?
                                 "-" : "";
              snprintf(g_ui.voice_result_buffer,
                       sizeof(g_ui.voice_result_buffer),
                       "%s  %s%ld.%02ld C", g_ui.text->card_titles[0], sign,
                       (long)(magnitude / 100), (long)(magnitude % 100));
            }
            break;

          case AUDIO_SERVICE_REQUEST_HUMIDITY:
            snprintf(g_ui.voice_result_buffer,
                     sizeof(g_ui.voice_result_buffer), "%s  %u.%02u%%",
                     g_ui.text->card_titles[1],
                     snapshot->values.humidity_x100 / 100,
                     snapshot->values.humidity_x100 % 100);
            break;

          case AUDIO_SERVICE_REQUEST_BH1750:
            snprintf(g_ui.voice_result_buffer,
                     sizeof(g_ui.voice_result_buffer), "%s  %lu.%lu lux",
                     g_ui.text->card_titles[2],
                     (unsigned long)(snapshot->values.light_x10 / 10),
                     (unsigned long)(snapshot->values.light_x10 % 10));
            break;

          case AUDIO_SERVICE_REQUEST_SGP30:
            snprintf(g_ui.voice_result_buffer,
                     sizeof(g_ui.voice_result_buffer),
                     "%s  eCO2 %u ppm\nTVOC %u ppb",
                     g_ui.text->card_titles[3], snapshot->values.eco2_ppm,
                     snapshot->values.tvoc_ppb);
            break;

          default:
            snprintf(g_ui.voice_result_buffer,
                     sizeof(g_ui.voice_result_buffer), "%s",
                     g_ui.text->voice_done);
            break;
        }

      lv_label_set_text_static(g_ui.voice_result, g_ui.voice_result_buffer);
    }
  else if (snapshot->request != AUDIO_SERVICE_REQUEST_QUERY)
    {
      lv_label_set_text_static(g_ui.voice_result,
                               active ? status : g_ui.text->voice_demo);
    }
  else
    {
      lv_label_set_text_static(g_ui.voice_result, g_ui.text->voice_demo);
    }

  if (g_ui.dialog_play_status != NULL &&
      routine_dialog_is_sensor(g_ui.dialog_type))
    {
      enum audio_service_request_e request =
        routine_dialog_request(g_ui.dialog_type);
      bool owns = snapshot->request == request;
      const char *dialog_status = status;

      if (owns && !active)
        {
          dialog_status = snapshot->outcome == AUDIO_SERVICE_OUTCOME_COMPLETED ?
                          g_ui.text->voice_done :
                          snapshot->outcome == AUDIO_SERVICE_OUTCOME_CANCELLED ?
                          g_ui.text->voice_stopped : status;
        }
      lv_label_set_text_static(g_ui.dialog_play_status,
                               owns ? dialog_status : g_ui.text->voice_idle);
      if (g_ui.dialog_play_label != NULL)
        {
          lv_label_set_text_static(g_ui.dialog_play_label,
                                   owns && active ? g_ui.text->voice_stop :
                                                    g_ui.text->play_data);
          lv_obj_center(g_ui.dialog_play_label);
        }
      if (owns && active)
        {
          lv_obj_set_style_bg_color(g_ui.dialog_play_button,
                                    ROUTINE_COLOR_ORANGE, 0);
        }
      else
        {
          lv_obj_set_style_bg_color(g_ui.dialog_play_button,
                                    ROUTINE_COLOR_BLUE, 0);
        }

      routine_sync_dialog_play_state();
    }
}

void routine_ui_deinit(void)
{
  if (g_ui.dialog != NULL)
    {
      lv_msgbox_close(g_ui.dialog);
      g_ui.dialog = NULL;
    }
  if (g_ui.root != NULL)
    {
      lv_obj_delete(g_ui.root);
    }
  if (g_ui.text_font != NULL)
    {
      lv_binfont_destroy(g_ui.text_font);
    }
  memset(&g_ui, 0, sizeof(g_ui));
}
