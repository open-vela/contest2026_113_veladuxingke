/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/envmon_bh1750.h
 ****************************************************************************/

#ifndef __ENVMON_BH1750_H
#define __ENVMON_BH1750_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include "envmon_i2c.h"

struct envmon_bh1750_s
{
  FAR struct envmon_i2c_s *bus;
  uint8_t address;
  bool initialized;
};

void envmon_bh1750_reset(FAR struct envmon_bh1750_s *sensor,
                         FAR struct envmon_i2c_s *bus);
int envmon_bh1750_start(FAR struct envmon_bh1750_s *sensor);
int envmon_bh1750_read(FAR struct envmon_bh1750_s *sensor,
                       FAR uint32_t *lux_x10);
uint32_t envmon_bh1750_convert(uint16_t raw);
int envmon_bh1750_selftest(void);

#endif /* __ENVMON_BH1750_H */
