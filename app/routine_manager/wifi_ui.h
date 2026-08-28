/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/wifi_ui.h
 ****************************************************************************/

#ifndef __WIFI_UI_H
#define __WIFI_UI_H

#include <nuttx/config.h>
#include <lvgl/lvgl.h>
#include <stdbool.h>

/* Wi-Fi UI 回调函数 */
typedef void (*wifi_ui_connect_cb_t)(const char *ssid, const char *password,
                                     void *user_data);

struct wifi_ui_options_s
{
  wifi_ui_connect_cb_t connect_callback;
  void *user_data;
  bool chinese;
  const lv_font_t *font;  /* 添加字体参数 */
};

/* 创建 Wi-Fi 配置界面 */
lv_obj_t *wifi_ui_create(lv_obj_t *parent,
                         const struct wifi_ui_options_s *options);

/* 显示/隐藏 Wi-Fi 配置界面 */
void wifi_ui_show(lv_obj_t *wifi_ui);
void wifi_ui_hide(lv_obj_t *wifi_ui);

/* 更新连接状态 */
void wifi_ui_set_status(lv_obj_t *wifi_ui, const char *status);

/* Wi-Fi 取得 IPv4 后展示独立运行所需的网页地址和4位令牌。 */
void wifi_ui_show_connection_info(lv_obj_t *wifi_ui, const char *ssid,
                                  const char *ipv4, const char *token);

/* 销毁 Wi-Fi UI */
void wifi_ui_destroy(lv_obj_t *wifi_ui);

#endif /* __WIFI_UI_H */
