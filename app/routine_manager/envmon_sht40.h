/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/envmon_sht40.h
 ****************************************************************************/

#ifndef __ENVMON_SHT40_H
#define __ENVMON_SHT40_H

#include <nuttx/config.h>

#include <stdint.h>

#include "envmon_i2c.h"

#define ENVMON_SHT40_ADDRESS 0x44

struct envmon_sht40_s
{
  FAR struct envmon_i2c_s *bus;
};

void envmon_sht40_init(FAR struct envmon_sht40_s *sensor,
                       FAR struct envmon_i2c_s *bus);
int envmon_sht40_measure(FAR struct envmon_sht40_s *sensor,
                         FAR int32_t *temperature_x100,
                         FAR uint16_t *humidity_x100);
int envmon_sht40_decode(FAR const uint8_t data[6],
                        FAR int32_t *temperature_x100,
                        FAR uint16_t *humidity_x100);
int envmon_sht40_selftest(void);

#endif /* __ENVMON_SHT40_H */
