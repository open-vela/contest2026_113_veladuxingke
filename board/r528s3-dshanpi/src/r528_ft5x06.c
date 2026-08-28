/****************************************************************************
 * contest2026_113_veladuxingke/board/r528s3-dshanpi/src/r528_ft5x06.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/input/ft5x06.h>

#include <hal_gpio.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FT5X06_I2C_ADDRESS       0x38
#define FT5X06_I2C_FREQUENCY     400000
#define FT5X06_RESET_PIN         GPIOE(0)
#define FT5X06_INTERRUPT_PIN     GPIOE(1)
#define FT5X06_RESET_LOW_US      10000
#define FT5X06_RESET_RELEASE_US  300000
#define FT5X06_SCREEN_WIDTH      480
#define FT5X06_SCREEN_HEIGHT     320

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifndef CONFIG_FT5X06_POLLMODE
static xcpt_t g_ft5x06_isr;
static FAR void *g_ft5x06_isr_arg;
static uint32_t g_ft5x06_irq;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifndef CONFIG_FT5X06_POLLMODE
static hal_irqreturn_t r528_ft5x06_interrupt(FAR void *arg)
{
  if (g_ft5x06_isr != NULL)
    {
      g_ft5x06_isr(g_ft5x06_irq, NULL, g_ft5x06_isr_arg);
    }

  return HAL_IRQ_OK;
}

static int r528_ft5x06_attach(
  FAR const struct ft5x06_config_s *config, xcpt_t isr, FAR void *arg)
{
  int ret;

  g_ft5x06_isr = isr;
  g_ft5x06_isr_arg = arg;

  ret = hal_gpio_irq_request(g_ft5x06_irq, r528_ft5x06_interrupt,
                             IRQ_TYPE_EDGE_FALLING, NULL);
  if (ret < 0)
    {
      g_ft5x06_isr = NULL;
      g_ft5x06_isr_arg = NULL;
      return -EIO;
    }

  return OK;
}

static void r528_ft5x06_enable(
  FAR const struct ft5x06_config_s *config, bool enable)
{
  if (enable)
    {
      hal_gpio_irq_enable(g_ft5x06_irq);
    }
  else
    {
      hal_gpio_irq_disable(g_ft5x06_irq);
    }
}

static void r528_ft5x06_clear(
  FAR const struct ft5x06_config_s *config)
{
}
#endif

static void r528_ft5x06_wakeup(
  FAR const struct ft5x06_config_s *config)
{
}

static void r528_ft5x06_nreset(
  FAR const struct ft5x06_config_s *config, bool state)
{
  hal_gpio_set_data(FT5X06_RESET_PIN,
                    state ? GPIO_DATA_HIGH : GPIO_DATA_LOW);
}

static void r528_ft5x06_transform(
  FAR const struct ft5x06_config_s *config,
  FAR int16_t *x, FAR int16_t *y)
{
  int16_t raw_x = *x;
  int16_t raw_y = *y;
  int16_t screen_x = FT5X06_SCREEN_WIDTH - 1 - raw_y;
  int16_t screen_y = raw_x;

  if (screen_x < 0)
    {
      screen_x = 0;
    }
  else if (screen_x >= FT5X06_SCREEN_WIDTH)
    {
      screen_x = FT5X06_SCREEN_WIDTH - 1;
    }

  if (screen_y < 0)
    {
      screen_y = 0;
    }
  else if (screen_y >= FT5X06_SCREEN_HEIGHT)
    {
      screen_y = FT5X06_SCREEN_HEIGHT - 1;
    }

  *x = screen_x;
  *y = screen_y;
}

static struct ft5x06_config_s g_ft5x06_config =
{
  .address = FT5X06_I2C_ADDRESS,
  .frequency = FT5X06_I2C_FREQUENCY,
#ifndef CONFIG_FT5X06_POLLMODE
  .attach = r528_ft5x06_attach,
  .enable = r528_ft5x06_enable,
  .clear = r528_ft5x06_clear,
#endif
  .wakeup = r528_ft5x06_wakeup,
  .nreset = r528_ft5x06_nreset,
  .transform = r528_ft5x06_transform,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int r528_ft5x06_register(FAR struct i2c_master_s *i2c_bus)
{
  int ret;

  if (i2c_bus == NULL)
    {
      return -ENODEV;
    }

  ret = hal_gpio_sel_vol_mode(FT5X06_RESET_PIN, POWER_MODE_330);
  if (ret < 0)
    {
      return ret;
    }

  ret = hal_gpio_set_data(FT5X06_RESET_PIN, GPIO_DATA_LOW);
  if (ret < 0)
    {
      return ret;
    }

  ret = hal_gpio_pinmux_set_function(FT5X06_RESET_PIN, GPIO_MUXSEL_OUT);
  if (ret < 0)
    {
      return ret;
    }

  ret = hal_gpio_set_direction(FT5X06_RESET_PIN, GPIO_DIRECTION_OUTPUT);
  if (ret < 0)
    {
      return ret;
    }

#ifndef CONFIG_FT5X06_POLLMODE
  ret = hal_gpio_sel_vol_mode(FT5X06_INTERRUPT_PIN, POWER_MODE_330);
  if (ret < 0)
    {
      return ret;
    }

  ret = hal_gpio_set_pull(FT5X06_INTERRUPT_PIN, GPIO_PULL_UP);
  if (ret < 0)
    {
      return ret;
    }

  ret = hal_gpio_set_direction(FT5X06_INTERRUPT_PIN, GPIO_DIRECTION_INPUT);
  if (ret < 0)
    {
      return ret;
    }

  ret = hal_gpio_to_irq(FT5X06_INTERRUPT_PIN, &g_ft5x06_irq);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ft5x06: failed to map INT PE1 to IRQ: %d\n", ret);
      return ret;
    }
#endif

  up_udelay(FT5X06_RESET_LOW_US);
  r528_ft5x06_nreset(&g_ft5x06_config, true);
  up_udelay(FT5X06_RESET_RELEASE_US);

  ret = ft5x06_register(i2c_bus, &g_ft5x06_config, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ft5x06: register failed: %d\n", ret);
      return ret;
    }

  syslog(LOG_NOTICE,
         "ft5x06: /dev/input0 TWI2 addr=0x38 SDA=PE13 SCL=PE12 "
         "RESET=PE0 INT=PE1\n");
  return OK;
}
