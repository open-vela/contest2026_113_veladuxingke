/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/wifi_ui.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>

#include <lvgl/lvgl.h>
#include <wireless/wapi.h>

#include "wifi_ui.h"

#define WIFI_UI_MAX_NETWORKS 10
#define WIFI_UI_SCAN_TIMEOUT_MS 5000
#define WIFI_INTERFACE "wlan0"

struct wifi_network_s
{
  char ssid[32];
  int8_t rssi;
  bool secured;
};

struct wifi_ui_context_s
{
  lv_obj_t *panel;
  lv_obj_t *scan_list;
  lv_obj_t *scan_button;
  lv_obj_t *status_label;
  lv_obj_t *password_panel;
  lv_obj_t *password_ta;
  lv_obj_t *keyboard;
  lv_obj_t *connect_button;
  lv_obj_t *cancel_button;
  lv_obj_t *connection_dialog;
  wifi_ui_connect_cb_t connect_callback;
  void *user_data;
  const lv_font_t *font;
  bool chinese;
  bool scanning;
  char selected_ssid[32];
  char connection_info[256];
  struct wifi_network_s networks[WIFI_UI_MAX_NETWORKS];
  int network_count;
};

static void wifi_ui_scan_networks(struct wifi_ui_context_s *ctx);
static void wifi_ui_show_password_dialog(struct wifi_ui_context_s *ctx,
                                         const char *ssid);

