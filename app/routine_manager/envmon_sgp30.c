/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/envmon_sgp30.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <time.h>

#include "envmon_sgp30.h"

#define ENVMON_SGP30_WARMUP_SECONDS 15

void envmon_sgp30_reset(FAR struct envmon_sgp30_s *sensor,
                        FAR struct envmon_i2c_s *bus)
{
  sensor->bus = bus;
  sensor->initialized_at.tv_sec = 0;
  sensor->initialized_at.tv_nsec = 0;
  sensor->initialized = false;
}

int envmon_sgp30_init_air_quality(FAR struct envmon_sgp30_s *sensor)
{
  static const uint8_t command[] = {0x20, 0x03};
  int ret;

  if (sensor == NULL || sensor->bus == NULL)
    {
      return -EINVAL;
    }

  ret = envmon_i2c_write(sensor->bus, ENVMON_SGP30_ADDRESS,
                         command, sizeof(command));
  if (ret < 0)
    {
      sensor->initialized = false;
      return ret;
    }

  struct timespec delay = {0, 10 * 1000 * 1000};
  nanosleep(&delay, NULL);
  clock_gettime(CLOCK_MONOTONIC, &sensor->initialized_at);
  sensor->initialized = true;
  return OK;
}

int envmon_sgp30_decode(FAR const uint8_t data[6],
                        FAR uint16_t *eco2_ppm,
                        FAR uint16_t *tvoc_ppb)
{
  if (data == NULL || eco2_ppm == NULL || tvoc_ppb == NULL)
    {
      return -EINVAL;
    }

  if (envmon_sensirion_crc8(data, 2) != data[2] ||
      envmon_sensirion_crc8(data + 3, 2) != data[5])
    {
      return -EBADMSG;
    }

  *eco2_ppm = ((uint16_t)data[0] << 8) | data[1];
  *tvoc_ppb = ((uint16_t)data[3] << 8) | data[4];
  return OK;
}

int envmon_sgp30_measure(FAR struct envmon_sgp30_s *sensor,
                         FAR uint16_t *eco2_ppm,
                         FAR uint16_t *tvoc_ppb)
{
  static const uint8_t command[] = {0x20, 0x08};
  uint8_t data[6];
  int ret;

  if (sensor == NULL || !sensor->initialized)
    {
      return -ENODEV;
    }

  ret = envmon_i2c_write_delay_read(sensor->bus, ENVMON_SGP30_ADDRESS,
                                    command, sizeof(command), 12,
                                    data, sizeof(data));
  if (ret < 0)
    {
      return ret;
    }

  return envmon_sgp30_decode(data, eco2_ppm, tvoc_ppb);
}

bool envmon_sgp30_warming_up(FAR const struct envmon_sgp30_s *sensor,
                             FAR const struct timespec *now)
{
  if (sensor == NULL || !sensor->initialized || now == NULL)
    {
      return false;
    }

  return now->tv_sec - sensor->initialized_at.tv_sec <
         ENVMON_SGP30_WARMUP_SECONDS;
}

int envmon_sgp30_selftest(void)
{
  uint8_t data[6] = {0x01, 0x90, 0, 0x00, 0x00, 0};
  uint16_t eco2;
  uint16_t tvoc;

  data[2] = envmon_sensirion_crc8(data, 2);
  data[5] = envmon_sensirion_crc8(data + 3, 2);
  if (envmon_sgp30_decode(data, &eco2, &tvoc) < 0 ||
      eco2 != 400 || tvoc != 0)
    {
      return -EINVAL;
    }

  data[5] ^= 1;
  return envmon_sgp30_decode(data, &eco2, &tvoc) == -EBADMSG ? OK :
                                                                    -EINVAL;
}
