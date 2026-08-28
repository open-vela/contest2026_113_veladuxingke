/****************************************************************************
 * contest2026_113_veladuxingke/app/lan_panel/lan_auth.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef CONFIG_CONTEST2026_113_LAN_PANEL_AUTH_PATH
#  define CONFIG_CONTEST2026_113_LAN_PANEL_AUTH_PATH "/data/panel.auth"
#endif

#define LAN_AUTH_BYTES 2
#define LAN_AUTH_TEXT 4

static int lan_auth_read_random(unsigned char *data, size_t size)
{
  int fd = open("/dev/urandom", O_RDONLY);
  size_t offset = 0;

  if (fd < 0)
    {
      return -errno;
    }

  while (offset < size)
    {
      ssize_t length = read(fd, data + offset, size - offset);
      if (length <= 0)
        {
          int ret = length == 0 ? -EIO : -errno;
          close(fd);
          return ret;
        }
      offset += length;
    }

  close(fd);
  return 0;
}

static int lan_auth_load_file(char *token, size_t size)
{
  char stored[LAN_AUTH_TEXT + 1];
  int fd;
  ssize_t length;
  size_t i;

  if (token == NULL || size <= LAN_AUTH_TEXT)
    {
      return -EINVAL;
    }

  fd = open(CONFIG_CONTEST2026_113_LAN_PANEL_AUTH_PATH, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  /* Read one byte beyond the accepted token length so legacy 64-character
   * token files cannot be mistaken for a valid four-digit token merely
   * because their first four characters happen to be decimal digits.
   */

  length = read(fd, stored, sizeof(stored));
  close(fd);
  if (length != LAN_AUTH_TEXT)
    {
      memset(stored, 0, sizeof(stored));
      return -EINVAL;
    }

  for (i = 0; i < LAN_AUTH_TEXT; i++)
    {
      if (stored[i] < '0' || stored[i] > '9')
        {
          memset(stored, 0, sizeof(stored));
          return -EINVAL;
        }
    }

  memcpy(token, stored, LAN_AUTH_TEXT);
  token[LAN_AUTH_TEXT] = '\0';
  memset(stored, 0, sizeof(stored));
  return 0;
}

int lan_auth_load(char *token, size_t size)
{
  unsigned char random_bytes[LAN_AUTH_BYTES];
  char temporary[sizeof(CONFIG_CONTEST2026_113_LAN_PANEL_AUTH_PATH) + 8];
  char generated[LAN_AUTH_TEXT + 1];
  int fd;
  int ret;
  unsigned int value;

  if (token == NULL || size <= LAN_AUTH_TEXT)
    {
      return -EINVAL;
    }

  ret = lan_auth_load_file(token, size);
  if (ret == 0)
    {
      return 0;
    }

  ret = lan_auth_read_random(random_bytes, sizeof(random_bytes));
  if (ret < 0)
    {
      return ret;
    }

  value = ((unsigned int)random_bytes[0] << 8) | random_bytes[1];
  snprintf(generated, sizeof(generated), "%04u", value % 10000);

  snprintf(temporary, sizeof(temporary), "%s.tmp",
           CONFIG_CONTEST2026_113_LAN_PANEL_AUTH_PATH);
  fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0 && errno == EEXIST)
    {
      unlink(temporary);
      fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
    }
  if (fd < 0)
    {
      return -errno;
    }

  ret = write(fd, generated, LAN_AUTH_TEXT) == LAN_AUTH_TEXT ? 0 : -EIO;
  if (ret == 0 && fsync(fd) < 0)
    {
      ret = -errno;
    }
  close(fd);
  if (ret < 0 || rename(temporary, CONFIG_CONTEST2026_113_LAN_PANEL_AUTH_PATH) < 0)
    {
      if (ret == 0)
        {
          ret = -errno;
        }
      unlink(temporary);
      return ret;
    }

  memcpy(token, generated, sizeof(generated));
  memset(random_bytes, 0, sizeof(random_bytes));
  memset(generated, 0, sizeof(generated));
  return 0;
}

int lan_auth_check(const char *token, size_t length)
{
  char expected[LAN_AUTH_TEXT + 1];
  size_t i;
  unsigned char difference = 0;

  if (token == NULL || length != LAN_AUTH_TEXT ||
      lan_auth_load(expected, sizeof(expected)) < 0)
    {
      return -EACCES;
    }

  for (i = 0; i < LAN_AUTH_TEXT; i++)
    {
      difference |= (unsigned char)(token[i] ^ expected[i]);
    }

  memset(expected, 0, sizeof(expected));
  return difference == 0 ? 0 : -EACCES;
}
