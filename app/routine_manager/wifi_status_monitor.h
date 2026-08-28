/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/wifi_status_monitor.h
 ****************************************************************************/

#ifndef __APP_ROUTINE_MANAGER_WIFI_STATUS_MONITOR_H
#define __APP_ROUTINE_MANAGER_WIFI_STATUS_MONITOR_H

#include <lvgl/lvgl.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* 启动 WiFi 状态监控
 * wifi_panel: wifi_ui 创建的面板对象
 * chinese: 是否使用中文
 */
void wifi_status_monitor_start(lv_obj_t *wifi_panel, bool chinese);

/* 停止 WiFi 状态监控 */
void wifi_status_monitor_stop(void);

/* 触发立即检查（配置保存后调用） */
void wifi_status_monitor_check_now(void);

#endif /* __APP_ROUTINE_MANAGER_WIFI_STATUS_MONITOR_H */
