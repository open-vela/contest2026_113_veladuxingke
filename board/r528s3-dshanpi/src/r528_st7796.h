/****************************************************************************
 * contest2026_113_veladuxingke/board/r528s3-dshanpi/src/r528_st7796.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __BOARDS_R528_R528S3_DSHANPI_SRC_R528_ST7796_H
#define __BOARDS_R528_R528S3_DSHANPI_SRC_R528_ST7796_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/compiler.h>
#include <nuttx/lcd/lcd.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define R528_ST7796_XRES 480
#define R528_ST7796_YRES 320
#define R528_ST7796_BPP  16

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct r528_st7796_bus_s
{
  CODE int (*select)(bool selected);
  CODE int (*cmddata)(bool data);
  CODE int (*write)(FAR const uint8_t *buffer, size_t buflen);
  CODE int (*reset)(bool asserted);
  CODE int (*backlight)(bool enabled);
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int r528_st7796_lcdinitialize(FAR const struct r528_st7796_bus_s *bus,
                              FAR struct lcd_dev_s **lcd);
void r528_st7796_lcduninitialize(void);

#endif /* __BOARDS_R528_R528S3_DSHANPI_SRC_R528_ST7796_H */
