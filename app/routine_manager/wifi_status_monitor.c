/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/wifi_status_monitor.c
 *
 * WiFi 状态监控模块 - 定期读取 /data/wifi.status 并更新 UI
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <lvgl/lvgl.h>

#include "wifi_ui.h"

#define WIFI_STATUS_PATH "/data/wifi.status"
#define WIFI_TOKEN_PATH "/data/panel.auth"
#define WIFI_STATUS_CHECK_MS 3000  /* 每3秒检查一次 */

struct wifi_status_s
{
  int schema;
  char config[16];
  char ssid[33];
  int associated;
  int ipv4_ready;
  char ipv4[64];
  char stage[32];
  int error;
  int retry_wait;
};

struct wifi_monitor_context_s
{
  lv_timer_t *timer;
  lv_obj_t *wifi_panel;
  bool chinese;
  bool connected_notified;
};

static struct wifi_monitor_context_s g_monitor = {0};

/* 简易 JSON 解析 - 提取字符串值 */
static bool wifi_parse_string(const char *json, const char *key, char *out, size_t size)
{
  char search[64];
  const char *start;
  const char *end;
  size_t len;

  snprintf(search, sizeof(search), "\"%s\":\"", key);
  start = strstr(json, search);
  if (start == NULL)
    {
      return false;
    }

  start += strlen(search);
  end = strchr(start, '"');
  if (end == NULL)
    {
      return false;
    }

  len = end - start;
  if (len >= size)
    {
      len = size - 1;
    }

  memcpy(out, start, len);
  out[len] = '\0';
  return true;
}

/* 简易 JSON 解析 - 提取整数值 */
static bool wifi_parse_int(const char *json, const char *key, int *out)
{
  char search[64];
  const char *start;

  snprintf(search, sizeof(search), "\"%s\":", key);
  start = strstr(json, search);
  if (start == NULL)
    {
      return false;
    }

  start += strlen(search);
  *out = atoi(start);
  return true;
}

/* 读取并解析 WiFi 状态文件 */
static bool wifi_read_status(struct wifi_status_s *status)
{
  char buffer[512];
  int fd;
  ssize_t len;

  memset(status, 0, sizeof(*status));

  fd = open(WIFI_STATUS_PATH, O_RDONLY);
  if (fd < 0)
    {
      return false;
    }

  len = read(fd, buffer, sizeof(buffer) - 1);
  close(fd);

  if (len <= 0)
    {
      return false;
    }

  buffer[len] = '\0';

  wifi_parse_int(buffer, "schema", &status->schema);
  wifi_parse_string(buffer, "config", status->config, sizeof(status->config));
  wifi_parse_string(buffer, "ssid", status->ssid, sizeof(status->ssid));
  wifi_parse_int(buffer, "associated", &status->associated);
  wifi_parse_int(buffer, "ipv4_ready", &status->ipv4_ready);
  wifi_parse_string(buffer, "ipv4", status->ipv4, sizeof(status->ipv4));
  wifi_parse_string(buffer, "stage", status->stage, sizeof(status->stage));
  wifi_parse_int(buffer, "error", &status->error);
  wifi_parse_int(buffer, "retry_wait", &status->retry_wait);

  return true;
}

static bool wifi_read_panel_token(char token[5])
{
  int fd;
  ssize_t length;
  int i;

  fd = open(WIFI_TOKEN_PATH, O_RDONLY);
  if (fd < 0)
    {
      return false;
    }

  length = read(fd, token, 5);
  close(fd);
  if (length != 4)
    {
      return false;
    }

  for (i = 0; i < 4; i++)
    {
      if (token[i] < '0' || token[i] > '9')
        {
          return false;
        }
    }

  token[4] = '\0';
  return true;
}