/* 关闭按钮点击事件 */
static void wifi_ui_close_clicked(lv_event_t *e)
{
  struct wifi_ui_context_s *ctx = lv_event_get_user_data(e);

  if (ctx->panel != NULL)
    {
      lv_obj_add_flag(ctx->panel, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 扫描按钮点击事件 */
static void wifi_ui_scan_clicked(lv_event_t *e)
{
  struct wifi_ui_context_s *ctx = lv_event_get_user_data(e);

  if (ctx->scanning)
    {
      return;
    }

  wifi_ui_scan_networks(ctx);
}

/* 网络列表项点击事件 */
static void wifi_ui_network_clicked(lv_event_t *e)
{
  struct wifi_ui_context_s *ctx = lv_event_get_user_data(e);
  lv_obj_t *btn = lv_event_get_target(e);
  uint32_t index = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);

  if (index < ctx->network_count)
    {
      strncpy(ctx->selected_ssid, ctx->networks[index].ssid,
              sizeof(ctx->selected_ssid) - 1);
      ctx->selected_ssid[sizeof(ctx->selected_ssid) - 1] = '\0';

      wifi_ui_show_password_dialog(ctx, ctx->selected_ssid);
    }
}

/* 连接按钮点击事件 */
static void wifi_ui_connect_clicked(lv_event_t *e)
{
  struct wifi_ui_context_s *ctx = lv_event_get_user_data(e);
  const char *password = lv_textarea_get_text(ctx->password_ta);

  if (ctx->connect_callback != NULL)
    {
      ctx->connect_callback(ctx->selected_ssid, password, ctx->user_data);
    }

  /* 隐藏密码对话框 */
  lv_obj_add_flag(ctx->password_panel, LV_OBJ_FLAG_HIDDEN);

  /* 显示连接中状态 */
  const char *msg = ctx->chinese ? "配置已保存，Wi-Fi管理器正在连接..." :
                                   "Config saved, connecting...";
  lv_label_set_text(ctx->status_label, msg);

  /* 保留在 Wi-Fi 页面，让用户看到连接过程与结果。 */
}

/* 取消按钮点击事件 */
static void wifi_ui_cancel_clicked(lv_event_t *e)
{
  struct wifi_ui_context_s *ctx = lv_event_get_user_data(e);

  /* 隐藏密码对话框 */
  lv_obj_add_flag(ctx->password_panel, LV_OBJ_FLAG_HIDDEN);
}

/* 扫描 Wi-Fi 网络 */
static void wifi_ui_scan_networks(struct wifi_ui_context_s *ctx)
{
  int sockfd;
  int ret;
  int i;
  struct wapi_list_s list;
  struct wapi_scan_info_s *scan_info;

  ctx->scanning = true;
  ctx->network_count = 0;

  const char *msg = ctx->chinese ? "扫描中..." : "Scanning...";
  lv_label_set_text(ctx->status_label, msg);
  lv_obj_clear_flag(ctx->scan_list, LV_OBJ_FLAG_HIDDEN);

  /* 清空列表 */
  lv_obj_clean(ctx->scan_list);

  sockfd = socket(PF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0)
    {
      msg = ctx->chinese ? "错误：无法打开套接字" : "Error: Cannot open socket";
      lv_label_set_text(ctx->status_label, msg);
      ctx->scanning = false;
      return;
    }

  /* 初始化扫描 - NULL 表示扫描所有网络 */
  ret = wapi_scan_init(sockfd, WIFI_INTERFACE, NULL);
  if (ret < 0)
    {
      msg = ctx->chinese ? "错误：扫描初始化失败" : "Error: Scan init failed";
      lv_label_set_text(ctx->status_label, msg);
      close(sockfd);
      ctx->scanning = false;
      return;
    }

  /* 等待扫描完成 */
  sleep(3);

  /* 检查扫描状态 */
  ret = wapi_scan_stat(sockfd, WIFI_INTERFACE);
  if (ret < 0)
    {
      msg = ctx->chinese ? "错误：扫描失败" : "Error: Scan failed";
      lv_label_set_text(ctx->status_label, msg);
      close(sockfd);
      ctx->scanning = false;
      return;
    }

  /* 获取扫描结果 */
  memset(&list, 0, sizeof(list));
  ret = wapi_scan_coll(sockfd, WIFI_INTERFACE, &list);
  close(sockfd);

  if (ret < 0)
    {
      msg = ctx->chinese ? "错误：获取结果失败" : "Error: Get results failed";
      lv_label_set_text(ctx->status_label, msg);
      ctx->scanning = false;
      return;
    }

  /* 解析结果 */
  scan_info = list.head.scan;
  while (scan_info != NULL && ctx->network_count < WIFI_UI_MAX_NETWORKS)
    {
      if (scan_info->essid[0] != '\0')
        {
          /* 检查是否已存在（去重） */
          bool duplicate = false;
          for (i = 0; i < ctx->network_count; i++)
            {
              if (strcmp(ctx->networks[i].ssid, scan_info->essid) == 0)
                {
                  duplicate = true;
                  break;
                }
            }

          if (!duplicate)
            {
              strncpy(ctx->networks[ctx->network_count].ssid, scan_info->essid,
                      sizeof(ctx->networks[0].ssid) - 1);
              ctx->networks[ctx->network_count].ssid[sizeof(ctx->networks[0].ssid) - 1] = '\0';
              ctx->networks[ctx->network_count].rssi = scan_info->rssi;
              /* 假设有加密（安全起见） */
              ctx->networks[ctx->network_count].secured = true;
              ctx->network_count++;
            }
        }

      scan_info = scan_info->next;
    }

  /* 释放扫描结果 */
  wapi_scan_coll_free(&list);

  /* 更新列表显示 */
  for (i = 0; i < ctx->network_count; i++)
    {
      lv_obj_t *btn = lv_list_add_button(ctx->scan_list, LV_SYMBOL_WIFI,
                                         ctx->networks[i].ssid);
      lv_obj_t *icon = lv_obj_get_child(btn, 0);
      if (icon != NULL)
        {
          /* LV_SYMBOL_WIFI lives in the built-in symbol font.  The optional
           * Chinese font used for list text does not carry this private-use
           * glyph and renders a square instead. */
          lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
        }
      lv_obj_set_user_data(btn, (void *)(uintptr_t)i);
      lv_obj_add_event_cb(btn, wifi_ui_network_clicked, LV_EVENT_CLICKED, ctx);

      /* 添加信号强度和加密标识 */
      lv_obj_t *label = lv_obj_get_child(btn, 1);
      if (label != NULL)
        {
          char buf[64];
          const char *lock_icon = ctx->networks[i].secured ? "🔒" : "";
          snprintf(buf, sizeof(buf), "%s  %s  %ddBm",
                   ctx->networks[i].ssid,
                   lock_icon,
                   ctx->networks[i].rssi);
          lv_label_set_text(label, buf);
          lv_obj_set_style_text_font(label, ctx->font, 0);
        }
    }

  if (ctx->network_count == 0)
    {
      msg = ctx->chinese ? "未发现网络" : "No networks found";
      lv_label_set_text(ctx->status_label, msg);
    }
  else
    {
      char buf[64];
      if (ctx->chinese)
        {
          snprintf(buf, sizeof(buf), "发现 %d 个网络", ctx->network_count);
        }
      else
        {
          snprintf(buf, sizeof(buf), "Found %d networks", ctx->network_count);
        }
      lv_label_set_text(ctx->status_label, buf);
    }

  ctx->scanning = false;
}

/* 显示密码输入对话框 */
static void wifi_ui_show_password_dialog(struct wifi_ui_context_s *ctx,
                                         const char *ssid)
{
  char buf[64];

  /* 更新标题 */
  if (ctx->chinese)
    {
      snprintf(buf, sizeof(buf), "连接到: %s", ssid);
    }
  else
    {
      snprintf(buf, sizeof(buf), "Connect to: %s", ssid);
    }

  lv_obj_t *title = lv_obj_get_child(ctx->password_panel, 0);
  if (title != NULL)
    {
      lv_label_set_text(title, buf);
    }

  /* 清空密码 */
  lv_textarea_set_text(ctx->password_ta, "");

  /* 显示对话框 */
  lv_obj_clear_flag(ctx->password_panel, LV_OBJ_FLAG_HIDDEN);

  /* 聚焦到密码输入框 */
  lv_group_focus_obj(ctx->password_ta);
}

/* 创建 Wi-Fi UI */
lv_obj_t *wifi_ui_create(lv_obj_t *parent,
                         const struct wifi_ui_options_s *options)
{
  struct wifi_ui_context_s *ctx;
  lv_obj_t *panel;
  lv_obj_t *title;
  lv_obj_t *scan_container;

  ctx = malloc(sizeof(struct wifi_ui_context_s));
  if (ctx == NULL)
    {
      return NULL;
    }

  memset(ctx, 0, sizeof(*ctx));
  ctx->connect_callback = options->connect_callback;
  ctx->user_data = options->user_data;
  ctx->chinese = options->chinese;
  ctx->font = options->font ? options->font : &lv_font_montserrat_14;

  /* 创建主面板 */
  panel = lv_obj_create(parent);
  lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x171a1d), 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, 10, 0);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
  ctx->panel = panel;

  /* 标题 */
  title = lv_label_create(panel);
  lv_label_set_text(title, ctx->chinese ? "Wi-Fi 配置" : "WiFi Setup");
  lv_obj_set_style_text_font(title, ctx->font, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xf5f7f8), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 5);

  /* 关闭按钮 */
  lv_obj_t *close_btn = lv_button_create(panel);
  lv_obj_set_size(close_btn, 40, 30);
  lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -5, 5);
  lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xd94f55), 0);
  lv_obj_add_event_cb(close_btn, wifi_ui_close_clicked, LV_EVENT_CLICKED, ctx);

  lv_obj_t *close_label = lv_label_create(close_btn);
  lv_label_set_text(close_label, "X");
  lv_obj_center(close_label);

  /* 扫描按钮 */
  scan_container = lv_obj_create(panel);
  lv_obj_set_size(scan_container, LV_PCT(100), 40);
  lv_obj_set_style_bg_color(scan_container, lv_color_hex(0x23282e), 0);
  lv_obj_set_style_border_width(scan_container, 0, 0);
  lv_obj_align(scan_container, LV_ALIGN_TOP_MID, 0, 40);

  ctx->scan_button = lv_button_create(scan_container);
  lv_obj_set_size(ctx->scan_button, 80, 35);
  lv_obj_align(ctx->scan_button, LV_ALIGN_LEFT_MID, 5, 0);
  lv_obj_add_event_cb(ctx->scan_button, wifi_ui_scan_clicked,
                      LV_EVENT_CLICKED, ctx);

  lv_obj_t *scan_label = lv_label_create(ctx->scan_button);
  lv_label_set_text(scan_label, ctx->chinese ? "扫描" : "Scan");
  lv_obj_set_style_text_font(scan_label, ctx->font, 0);
  lv_obj_center(scan_label);

  /* 状态标签 */
  ctx->status_label = lv_label_create(scan_container);
  lv_label_set_text(ctx->status_label,
                    ctx->chinese ? "点击扫描查找网络" : "Click Scan to find networks");
  lv_obj_set_style_text_font(ctx->status_label, ctx->font, 0);
  lv_obj_set_style_text_color(ctx->status_label, lv_color_hex(0x87919a), 0);
  lv_obj_align(ctx->status_label, LV_ALIGN_RIGHT_MID, -5, 0);

  /* 网络列表 */
  ctx->scan_list = lv_list_create(panel);
  lv_obj_set_size(ctx->scan_list, LV_PCT(100), 200);
  lv_obj_align(ctx->scan_list, LV_ALIGN_TOP_MID, 0, 90);
  lv_obj_set_style_bg_color(ctx->scan_list, lv_color_hex(0x23282e), 0);
  lv_obj_add_flag(ctx->scan_list, LV_OBJ_FLAG_HIDDEN);

  /* 密码输入面板 */
  ctx->password_panel = lv_obj_create(panel);
  lv_obj_set_size(ctx->password_panel, LV_PCT(100), LV_PCT(100));
  lv_obj_center(ctx->password_panel);
  lv_obj_set_style_bg_color(ctx->password_panel, lv_color_hex(0x23282e), 0);
  lv_obj_add_flag(ctx->password_panel, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *pwd_title = lv_label_create(ctx->password_panel);
  lv_label_set_text(pwd_title, ctx->chinese ? "输入密码" : "Enter Password");
  lv_obj_set_style_text_font(pwd_title, ctx->font, 0);
  lv_obj_align(pwd_title, LV_ALIGN_TOP_MID, 0, 3);

  /* 密码输入框 - 位置更靠上 */
  ctx->password_ta = lv_textarea_create(ctx->password_panel);
  lv_obj_set_size(ctx->password_ta, LV_PCT(85), 40);
  lv_obj_align(ctx->password_ta, LV_ALIGN_TOP_MID, 0, 25);
  lv_textarea_set_password_mode(ctx->password_ta, false);
  lv_textarea_set_one_line(ctx->password_ta, true);
  lv_textarea_set_placeholder_text(ctx->password_ta, ctx->chinese ? "密码" : "Password");
  lv_obj_set_style_text_font(ctx->password_ta, &lv_font_montserrat_18, 0);
  lv_obj_set_style_pad_all(ctx->password_ta, 6, 0);

  /* 连接和取消按钮 - 放在输入框下方 */
  ctx->connect_button = lv_button_create(ctx->password_panel);
  lv_obj_set_size(ctx->connect_button, 90, 35);
  lv_obj_align(ctx->connect_button, LV_ALIGN_TOP_LEFT, 10, 75);
  lv_obj_add_event_cb(ctx->connect_button, wifi_ui_connect_clicked, LV_EVENT_CLICKED, ctx);
  lv_obj_set_style_bg_color(ctx->connect_button, lv_color_hex(0x24a879), 0);

  lv_obj_t *conn_label = lv_label_create(ctx->connect_button);
  lv_label_set_text(conn_label, ctx->chinese ? "连接" : "Connect");
  lv_obj_set_style_text_font(conn_label, ctx->font, 0);
  lv_obj_center(conn_label);

  ctx->cancel_button = lv_button_create(ctx->password_panel);
  lv_obj_set_size(ctx->cancel_button, 90, 35);
  lv_obj_align(ctx->cancel_button, LV_ALIGN_TOP_RIGHT, -10, 75);
  lv_obj_add_event_cb(ctx->cancel_button, wifi_ui_cancel_clicked, LV_EVENT_CLICKED, ctx);
  lv_obj_set_style_bg_color(ctx->cancel_button, lv_color_hex(0xd94f55), 0);

  lv_obj_t *cancel_label = lv_label_create(ctx->cancel_button);
  lv_label_set_text(cancel_label, ctx->chinese ? "取消" : "Cancel");
  lv_obj_set_style_text_font(cancel_label, ctx->font, 0);
  lv_obj_center(cancel_label);

  /* 键盘 - 放在底部，缩小高度 */
  ctx->keyboard = lv_keyboard_create(ctx->password_panel);
  lv_obj_set_size(ctx->keyboard, LV_PCT(100), 155);
  lv_obj_align(ctx->keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(ctx->keyboard, ctx->password_ta);
  /* Wi-Fi passwords commonly contain letters and symbols. */
  lv_keyboard_set_mode(ctx->keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);

  /* 保存上下文到面板 */
  lv_obj_set_user_data(panel, ctx);

  return panel;
}

void wifi_ui_show(lv_obj_t *wifi_ui)
{
  if (wifi_ui != NULL)
    {
      lv_obj_clear_flag(wifi_ui, LV_OBJ_FLAG_HIDDEN);
    }
}

void wifi_ui_hide(lv_obj_t *wifi_ui)
{
  if (wifi_ui != NULL)
    {
      lv_obj_add_flag(wifi_ui, LV_OBJ_FLAG_HIDDEN);
    }
}

void wifi_ui_set_status(lv_obj_t *wifi_ui, const char *status)
{
  struct wifi_ui_context_s *ctx;

  if (wifi_ui == NULL || status == NULL)
    {
      return;
    }

  ctx = lv_obj_get_user_data(wifi_ui);
  if (ctx != NULL && ctx->status_label != NULL)
    {
      lv_label_set_text(ctx->status_label, status);
    }
}

static void wifi_ui_connection_dialog_delete_cb(lv_event_t *event)
{
  struct wifi_ui_context_s *ctx = lv_event_get_user_data(event);

  if (ctx != NULL && lv_event_get_target(event) == ctx->connection_dialog)
    {
      ctx->connection_dialog = NULL;
    }
}

static void wifi_ui_connection_dialog_close_cb(lv_event_t *event)
{
  struct wifi_ui_context_s *ctx = lv_event_get_user_data(event);
  lv_obj_t *dialog;

  if (ctx == NULL)
    {
      return;
    }

  dialog = ctx->connection_dialog;
  ctx->connection_dialog = NULL;
  if (dialog != NULL)
    {
      lv_msgbox_close(dialog);
    }
}

void wifi_ui_show_connection_info(lv_obj_t *wifi_ui, const char *ssid,
                                  const char *ipv4, const char *token)
{
  struct wifi_ui_context_s *ctx;
  lv_obj_t *title;
  lv_obj_t *text;
  lv_obj_t *button;
  lv_obj_t *label;

  if (wifi_ui == NULL || ssid == NULL || ipv4 == NULL || token == NULL)
    {
      return;
    }

  ctx = lv_obj_get_user_data(wifi_ui);
  if (ctx == NULL)
    {
      return;
    }

  if (ctx->connection_dialog != NULL)
    {
      lv_msgbox_close(ctx->connection_dialog);
      ctx->connection_dialog = NULL;
    }

  ctx->connection_dialog = lv_msgbox_create(NULL);
  if (ctx->connection_dialog == NULL)
    {
      return;
    }

  lv_obj_set_width(ctx->connection_dialog, 430);
  lv_obj_set_style_bg_color(ctx->connection_dialog,
                            lv_color_hex(0x23282e), 0);
  lv_obj_set_style_text_color(ctx->connection_dialog,
                              lv_color_hex(0xf5f7f8), 0);
  lv_obj_add_event_cb(ctx->connection_dialog,
                      wifi_ui_connection_dialog_delete_cb,
                      LV_EVENT_DELETE, ctx);

  title = lv_msgbox_add_title(
    ctx->connection_dialog,
    ctx->chinese ? "Wi-Fi 连接成功" : "Wi-Fi connected");
  lv_obj_set_style_text_font(title, ctx->font, 0);
  if (ctx->chinese)
    {
      snprintf(ctx->connection_info, sizeof(ctx->connection_info),
               "Wi-Fi: %s\nIP: %s\n网页: http://%s:8080/\n4位令牌: %s",
               ssid[0] == '\0' ? "--" : ssid, ipv4, ipv4, token);
    }
  else
    {
      snprintf(ctx->connection_info, sizeof(ctx->connection_info),
               "Wi-Fi: %s\nIP: %s\nWeb: http://%s:8080/\n4-digit token: %s",
               ssid[0] == '\0' ? "--" : ssid, ipv4, ipv4, token);
    }

  text = lv_msgbox_add_text(ctx->connection_dialog, ctx->connection_info);
  lv_obj_set_style_text_font(text, ctx->font, 0);
  button = lv_msgbox_add_footer_button(
    ctx->connection_dialog, ctx->chinese ? "知道了" : "OK");
  lv_obj_set_height(button, 44);
  label = lv_obj_get_child(button, 0);
  if (label != NULL)
    {
      lv_obj_set_style_text_font(label, ctx->font, 0);
    }
  lv_obj_add_event_cb(button, wifi_ui_connection_dialog_close_cb,
                      LV_EVENT_SHORT_CLICKED, ctx);
}

void wifi_ui_destroy(lv_obj_t *wifi_ui)
{
  struct wifi_ui_context_s *ctx;

  if (wifi_ui == NULL)
    {
      return;
    }

  ctx = lv_obj_get_user_data(wifi_ui);
  if (ctx != NULL)
    {
      if (ctx->connection_dialog != NULL)
        {
          lv_msgbox_close(ctx->connection_dialog);
          ctx->connection_dialog = NULL;
        }
      free(ctx);
    }

  lv_obj_del(wifi_ui);
}
