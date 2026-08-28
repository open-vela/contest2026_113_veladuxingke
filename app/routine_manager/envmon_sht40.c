/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/envmon_sht40.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>

#include "envmon_sht40.h"

#define ENVMON_SHT40_MEASURE_HIGH 0xfd

void envmon_sht40_init(FAR struct envmon_sht40_s *sensor,
                       FAR struct envmon_i2c_s *bus)
{
  sensor->bus = bus;
}

int envmon_sht40_decode(FAR const uint8_t data[6],
                        FAR int32_t *temperature_x100,
                        FAR uint16_t *humidity_x100)
{
  uint16_t raw_temperature;
  uint16_t raw_humidity;
  int64_t temperature;
  int64_t humidity;

  if (data == NULL || temperature_x100 == NULL || humidity_x100 == NULL)
    {
      return -EINVAL;
    }

  if (envmon_sensirion_crc8(data, 2) != data[2] ||
      envmon_sensirion_crc8(data + 3, 2) != data[5])
    {
      return -EBADMSG;
    }

  raw_temperature = ((uint16_t)data[0] << 8) | data[1];
  raw_humidity = ((uint16_t)data[3] << 8) | data[4];
  temperature = -4500 + ((int64_t)raw_temperature * 17500 + 32767) / 65535;
  humidity = -600 + ((int64_t)raw_humidity * 12500 + 32767) / 65535;
  if (humidity < 0)
    {
      humidity = 0;
    }
  else if (humidity > 10000)
    {
      humidity = 10000;
    }

  *temperature_x100 = temperature;
  *humidity_x100 = humidity;
  return OK;
}

int envmon_sht40_measure(FAR struct envmon_sht40_s *sensor,
                         FAR int32_t *temperature_x100,
                         FAR uint16_t *humidity_x100)
{
  uint8_t command = ENVMON_SHT40_MEASURE_HIGH;
  uint8_t data[6];
  int ret;

  if (sensor == NULL || sensor->bus == NULL)
    {
      return -EINVAL;
    }

  ret = envmon_i2c_write_delay_read(sensor->bus, ENVMON_SHT40_ADDRESS,
                                    &command, sizeof(command), 10,
                                    data, sizeof(data));
  if (ret < 0)
    {
      return ret;
    }

  return envmon_sht40_decode(data, temperature_x100, humidity_x100);
}

int envmon_sht40_selftest(void)
{
  uint8_t data[6] = {0x66, 0x66, 0, 0x80, 0x00, 0};
  int32_t temperature;
  uint16_t humidity;

  data[2] = envmon_sensirion_crc8(data, 2);
  data[5] = envmon_sensirion_crc8(data + 3, 2);
  if (envmon_sht40_decode(data, &temperature, &humidity) < 0 ||
      temperature < 2490 || temperature > 2510 ||
      humidity < 5640 || humidity > 5660)
    {
      return -EINVAL;
    }

  data[2] ^= 1;
  return envmon_sht40_decode(data, &temperature, &humidity) == -EBADMSG ?
         OK : -EINVAL;
}