/* 定时器回调 - 检查 WiFi 状态并更新 UI */
static void wifi_status_timer_cb(lv_timer_t *timer)
{
  struct wifi_status_s status;
  char msg[128];
  char token[5];

  (void)timer;

  if (g_monitor.wifi_panel == NULL)
    {
      return;
    }

  /* 读取状态文件 */
  if (!wifi_read_status(&status))
    {
      return;
    }

  /* 根据状态更新 UI */
  if (status.ipv4_ready && status.associated)
    {
      /* 连接成功 */
      if (!g_monitor.connected_notified)
        {
          if (!wifi_read_panel_token(token))
            {
              wifi_ui_set_status(
                g_monitor.wifi_panel,
                g_monitor.chinese ? "已连接，正在准备网页令牌..." :
                                    "Connected, preparing web token...");
              return;
            }

          if (g_monitor.chinese)
            {
              snprintf(msg, sizeof(msg), "✓ 已连接 IP: %s", status.ipv4);
            }
          else
            {
              snprintf(msg, sizeof(msg), "✓ Connected IP: %s", status.ipv4);
            }
          wifi_ui_set_status(g_monitor.wifi_panel, msg);
          wifi_ui_show_connection_info(g_monitor.wifi_panel, status.ssid,
                                       status.ipv4, token);
          g_monitor.connected_notified = true;

          /* 连接成功后减慢检查频率 */
          lv_timer_set_period(g_monitor.timer, 10000);
        }
    }
  else if (strcmp(status.config, "valid") == 0)
    {
      /* 正在连接 */
      g_monitor.connected_notified = false;

      if (status.retry_wait > 0)
        {
          if (g_monitor.chinese)
            {
              snprintf(msg, sizeof(msg), "连接失败，%d秒后重试...", status.retry_wait);
            }
          else
            {
              snprintf(msg, sizeof(msg), "Failed, retry in %ds...", status.retry_wait);
            }
        }
      else if (status.associated)
        {
          if (g_monitor.chinese)
            {
              snprintf(msg, sizeof(msg), "已关联AP，正在获取IP...");
            }
          else
            {
              snprintf(msg, sizeof(msg), "Associated, getting IP...");
            }
        }
      else
        {
          if (g_monitor.chinese)
            {
              snprintf(msg, sizeof(msg), "正在连接...");
            }
          else
            {
              snprintf(msg, sizeof(msg), "Connecting...");
            }
        }
      wifi_ui_set_status(g_monitor.wifi_panel, msg);
    }
  else if (strcmp(status.config, "missing") == 0)
    {
      /* 未配置 */
      g_monitor.connected_notified = false;
      if (g_monitor.chinese)
        {
          wifi_ui_set_status(g_monitor.wifi_panel, "未配置 WiFi");
        }
      else
        {
          wifi_ui_set_status(g_monitor.wifi_panel, "WiFi not configured");
        }
    }
  else if (strcmp(status.config, "invalid") == 0)
    {
      /* 配置无效 */
      g_monitor.connected_notified = false;
      if (g_monitor.chinese)
        {
          wifi_ui_set_status(g_monitor.wifi_panel, "配置无效");
        }
      else
        {
          wifi_ui_set_status(g_monitor.wifi_panel, "Invalid config");
        }
    }
}

/* 启动 WiFi 状态监控 */
void wifi_status_monitor_start(lv_obj_t *wifi_panel, bool chinese)
{
  if (g_monitor.timer != NULL)
    {
      return;  /* 已启动 */
    }

  g_monitor.wifi_panel = wifi_panel;
  g_monitor.chinese = chinese;
  g_monitor.connected_notified = false;

  g_monitor.timer = lv_timer_create(wifi_status_timer_cb,
                                     WIFI_STATUS_CHECK_MS, NULL);
}

/* 停止 WiFi 状态监控 */
void wifi_status_monitor_stop(void)
{
  if (g_monitor.timer != NULL)
    {
      lv_timer_del(g_monitor.timer);
      g_monitor.timer = NULL;
    }

  memset(&g_monitor, 0, sizeof(g_monitor));
}

/* 触发立即检查（配置保存后调用） */
void wifi_status_monitor_check_now(void)
{
  if (g_monitor.timer != NULL)
    {
      g_monitor.connected_notified = false;
      lv_timer_set_period(g_monitor.timer, WIFI_STATUS_CHECK_MS);
      lv_timer_reset(g_monitor.timer);
      lv_timer_ready(g_monitor.timer);
    }
}
