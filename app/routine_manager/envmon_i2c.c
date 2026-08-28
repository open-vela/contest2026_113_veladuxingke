/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/envmon_i2c.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "envmon_i2c.h"

#define ENVMON_I2C_ATTEMPTS 3

static bool envmon_i2c_retryable(int ret)
{
  return ret == -ENXIO || ret == -EIO || ret == -EAGAIN ||
         ret == -ETIMEDOUT || ret == -EBUSY;
}

static int envmon_i2c_ioctl_transfer(FAR void *arg,
                                     FAR struct i2c_msg_s *messages,
                                     size_t count)
{
  struct i2c_transfer_s transfer;
  FAR struct envmon_i2c_s *bus = arg;
  int ret;

  transfer.msgv = messages;
  transfer.msgc = count;
  ret = ioctl(bus->fd, I2CIOC_TRANSFER,
              (unsigned long)(uintptr_t)&transfer);
  if (ret < 0)
    {
      return -errno;
    }

  if (ret != 0 && ret != count)
    {
      return -EIO;
    }

  return OK;
}

static int envmon_i2c_transfer(FAR struct envmon_i2c_s *bus,
                               FAR struct i2c_msg_s *messages,
                               size_t count)
{
  static const useconds_t retry_delay[] = {0, 2000, 5000};
  int ret = -EIO;
  int attempt;

  if (bus == NULL || bus->transfer == NULL || messages == NULL || count == 0)
    {
      return -EINVAL;
    }

  for (attempt = 0; attempt < ENVMON_I2C_ATTEMPTS; attempt++)
    {
      if (retry_delay[attempt] != 0)
        {
          usleep(retry_delay[attempt]);
        }

      ret = bus->transfer(bus->transfer_arg, messages, count);
      if (ret >= 0 || !envmon_i2c_retryable(ret))
        {
          return ret;
        }
    }

  return ret;
}

int envmon_i2c_open(FAR struct envmon_i2c_s *bus, FAR const char *path)
{
  if (bus == NULL || path == NULL)
    {
      return -EINVAL;
    }

  memset(bus, 0, sizeof(*bus));
  bus->fd = open(path, O_RDONLY);
  if (bus->fd < 0)
    {
      int ret = -errno;
      bus->fd = -1;
      return ret;
    }

  bus->frequency = ENVMON_I2C_FREQUENCY;
  bus->transfer = envmon_i2c_ioctl_transfer;
  bus->transfer_arg = bus;
  printf("envmon: opened %s at %lu Hz\n", path,
         (unsigned long)bus->frequency);
  return OK;
}

void envmon_i2c_close(FAR struct envmon_i2c_s *bus)
{
  if (bus != NULL && bus->fd >= 0)
    {
      close(bus->fd);
      bus->fd = -1;
    }
}

int envmon_i2c_probe(FAR struct envmon_i2c_s *bus, uint8_t address)
{
  struct i2c_msg_s message;

  if (bus == NULL || address > 0x7f)
    {
      return -EINVAL;
    }

  memset(&message, 0, sizeof(message));
  message.frequency = bus->frequency;
  message.addr = address;
  return envmon_i2c_transfer(bus, &message, 1);
}

int envmon_i2c_write(FAR struct envmon_i2c_s *bus, uint8_t address,
                     FAR const uint8_t *data, size_t length)
{
  struct i2c_msg_s message;

  if (bus == NULL || address > 0x7f || data == NULL || length == 0 ||
      length > SSIZE_MAX)
    {
      return -EINVAL;
    }

  message.frequency = bus->frequency;
  message.addr = address;
  message.flags = 0;
  message.buffer = (FAR uint8_t *)data;
  message.length = length;
  return envmon_i2c_transfer(bus, &message, 1);
}

int envmon_i2c_read(FAR struct envmon_i2c_s *bus, uint8_t address,
                    FAR uint8_t *data, size_t length)
{
  struct i2c_msg_s message;

  if (bus == NULL || address > 0x7f || data == NULL || length == 0 ||
      length > SSIZE_MAX)
    {
      return -EINVAL;
    }

  message.frequency = bus->frequency;
  message.addr = address;
  message.flags = I2C_M_READ;
  message.buffer = data;
  message.length = length;
  return envmon_i2c_transfer(bus, &message, 1);
}

int envmon_i2c_write_read(FAR struct envmon_i2c_s *bus, uint8_t address,
                          FAR const uint8_t *write_data,
                          size_t write_length, FAR uint8_t *read_data,
                          size_t read_length)
{
  struct i2c_msg_s messages[2];

  if (bus == NULL || address > 0x7f || write_data == NULL ||
      write_length == 0 || read_data == NULL || read_length == 0 ||
      write_length > SSIZE_MAX || read_length > SSIZE_MAX)
    {
      return -EINVAL;
    }

  messages[0].frequency = bus->frequency;
  messages[0].addr = address;
  messages[0].flags = I2C_M_NOSTOP;
  messages[0].buffer = (FAR uint8_t *)write_data;
  messages[0].length = write_length;
  messages[1].frequency = bus->frequency;
  messages[1].addr = address;
  messages[1].flags = I2C_M_READ;
  messages[1].buffer = read_data;
  messages[1].length = read_length;
  return envmon_i2c_transfer(bus, messages, 2);
}

int envmon_i2c_write_delay_read(FAR struct envmon_i2c_s *bus,
                                uint8_t address,
                                FAR const uint8_t *write_data,
                                size_t write_length, unsigned int delay_ms,
                                FAR uint8_t *read_data, size_t read_length)
{
  int ret;

  if (delay_ms > UINT_MAX / 1000)
    {
      return -EINVAL;
    }

  ret = envmon_i2c_write(bus, address, write_data, write_length);
  if (ret < 0)
    {
      return ret;
    }

  usleep((useconds_t)delay_ms * 1000);
  return envmon_i2c_read(bus, address, read_data, read_length);
}

void envmon_i2c_set_test_backend(FAR struct envmon_i2c_s *bus,
                                 envmon_i2c_transfer_t transfer,
                                 FAR void *arg)
{
  memset(bus, 0, sizeof(*bus));
  bus->fd = -1;
  bus->frequency = ENVMON_I2C_FREQUENCY;
  bus->transfer = transfer;
  bus->transfer_arg = arg;
}

uint8_t envmon_sensirion_crc8(FAR const uint8_t *data, size_t length)
{
  uint8_t crc = 0xff;
  size_t i;
  int bit;

  for (i = 0; i < length; i++)
    {
      crc ^= data[i];
      for (bit = 0; bit < 8; bit++)
        {
          crc = (crc & 0x80) != 0 ? (crc << 1) ^ 0x31 : crc << 1;
        }
    }

  return crc;
}

int envmon_i2c_selftest(void)
{
  static const uint8_t vector[] = {0xbe, 0xef};

  return envmon_sensirion_crc8(vector, sizeof(vector)) == 0x92 ? OK :
                                                                      -EINVAL;
}
