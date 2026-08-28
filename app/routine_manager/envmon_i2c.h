/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/envmon_i2c.h
 ****************************************************************************/

#ifndef __ENVMON_I2C_H
#define __ENVMON_I2C_H

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>

#include <nuttx/i2c/i2c_master.h>

#define ENVMON_I2C_FREQUENCY 100000

typedef int (*envmon_i2c_transfer_t)(FAR void *arg,
                                     FAR struct i2c_msg_s *messages,
                                     size_t count);

struct envmon_i2c_s
{
  int fd;
  uint32_t frequency;
  envmon_i2c_transfer_t transfer;
  FAR void *transfer_arg;
};

int envmon_i2c_open(FAR struct envmon_i2c_s *bus, FAR const char *path);
void envmon_i2c_close(FAR struct envmon_i2c_s *bus);
int envmon_i2c_probe(FAR struct envmon_i2c_s *bus, uint8_t address);
int envmon_i2c_write(FAR struct envmon_i2c_s *bus, uint8_t address,
                     FAR const uint8_t *data, size_t length);
int envmon_i2c_read(FAR struct envmon_i2c_s *bus, uint8_t address,
                    FAR uint8_t *data, size_t length);
int envmon_i2c_write_read(FAR struct envmon_i2c_s *bus, uint8_t address,
                          FAR const uint8_t *write_data,
                          size_t write_length, FAR uint8_t *read_data,
                          size_t read_length);
int envmon_i2c_write_delay_read(FAR struct envmon_i2c_s *bus,
                                uint8_t address,
                                FAR const uint8_t *write_data,
                                size_t write_length, unsigned int delay_ms,
                                FAR uint8_t *read_data, size_t read_length);
void envmon_i2c_set_test_backend(FAR struct envmon_i2c_s *bus,
                                 envmon_i2c_transfer_t transfer,
                                 FAR void *arg);
uint8_t envmon_sensirion_crc8(FAR const uint8_t *data, size_t length);
int envmon_i2c_selftest(void);

#endif /* __ENVMON_I2C_H */
