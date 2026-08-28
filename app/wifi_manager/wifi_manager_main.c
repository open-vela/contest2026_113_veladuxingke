/****************************************************************************
 * contest2026_113_veladuxingke/app/wifi_manager/wifi_manager_main.c
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/wireless/wireless.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/ethernet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <netutils/netlib.h>
#include <wireless/wapi.h>

#include "wifi_config.h"
#include "wifi_time_sync.h"

#define WIFI_MANAGER_CONFIG_PATH       "/data/wifi.cfg"
#define WIFI_MANAGER_STATUS_PATH       "/data/wifi.status"
#define WIFI_MANAGER_INTERFACE         "wlan0"
#define WIFI_MANAGER_CONFIG_POLL_SEC   5
#define WIFI_MANAGER_ASSOC_RETRIES     10
#define WIFI_MANAGER_BACKOFF_MAX_SEC   300
#define WIFI_MANAGER_TIME_RETRY_SEC    30
#define WIFI_MANAGER_TIME_RESYNC_SEC   (6 * 60 * 60)

#define WIFI_MANAGER_STAGE_IFUP        "ifup"
#define WIFI_MANAGER_STAGE_SOCKET      "socket"
#define WIFI_MANAGER_STAGE_MODE        "mode"
#define WIFI_MANAGER_STAGE_AUTH        "auth"
#define WIFI_MANAGER_STAGE_KEY         "key"
#define WIFI_MANAGER_STAGE_ESSID       "essid"
#define WIFI_MANAGER_STAGE_ASSOC       "association"
#define WIFI_MANAGER_STAGE_DHCP        "dhcp"

enum wifi_config_state_e
{
  WIFI_CONFIG_STATE_UNKNOWN = 0,
  WIFI_CONFIG_STATE_MISSING,
  WIFI_CONFIG_STATE_INVALID,
  WIFI_CONFIG_STATE_VALID
};

static time_t g_next_time_sync;
static int g_time_sync_error;
static bool g_time_synchronized;
static char g_status_ssid[WIFI_CONFIG_SSID_MAX + 1];

static void wifi_manager_json_escape(FAR const char *source,
                                     FAR char *output, size_t size)
{
  size_t used = 0;

  while (source != NULL && *source != '\0' && used + 1 < size)
    {
      if ((*source == '"' || *source == '\\') && used + 2 < size)
        {
          output[used++] = '\\';
        }

      if (used + 1 >= size)
        {
          break;
        }

      output[used++] = *source++;
    }

  output[used] = '\0';
}

static const char *wifi_manager_config_state_name(
  enum wifi_config_state_e state)
{
  switch (state)
    {
      case WIFI_CONFIG_STATE_MISSING:
        return "missing";
      case WIFI_CONFIG_STATE_INVALID:
        return "invalid";
      case WIFI_CONFIG_STATE_VALID:
        return "valid";
      default:
        return "unknown";
    }
}

static void wifi_manager_publish_status(enum wifi_config_state_e state,
                                        bool associated, bool ipv4_ready,
                                        FAR const char *stage, int error,
                                        unsigned int retry_wait)
{
  char temporary[sizeof(WIFI_MANAGER_STATUS_PATH) + 8];
  char address[INET_ADDRSTRLEN];
  char escaped_ssid[WIFI_CONFIG_SSID_MAX * 2 + 1];
  char data[512];
  struct in_addr ipv4;
  int fd;
  int length;

  memset(&ipv4, 0, sizeof(ipv4));
  memset(address, 0, sizeof(address));
  wifi_manager_json_escape(g_status_ssid, escaped_ssid,
                           sizeof(escaped_ssid));
  if (ipv4_ready && netlib_get_ipv4addr(WIFI_MANAGER_INTERFACE, &ipv4) == 0)
    {
      inet_ntop(AF_INET, &ipv4, address, sizeof(address));
    }

  length = snprintf(data, sizeof(data),
                    "{\"schema\":1,\"config\":\"%s\","
                    "\"ssid\":\"%s\","
                    "\"associated\":%u,\"ipv4_ready\":%u,"
                    "\"ipv4\":\"%s\",\"time_synchronized\":%u,"
                    "\"stage\":\"%s\",\"error\":%d,"
                    "\"retry_wait\":%u}\n",
                    wifi_manager_config_state_name(state), escaped_ssid,
                    associated ? 1 : 0,
                    ipv4_ready ? 1 : 0, address,
                    g_time_synchronized ? 1 : 0,
                    stage == NULL ? "" : stage, error, retry_wait);
  if (length < 0 || (size_t)length >= sizeof(data))
    {
      return;
    }

  snprintf(temporary, sizeof(temporary), "%s.tmp", WIFI_MANAGER_STATUS_PATH);
  fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0)
    {
      return;
    }

  if (write(fd, data, length) != length || fsync(fd) < 0)
    {
      close(fd);
      unlink(temporary);
      return;
    }

  close(fd);
  rename(temporary, WIFI_MANAGER_STATUS_PATH);
}

struct wifi_connect_result_s
{
  FAR const char *stage;
  int error;
};

static int wifi_manager_normalize_error(int ret)
{
  int error = errno;

  if (ret != ERROR)
    {
      return ret;
    }

  return error > 0 ? -error : -EIO;
}

static int wifi_manager_read_config(struct wifi_config_s *config)
{
  char data[WIFI_CONFIG_FILE_MAX + 1];
  ssize_t length;
  int fd;
  int ret;

  fd = open(WIFI_MANAGER_CONFIG_PATH, O_RDONLY);
  if (fd < 0)
    {
      return errno == ENOENT ? -ENOENT : -errno;
    }

  length = read(fd, data, sizeof(data));
  if (length < 0)
    {
      ret = -errno;
      close(fd);
      return ret;
    }

  ret = close(fd);
  if (ret < 0)
    {
      return -errno;
    }

  if (length == sizeof(data))
    {
      memset(data, 0, sizeof(data));
      return -E2BIG;
    }

  ret = wifi_config_parse(data, length, config);
  memset(data, 0, sizeof(data));
  return ret;
}

static bool wifi_manager_ap_connected(int socket_fd)
{
  struct ether_addr ap;
  uint8_t index;

  memset(&ap, 0, sizeof(ap));
  if (wapi_get_ap(socket_fd, WIFI_MANAGER_INTERFACE, &ap) < 0)
    {
      return false;
    }

  for (index = 0; index < sizeof(ap.ether_addr_octet); index++)
    {
      if (ap.ether_addr_octet[index] != 0)
        {
          return true;
        }
    }

  return false;
}

static bool wifi_manager_ipv4_ready(void)
{
  struct in_addr address;

  address.s_addr = INADDR_ANY;
  return netlib_get_ipv4addr(WIFI_MANAGER_INTERFACE, &address) == 0 &&
         address.s_addr != INADDR_ANY;
}

static void wifi_manager_sync_time(void)
{
  struct timespec monotonic;
  int ret;

  if (clock_gettime(CLOCK_MONOTONIC, &monotonic) < 0)
    {
      monotonic.tv_sec = 0;
      monotonic.tv_nsec = 0;
    }

  if (monotonic.tv_sec < g_next_time_sync)
    {
      return;
    }

  ret = wifi_time_sync();
  if (ret == OK)
    {
      g_time_synchronized = true;
      g_time_sync_error = OK;
      g_next_time_sync = monotonic.tv_sec + WIFI_MANAGER_TIME_RESYNC_SEC;
      syslog(LOG_INFO, "wifi_manager: system time synchronized via NTP\n");
    }
  else
    {
      g_next_time_sync = monotonic.tv_sec + WIFI_MANAGER_TIME_RETRY_SEC;
      if (ret != g_time_sync_error)
        {
          syslog(LOG_WARNING,
                 "wifi_manager: NTP synchronization failed (%d); "
                 "retry in %u seconds\n",
                 ret, WIFI_MANAGER_TIME_RETRY_SEC);
          g_time_sync_error = ret;
        }
    }
}

static int wifi_manager_dhcp(FAR struct wifi_connect_result_s *result)
{
  int ret;

  errno = 0;
  ret = netlib_obtain_ipv4addr(WIFI_MANAGER_INTERFACE);
  if (ret < 0)
    {
      result->stage = WIFI_MANAGER_STAGE_DHCP;
      result->error = wifi_manager_normalize_error(ret);
      return result->error;
    }

  return OK;
}

static int wifi_manager_connect(FAR const struct wifi_config_s *config,
                                FAR struct wifi_connect_result_s *result)
{
  int socket_fd;
  int ret;
  int retry;

  result->stage = NULL;
  result->error = OK;

  errno = 0;
  ret = netlib_ifup(WIFI_MANAGER_INTERFACE);
  if (ret < 0)
    {
      result->stage = WIFI_MANAGER_STAGE_IFUP;
      result->error = wifi_manager_normalize_error(ret);
      return result->error;
    }

  errno = 0;
  socket_fd = wapi_make_socket();
  if (socket_fd < 0)
    {
      result->stage = WIFI_MANAGER_STAGE_SOCKET;
      result->error = wifi_manager_normalize_error(socket_fd);
      return result->error;
    }

  ret = wapi_set_mode(socket_fd, WIFI_MANAGER_INTERFACE,
                      WAPI_MODE_MANAGED);
  if (ret < 0)
    {
      result->stage = WIFI_MANAGER_STAGE_MODE;
      goto out;
    }

  ret = wpa_driver_wext_set_auth_param(socket_fd,
                                       WIFI_MANAGER_INTERFACE,
                                       IW_AUTH_WPA_VERSION,
                                       IW_AUTH_WPA_VERSION_WPA2);
  if (ret < 0)
    {
      result->stage = WIFI_MANAGER_STAGE_AUTH;
      goto out;
    }

  ret = wpa_driver_wext_set_auth_param(socket_fd,
                                       WIFI_MANAGER_INTERFACE,
                                       IW_AUTH_CIPHER_PAIRWISE,
                                       IW_AUTH_CIPHER_CCMP);
  if (ret < 0)
    {
      result->stage = WIFI_MANAGER_STAGE_AUTH;
      goto out;
    }

  ret = wpa_driver_wext_set_key_ext(socket_fd, WIFI_MANAGER_INTERFACE,
                                    WPA_ALG_CCMP, config->password,
                                    strlen(config->password));
  if (ret < 0)
    {
      result->stage = WIFI_MANAGER_STAGE_KEY;
      goto out;
    }

  ret = wapi_set_essid(socket_fd, WIFI_MANAGER_INTERFACE, config->ssid,
                       WAPI_ESSID_ON);
  if (ret < 0)
    {
      result->stage = WIFI_MANAGER_STAGE_ESSID;
      goto out;
    }

  ret = -ETIMEDOUT;
  result->stage = WIFI_MANAGER_STAGE_ASSOC;
  for (retry = 0; retry < WIFI_MANAGER_ASSOC_RETRIES; retry++)
    {
      if (wifi_manager_ap_connected(socket_fd))
        {
          ret = OK;
          break;
        }

      sleep(1);
    }

out:
  close(socket_fd);
  if (ret < 0)
    {
      result->error = wifi_manager_normalize_error(ret);
      return result->error;
    }

  return wifi_manager_dhcp(result);
}

int main(int argc, FAR char *argv[])
{
  struct wifi_config_s active_config;
  struct wifi_config_s pending_config;
  struct wifi_connect_result_s result;
  enum wifi_config_state_e state = WIFI_CONFIG_STATE_UNKNOWN;
  FAR const char *last_failure_stage = NULL;
  unsigned int backoff = WIFI_MANAGER_CONFIG_POLL_SEC;
  unsigned int retry_wait = 0;
  int last_failure = OK;
  int socket_fd;
  int ret;
  bool active_valid = false;
  bool associated;

  memset(&active_config, 0, sizeof(active_config));
  memset(&pending_config, 0, sizeof(pending_config));
  syslog(LOG_INFO, "wifi_manager: waiting for Wi-Fi configuration\n");
  wifi_manager_publish_status(WIFI_CONFIG_STATE_UNKNOWN, false, false,
                              NULL, 0, 0);

  for (;;)
    {
      ret = wifi_manager_read_config(&pending_config);
      if (ret == -ENOENT)
        {
          if (state != WIFI_CONFIG_STATE_MISSING)
            {
              syslog(LOG_INFO,
                     "wifi_manager: configuration not present; idle\n");
            }

          state = WIFI_CONFIG_STATE_MISSING;
          wifi_config_clear(&pending_config);
          wifi_config_clear(&active_config);
          memset(g_status_ssid, 0, sizeof(g_status_ssid));
          active_valid = false;
          retry_wait = 0;
          last_failure = OK;
          last_failure_stage = NULL;
          wifi_manager_publish_status(state, false, false, NULL, 0, 0);
          sleep(WIFI_MANAGER_CONFIG_POLL_SEC);
          continue;
        }
      else if (ret < 0)
        {
          if (state != WIFI_CONFIG_STATE_INVALID)
            {
              syslog(LOG_WARNING,
                     "wifi_manager: configuration is invalid; idle\n");
            }

          state = WIFI_CONFIG_STATE_INVALID;
          wifi_config_clear(&pending_config);
          wifi_config_clear(&active_config);
          memset(g_status_ssid, 0, sizeof(g_status_ssid));
          active_valid = false;
          retry_wait = 0;
          wifi_manager_publish_status(state, false, false, NULL, ret, 0);
          sleep(WIFI_MANAGER_CONFIG_POLL_SEC);
          continue;
        }

      state = WIFI_CONFIG_STATE_VALID;
      if (!active_valid ||
          !wifi_config_equal(&pending_config, &active_config))
        {
          wifi_config_clear(&active_config);
          active_config = pending_config;
          memset(&pending_config, 0, sizeof(pending_config));
          strlcpy(g_status_ssid, active_config.ssid,
                  sizeof(g_status_ssid));
          active_valid = true;
          backoff = WIFI_MANAGER_CONFIG_POLL_SEC;
          retry_wait = 0;
          last_failure = OK;
          last_failure_stage = NULL;
          syslog(LOG_INFO, "wifi_manager: applying new configuration\n");
        }
      else
        {
          wifi_config_clear(&pending_config);
        }

      if (retry_wait > 0)
        {
          retry_wait = retry_wait > WIFI_MANAGER_CONFIG_POLL_SEC ?
                       retry_wait - WIFI_MANAGER_CONFIG_POLL_SEC : 0;
          sleep(WIFI_MANAGER_CONFIG_POLL_SEC);
          continue;
        }

      socket_fd = wapi_make_socket();
      associated = socket_fd >= 0 &&
                   wifi_manager_ap_connected(socket_fd);
      if (socket_fd >= 0)
        {
          close(socket_fd);
        }

      if (associated && wifi_manager_ipv4_ready())
        {
          wifi_manager_sync_time();
          backoff = WIFI_MANAGER_CONFIG_POLL_SEC;
          last_failure = OK;
          last_failure_stage = NULL;
          wifi_manager_publish_status(state, true, true, NULL, 0, 0);
          sleep(WIFI_MANAGER_CONFIG_POLL_SEC);
          continue;
        }

      if (associated)
        {
          ret = wifi_manager_dhcp(&result);
        }
      else
        {
          ret = wifi_manager_connect(&active_config, &result);
        }

      if (ret == 0)
        {
          backoff = WIFI_MANAGER_CONFIG_POLL_SEC;
          last_failure = OK;
          last_failure_stage = NULL;
          syslog(LOG_INFO, "wifi_manager: connected\n");
          if (wifi_manager_ipv4_ready())
            {
              wifi_manager_sync_time();
            }

          wifi_manager_publish_status(state, true, wifi_manager_ipv4_ready(),
                                      NULL, 0, 0);
          sleep(WIFI_MANAGER_CONFIG_POLL_SEC);
          continue;
        }

      if (ret != last_failure || result.stage != last_failure_stage)
        {
          syslog(LOG_WARNING,
                 "wifi_manager: %s failed (%d); retry in %u seconds\n",
                 result.stage == NULL ? "connection" : result.stage,
                 ret, backoff);
          last_failure = ret;
          last_failure_stage = result.stage;
        }

      retry_wait = backoff;
      wifi_manager_publish_status(state, associated, false, result.stage,
                                  ret, retry_wait);
      if (backoff < WIFI_MANAGER_BACKOFF_MAX_SEC)
        {
          backoff *= 2;
          if (backoff > WIFI_MANAGER_BACKOFF_MAX_SEC)
            {
              backoff = WIFI_MANAGER_BACKOFF_MAX_SEC;
            }
        }
    }

  return 0;
}
