/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/envmon_sgp30.h
 ****************************************************************************/

#ifndef __ENVMON_SGP30_H
#define __ENVMON_SGP30_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "envmon_i2c.h"

#define ENVMON_SGP30_ADDRESS 0x58

struct envmon_sgp30_s
{
  FAR struct envmon_i2c_s *bus;
  struct timespec initialized_at;
  bool initialized;
};

void envmon_sgp30_reset(FAR struct envmon_sgp30_s *sensor,
                        FAR struct envmon_i2c_s *bus);
int envmon_sgp30_init_air_quality(FAR struct envmon_sgp30_s *sensor);
int envmon_sgp30_measure(FAR struct envmon_sgp30_s *sensor,
                         FAR uint16_t *eco2_ppm,
                         FAR uint16_t *tvoc_ppb);
int envmon_sgp30_decode(FAR const uint8_t data[6],
                        FAR uint16_t *eco2_ppm,
                        FAR uint16_t *tvoc_ppb);
bool envmon_sgp30_warming_up(FAR const struct envmon_sgp30_s *sensor,
                             FAR const struct timespec *now);
int envmon_sgp30_selftest(void);

#endif /* __ENVMON_SGP30_H */
