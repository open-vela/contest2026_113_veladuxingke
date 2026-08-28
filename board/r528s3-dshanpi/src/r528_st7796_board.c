/****************************************************************************
 * contest2026_113_veladuxingke/board/r528s3-dshanpi/src/r528_st7796_board.c
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
#include <nuttx/board.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/spi/spi.h>

#include <hal_gpio.h>

#include "r528_st7796.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_R528_ST7796_CS_PIN
#  define CONFIG_R528_ST7796_CS_PIN 10
#endif

#ifndef CONFIG_R528_ST7796_SCK_PIN
#  define CONFIG_R528_ST7796_SCK_PIN 11
#endif

#ifndef CONFIG_R528_ST7796_MOSI_PIN
#  define CONFIG_R528_ST7796_MOSI_PIN 12
#endif

#ifndef CONFIG_R528_ST7796_DC_PIN
#  define CONFIG_R528_ST7796_DC_PIN 14
#endif

#ifndef CONFIG_R528_ST7796_RESET_PIN
#  define CONFIG_R528_ST7796_RESET_PIN 15
#endif

#ifndef CONFIG_R528_ST7796_BACKLIGHT_PIN
#  define CONFIG_R528_ST7796_BACKLIGHT_PIN 16
#endif

#ifndef CONFIG_R528_ST7796_SOFTSPI_DELAY_US
#  define CONFIG_R528_ST7796_SOFTSPI_DELAY_US 1
#endif

#ifndef CONFIG_R528_ST7796_SPI_FREQUENCY
#  define CONFIG_R528_ST7796_SPI_FREQUENCY 8000000
#endif

#define ST7796_CS_PIN          GPIOD(CONFIG_R528_ST7796_CS_PIN)
#define ST7796_SCK_PIN         GPIOD(CONFIG_R528_ST7796_SCK_PIN)
#define ST7796_MOSI_PIN        GPIOD(CONFIG_R528_ST7796_MOSI_PIN)
#define ST7796_DC_PIN          GPIOD(CONFIG_R528_ST7796_DC_PIN)
#define ST7796_RESET_PIN       GPIOD(CONFIG_R528_ST7796_RESET_PIN)
#define ST7796_BACKLIGHT_PIN   GPIOD(CONFIG_R528_ST7796_BACKLIGHT_PIN)

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int st7796_gpio_select(bool selected);
static int st7796_gpio_cmddata(bool data);
static int st7796_gpio_write(FAR const uint8_t *buffer, size_t buflen);
static int st7796_gpio_reset(bool asserted);
static int st7796_gpio_backlight(bool enabled);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct r528_st7796_bus_s g_st7796_bus =
{
  .select = st7796_gpio_select,
  .cmddata = st7796_gpio_cmddata,
  .write = st7796_gpio_write,
  .reset = st7796_gpio_reset,
  .backlight = st7796_gpio_backlight,
};

static FAR struct lcd_dev_s *g_st7796_lcd;
#ifndef CONFIG_R528_ST7796_SOFTSPI
static FAR struct spi_dev_s *g_st7796_spi;
#endif
static bool g_st7796_board_initialized;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int st7796_gpio_configure(gpio_pin_t pin, gpio_data_t initial)
{
  int ret;

  ret = hal_gpio_sel_vol_mode(pin, POWER_MODE_330);
  if (ret < 0)
    {
      return ret;
    }

  ret = hal_gpio_set_data(pin, initial);
  if (ret < 0)
    {
      return ret;
    }

  ret = hal_gpio_set_pull(pin, GPIO_PULL_DOWN_DISABLED);
  if (ret < 0)
    {
      return ret;
    }

  ret = hal_gpio_set_driving_level(pin, GPIO_DRIVING_LEVEL2);
  if (ret < 0)
    {
      return ret;
    }

  ret = hal_gpio_pinmux_set_function(pin, GPIO_MUXSEL_OUT);
  if (ret < 0)
    {
      return ret;
    }

  return hal_gpio_set_direction(pin, GPIO_DIRECTION_OUTPUT);
}

static int st7796_gpio_select(bool selected)
{
#ifdef CONFIG_R528_ST7796_SOFTSPI
  return hal_gpio_set_data(ST7796_CS_PIN,
                           selected ? GPIO_DATA_LOW : GPIO_DATA_HIGH);
#else
  SPI_SELECT(g_st7796_spi, 0, selected);
  return OK;
#endif
}

static int st7796_gpio_cmddata(bool data)
{
  return hal_gpio_set_data(ST7796_DC_PIN,
                           data ? GPIO_DATA_HIGH : GPIO_DATA_LOW);
}

static int st7796_gpio_write(FAR const uint8_t *buffer, size_t buflen)
{
#ifdef CONFIG_R528_ST7796_SOFTSPI
  size_t byte;
  int bit;
  int ret;
#endif

  if (buffer == NULL && buflen > 0)
    {
      return -EINVAL;
    }

#ifdef CONFIG_R528_ST7796_SOFTSPI
  for (byte = 0; byte < buflen; byte++)
    {
      uint8_t value = buffer[byte];

      for (bit = 0; bit < 8; bit++)
        {
          ret = hal_gpio_set_data(ST7796_MOSI_PIN,
                                  (value & 0x80) != 0 ? GPIO_DATA_HIGH :
                                                       GPIO_DATA_LOW);
          if (ret < 0)
            {
              return ret;
            }

          up_udelay(CONFIG_R528_ST7796_SOFTSPI_DELAY_US);
          ret = hal_gpio_set_data(ST7796_SCK_PIN, GPIO_DATA_HIGH);
          if (ret < 0)
            {
              return ret;
            }

          up_udelay(CONFIG_R528_ST7796_SOFTSPI_DELAY_US);
          ret = hal_gpio_set_data(ST7796_SCK_PIN, GPIO_DATA_LOW);
          if (ret < 0)
            {
              return ret;
            }

          value <<= 1;
        }
    }
#else
  SPI_SNDBLOCK(g_st7796_spi, buffer, buflen);
#endif

  return OK;
}

static int st7796_gpio_reset(bool asserted)
{
  return hal_gpio_set_data(ST7796_RESET_PIN,
                           asserted ? GPIO_DATA_LOW : GPIO_DATA_HIGH);
}

static int st7796_gpio_backlight(bool enabled)
{
#if defined(CONFIG_R528_ST7796_BACKLIGHT_ACTIVE_LOW)
  gpio_data_t value = enabled ? GPIO_DATA_LOW : GPIO_DATA_HIGH;
#else
  gpio_data_t value = enabled ? GPIO_DATA_HIGH : GPIO_DATA_LOW;
#endif

  return hal_gpio_set_data(ST7796_BACKLIGHT_PIN, value);
}

static bool st7796_pins_are_unique(void)
{
  const int pins[] =
  {
    CONFIG_R528_ST7796_CS_PIN,
    CONFIG_R528_ST7796_SCK_PIN,
    CONFIG_R528_ST7796_MOSI_PIN,
    CONFIG_R528_ST7796_DC_PIN,
    CONFIG_R528_ST7796_RESET_PIN,
    CONFIG_R528_ST7796_BACKLIGHT_PIN,
  };

  size_t i;
  size_t j;

  for (i = 0; i < sizeof(pins) / sizeof(pins[0]); i++)
    {
      if (pins[i] < 0 || pins[i] > 22)
        {
          return false;
        }

      for (j = i + 1; j < sizeof(pins) / sizeof(pins[0]); j++)
        {
          if (pins[i] == pins[j])
            {
              return false;
            }
        }
    }

  return true;
}

static int st7796_gpio_initialize(void)
{
  int ret;

  if (!st7796_pins_are_unique())
    {
      syslog(LOG_ERR,
             "st7796: invalid or duplicate Port D pin assignment\n");
      return -EINVAL;
    }

#if defined(CONFIG_R528_ST7796_BACKLIGHT_ACTIVE_LOW)
  ret = st7796_gpio_configure(ST7796_BACKLIGHT_PIN, GPIO_DATA_HIGH);
#else
  ret = st7796_gpio_configure(ST7796_BACKLIGHT_PIN, GPIO_DATA_LOW);
#endif
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_R528_ST7796_SOFTSPI
  ret = st7796_gpio_configure(ST7796_CS_PIN, GPIO_DATA_HIGH);
  if (ret < 0)
    {
      return ret;
    }

  ret = st7796_gpio_configure(ST7796_SCK_PIN, GPIO_DATA_LOW);
  if (ret < 0)
    {
      return ret;
    }

  ret = st7796_gpio_configure(ST7796_MOSI_PIN, GPIO_DATA_LOW);
  if (ret < 0)
    {
      return ret;
    }
#endif

  ret = st7796_gpio_configure(ST7796_DC_PIN, GPIO_DATA_LOW);
  if (ret < 0)
    {
      return ret;
    }

  return st7796_gpio_configure(ST7796_RESET_PIN, GPIO_DATA_LOW);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int board_lcd_initialize(void)
{
  int ret;

#ifndef CONFIG_R528_ST7796_SOFTSPI
  extern FAR struct spi_dev_s *sunxi_spibus_initialize(int port);
#endif

  if (g_st7796_board_initialized)
    {
      return OK;
    }

#ifndef CONFIG_R528_ST7796_SOFTSPI
  g_st7796_spi = sunxi_spibus_initialize(1);
  if (g_st7796_spi == NULL)
    {
      syslog(LOG_ERR, "st7796: SPI1 initialization failed\n");
      return -ENODEV;
    }

  SPI_SETMODE(g_st7796_spi, SPIDEV_MODE0);
  SPI_SETBITS(g_st7796_spi, 8);
#ifdef CONFIG_SPI_HWFEATURES
  ret = SPI_HWFEATURES(g_st7796_spi, 0);
  if (ret < 0)
    {
      return ret;
    }
#endif
  SPI_SETFREQUENCY(g_st7796_spi, CONFIG_R528_ST7796_SPI_FREQUENCY);
#endif

  ret = st7796_gpio_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "st7796: GPIO initialization failed: %d\n", ret);
      return ret;
    }

#ifdef CONFIG_R528_ST7796_SOFTSPI
  syslog(LOG_NOTICE,
         "lcd: softspi CS=PD%d SCK=PD%d MOSI=PD%d DC=PD%d "
         "RESET=PD%d BL=PD%d; PD13=MISO unused\n",
         CONFIG_R528_ST7796_CS_PIN, CONFIG_R528_ST7796_SCK_PIN,
         CONFIG_R528_ST7796_MOSI_PIN, CONFIG_R528_ST7796_DC_PIN,
         CONFIG_R528_ST7796_RESET_PIN,
         CONFIG_R528_ST7796_BACKLIGHT_PIN);
#else
  syslog(LOG_NOTICE,
         "lcd: hwspi1 %dHz CS=PD10 SCK=PD11 MOSI=PD12 DC=PD%d "
         "RESET=PD%d BL=PD%d; PD13=MISO unused\n",
         CONFIG_R528_ST7796_SPI_FREQUENCY, CONFIG_R528_ST7796_DC_PIN,
         CONFIG_R528_ST7796_RESET_PIN,
         CONFIG_R528_ST7796_BACKLIGHT_PIN);
#endif

  ret = r528_st7796_lcdinitialize(&g_st7796_bus, &g_st7796_lcd);
  if (ret < 0)
    {
      st7796_gpio_backlight(false);
      st7796_gpio_select(false);
      st7796_gpio_reset(true);
      g_st7796_lcd = NULL;
      return ret;
    }

  g_st7796_board_initialized = true;
  return OK;
}

FAR struct lcd_dev_s *board_lcd_getdev(int lcddev)
{
  return lcddev == 0 && g_st7796_board_initialized ? g_st7796_lcd : NULL;
}

void board_lcd_uninitialize(void)
{
  if (!g_st7796_board_initialized)
    {
      return;
    }

  r528_st7796_lcduninitialize();
  g_st7796_lcd = NULL;
  g_st7796_board_initialized = false;
}
