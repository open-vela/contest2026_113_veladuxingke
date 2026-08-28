/****************************************************************************
 * contest2026_113_veladuxingke/app/wifi_manager/wifi_time_sync.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define WIFI_NTP_PACKET_SIZE       48
#define WIFI_NTP_UNIX_DELTA        UINT32_C(2208988800)
#define WIFI_NTP_MIN_UNIX_TIME     UINT32_C(1577836800)
#define WIFI_NTP_TIMEOUT_SEC       1
#define WIFI_NTP_TIMEOUT_USEC      500000

static const char *g_wifi_ntp_servers[] =
{
  "203.107.6.88",
  "ntp.aliyun.com",
  "ntp.tencent.com",
  "cn.pool.ntp.org"
};

static uint32_t wifi_time_read_be32(FAR const uint8_t *buffer)
{
  return ((uint32_t)buffer[0] << 24) |
         ((uint32_t)buffer[1] << 16) |
         ((uint32_t)buffer[2] << 8) |
         (uint32_t)buffer[3];
}

static void wifi_time_write_be32(FAR uint8_t *buffer, uint32_t value)
{
  buffer[0] = value >> 24;
  buffer[1] = value >> 16;
  buffer[2] = value >> 8;
  buffer[3] = value;
}

static int wifi_time_sync_address(FAR const struct addrinfo *address)
{
  struct timeval timeout;
  struct timespec now;
  struct timespec synchronized;
  uint8_t request[WIFI_NTP_PACKET_SIZE];
  uint8_t reply[WIFI_NTP_PACKET_SIZE];
  uint64_t fraction;
  uint64_t ntp_seconds;
  uint32_t transmit_fraction;
  uint32_t transmit_seconds;
  uint32_t unix_seconds;
  ssize_t length;
  int fd;
  int ret;

  fd = socket(address->ai_family, address->ai_socktype,
              address->ai_protocol);
  if (fd < 0)
    {
      return -errno;
    }

  timeout.tv_sec = WIFI_NTP_TIMEOUT_SEC;
  timeout.tv_usec = WIFI_NTP_TIMEOUT_USEC;
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                 sizeof(timeout)) < 0 ||
      setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                 sizeof(timeout)) < 0)
    {
      ret = -errno;
      close(fd);
      return ret;
    }

  if (connect(fd, address->ai_addr, address->ai_addrlen) < 0)
    {
      ret = -errno;
      close(fd);
      return ret;
    }

  memset(request, 0, sizeof(request));
  request[0] = (4 << 3) | 3; /* NTP v4, client mode. */
  if (clock_gettime(CLOCK_REALTIME, &now) < 0)
    {
      ret = -errno;
      close(fd);
      return ret;
    }

  ntp_seconds = (uint64_t)now.tv_sec + WIFI_NTP_UNIX_DELTA;
  if (ntp_seconds > UINT32_MAX)
    {
      close(fd);
      return -ERANGE;
    }

  fraction = ((uint64_t)now.tv_nsec << 32) / 1000000000;
  wifi_time_write_be32(&request[40], (uint32_t)ntp_seconds);
  wifi_time_write_be32(&request[44], (uint32_t)fraction);

  length = send(fd, request, sizeof(request), 0);
  if (length != sizeof(request))
    {
      ret = length < 0 ? -errno : -EIO;
      close(fd);
      return ret;
    }

  length = recv(fd, reply, sizeof(reply), 0);
  if (length < WIFI_NTP_PACKET_SIZE)
    {
      ret = length < 0 ? -errno : -EPROTO;
      close(fd);
      return ret;
    }

  close(fd);
  if ((reply[0] >> 6) == 3 ||
      ((reply[0] & 7) != 4 && (reply[0] & 7) != 5) ||
      reply[1] == 0 || reply[1] > 15 ||
      memcmp(&reply[24], &request[40], 8) != 0)
    {
      return -EPROTO;
    }

  transmit_seconds = wifi_time_read_be32(&reply[40]);
  transmit_fraction = wifi_time_read_be32(&reply[44]);
  if (transmit_seconds < WIFI_NTP_UNIX_DELTA)
    {
      return -ERANGE;
    }

  unix_seconds = transmit_seconds - WIFI_NTP_UNIX_DELTA;
  if (unix_seconds < WIFI_NTP_MIN_UNIX_TIME)
    {
      return -ERANGE;
    }

  synchronized.tv_sec = unix_seconds;
  synchronized.tv_nsec =
    ((uint64_t)transmit_fraction * 1000000000) >> 32;
  if (clock_settime(CLOCK_REALTIME, &synchronized) < 0)
    {
      return -errno;
    }

  return OK;
}

int wifi_time_sync(void)
{
  struct addrinfo hints;
  FAR struct addrinfo *addresses;
  FAR struct addrinfo *address;
  size_t server;
  int last_error = -ENETUNREACH;
  int ret;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  hints.ai_flags = AI_NUMERICSERV;

  for (server = 0;
       server < sizeof(g_wifi_ntp_servers) / sizeof(g_wifi_ntp_servers[0]);
       server++)
    {
      ret = getaddrinfo(g_wifi_ntp_servers[server], "123", &hints,
                        &addresses);
      if (ret != 0)
        {
          last_error = -EHOSTUNREACH;
          continue;
        }

      for (address = addresses; address != NULL; address = address->ai_next)
        {
          last_error = wifi_time_sync_address(address);
          if (last_error == OK)
            {
              break;
            }
        }

      freeaddrinfo(addresses);
      if (last_error == OK)
        {
          return OK;
        }
    }

  return last_error;
}
