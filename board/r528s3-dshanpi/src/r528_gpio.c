/* Copyright (c) 2019-2025 Allwinner Technology Co., Ltd. ALL rights reserved.
 *
 * Board-specific GPIO initialization for smart lighting control
 */

#include <nuttx/config.h>

#include <stdio.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/ioexpander/gpio.h>

#ifdef CONFIG_DEV_GPIO

#define GPIO_PD21  21
#define GPIOD_BASE 96  /* Port D starts at GPIO 96 */

/****************************************************************************
 * Name: r528_gpio_initialize
 *
 * Description:
 *   Initialize GPIO pins for smart lighting control
 *
 ****************************************************************************/

int r528_gpio_initialize(void)
{
  int ret;

  /* Register PD21 as GPIO output for LED control */
  ret = gpio_lower_half_byname("/dev/gpio21",
                                GPIOD_BASE + GPIO_PD21,
                                GPIO_OUTPUT_PIN,
                                1);  /* Initial value: HIGH (LED off) */

  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to register GPIO21 (PD21): %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO, "GPIO21 (PD21) registered successfully\n");
  return OK;
}

#endif /* CONFIG_DEV_GPIO */
