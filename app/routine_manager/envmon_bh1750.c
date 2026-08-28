/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/envmon_bh1750.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <unistd.h>

#include "envmon_bh1750.h"

#define ENVMON_BH1750_PRIMARY_ADDRESS   0x23
#define ENVMON_BH1750_SECONDARY_ADDRESS 0x5c
#define ENVMON_BH1750_POWER_ON          0x01
#define ENVMON_BH1750_CONTINUOUS_HR     0x10

void envmon_bh1750_reset(FAR struct envmon_bh1750_s *sensor,
                         FAR struct envmon_i2c_s *bus)
{
  sensor->bus = bus;
  sensor->address = 0;
  sensor->initialized = false;
}

static int envmon_bh1750_try_address(FAR struct envmon_bh1750_s *sensor,
                                     uint8_t address)
{
  uint8_t command = ENVMON_BH1750_POWER_ON;
  int ret;

  ret = envmon_i2c_write(sensor->bus, address, &command, sizeof(command));
  if (ret < 0)
    {
      return ret;
    }

  command = ENVMON_BH1750_CONTINUOUS_HR;
  ret = envmon_i2c_write(sensor->bus, address, &command, sizeof(command));
  if (ret < 0)
    {
      return ret;
    }

  sensor->address = address;
  sensor->initialized = true;
  return OK;
}

int envmon_bh1750_start(FAR struct envmon_bh1750_s *sensor)
{
  int ret;

  if (sensor == NULL || sensor->bus == NULL)
    {
      return -EINVAL;
    }

  ret = envmon_bh1750_try_address(sensor, ENVMON_BH1750_PRIMARY_ADDRESS);
  if (ret < 0)
    {
      ret = envmon_bh1750_try_address(sensor,
                                      ENVMON_BH1750_SECONDARY_ADDRESS);
    }

  if (ret < 0)
    {
      sensor->initialized = false;
      return ret;
    }

  usleep(180000);
  return OK;
}

uint32_t envmon_bh1750_convert(uint16_t raw)
{
  return ((uint32_t)raw * 25 + 1) / 3;
}

int envmon_bh1750_read(FAR struct envmon_bh1750_s *sensor,
                       FAR uint32_t *lux_x10)
{
  uint8_t data[2];
  uint16_t raw;
  int ret;

  if (sensor == NULL || !sensor->initialized || lux_x10 == NULL)
    {
      return -ENODEV;
    }

  ret = envmon_i2c_read(sensor->bus, sensor->address, data, sizeof(data));
  if (ret < 0)
    {
      return ret;
    }

  raw = ((uint16_t)data[0] << 8) | data[1];
  *lux_x10 = envmon_bh1750_convert(raw);
  return OK;
}

int envmon_bh1750_selftest(void)
{
  return envmon_bh1750_convert(120) == 1000 ? OK : -EINVAL;
}
