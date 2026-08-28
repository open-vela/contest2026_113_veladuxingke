/****************************************************************************
 * contest2026_113_veladuxingke/board/r528s3-dshanpi/src/r528_ft5x06_stub.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stub implementation for r528_ft5x06_register to satisfy the linker.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <sys/types.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

struct i2c_master_s;
int r528_ft5x06_register(FAR struct i2c_master_s *i2c_bus)
{
  /* Stub: touchscreen registration is handled elsewhere */
  return 0;
}