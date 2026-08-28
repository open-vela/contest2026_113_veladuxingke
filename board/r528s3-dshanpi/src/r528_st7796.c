/****************************************************************************
 * contest2026_113_veladuxingke/board/r528s3-dshanpi/src/r528_st7796.c
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
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/mutex.h>
#include <nuttx/video/fb.h>

#include "r528_st7796.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ST7796_SLPIN       0x10
#define ST7796_SLPOUT      0x11
#define ST7796_INVON       0x21
#define ST7796_DISPOFF     0x28
#define ST7796_DISPON      0x29
#define ST7796_CASET       0x2a
#define ST7796_RASET       0x2b
#define ST7796_RAMWR       0x2c
#define ST7796_MADCTL      0x36
#define ST7796_COLMOD      0x3a

#define ST7796_MADCTL_MY   (1 << 7)
#define ST7796_MADCTL_MX   (1 << 6)
#define ST7796_MADCTL_MV   (1 << 5)
#define ST7796_MADCTL_BGR  (1 << 3)

#ifndef CONFIG_R528_ST7796_MADCTL
#  define CONFIG_R528_ST7796_MADCTL \
     (ST7796_MADCTL_MY | ST7796_MADCTL_MX | ST7796_MADCTL_MV | \
      ST7796_MADCTL_BGR)
#endif

#define ST7796_RESET_LOW_US       100000
#define ST7796_RESET_RELEASE_US   100000
#define ST7796_SLEEP_OUT_US       120000
#define ST7796_SLEEP_IN_US        120000
#define ST7796_LINEBUFFER_SIZE    (R528_ST7796_XRES * 2)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct st7796_dev_s
{
  struct lcd_dev_s dev;
  FAR const struct r528_st7796_bus_s *bus;
  mutex_t lock;
  uint8_t runbuffer[ST7796_LINEBUFFER_SIZE];
  uint8_t txbuffer[ST7796_LINEBUFFER_SIZE];
  uint8_t power;
  bool initialized;
  bool sleeping;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int st7796_getvideoinfo(FAR struct lcd_dev_s *dev,
                               FAR struct fb_videoinfo_s *vinfo);
static int st7796_getplaneinfo(FAR struct lcd_dev_s *dev,
                               unsigned int planeno,
                               FAR struct lcd_planeinfo_s *pinfo);
static int st7796_putrun(FAR struct lcd_dev_s *dev, fb_coord_t row,
                         fb_coord_t col, FAR const uint8_t *buffer,
                         size_t npixels);
static int st7796_putarea(FAR struct lcd_dev_s *dev, fb_coord_t row_start,
                          fb_coord_t row_end, fb_coord_t col_start,
                          fb_coord_t col_end, FAR const uint8_t *buffer,
                          fb_coord_t stride);
static int st7796_getpower(FAR struct lcd_dev_s *dev);
static int st7796_setpower(FAR struct lcd_dev_s *dev, int power);
static int st7796_getcontrast(FAR struct lcd_dev_s *dev);
static int st7796_setcontrast(FAR struct lcd_dev_s *dev,
                              unsigned int contrast);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const uint8_t g_lcd_madctl[] =
{
  CONFIG_R528_ST7796_MADCTL
};

static const uint8_t g_lcd_colmod[] =
{
  0x55
};

static const uint8_t g_lcd_e0[] =
{
  0xf0, 0x3e, 0x30, 0x06, 0x0a, 0x03, 0x4d, 0x56,
  0x3a, 0x06, 0x0f, 0x04, 0x18, 0x13, 0x00
};

static const uint8_t g_lcd_e1[] =
{
  0x0f, 0x37, 0x31, 0x0b, 0x0d, 0x06, 0x4d, 0x34,
  0x38, 0x06, 0x11, 0x01, 0x18, 0x13, 0x00
};

static const uint8_t g_lcd_c0[] =
{
  0x18, 0x17
};

static const uint8_t g_lcd_c1[] =
{
  0x41
};

static const uint8_t g_lcd_c5[] =
{
  0x00
};

static const uint8_t g_lcd_1a[] =
{
  0x80
};

static const uint8_t g_lcd_b0[] =
{
  0x00
};

static const uint8_t g_lcd_b1[] =
{
  0xa0
};

static const uint8_t g_lcd_b4[] =
{
  0x02
};

static const uint8_t g_lcd_b6[] =
{
  0x02, 0x02
};

static const uint8_t g_lcd_e9[] =
{
  0x00
};

static const uint8_t g_lcd_f7[] =
{
  0xa9, 0x51, 0x2c, 0x82
};

static const uint8_t g_lcd_columns[] =
{
  0x00, 0x00, 0x01, 0xdf
};

static const uint8_t g_lcd_rows[] =
{
  0x00, 0x00, 0x01, 0x3f
};

static struct st7796_dev_s g_st7796 =
{
  .dev =
    {
      .getvideoinfo = st7796_getvideoinfo,
      .getplaneinfo = st7796_getplaneinfo,
      .getpower = st7796_getpower,
      .setpower = st7796_setpower,
      .getcontrast = st7796_getcontrast,
      .setcontrast = st7796_setcontrast,
    },
  .lock = NXMUTEX_INITIALIZER,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int st7796_select(FAR struct st7796_dev_s *priv, bool selected)
{
  return priv->bus->select(selected);
}

static int st7796_write(FAR struct st7796_dev_s *priv, bool data,
                        FAR const uint8_t *buffer, size_t buflen)
{
  int ret;

  ret = priv->bus->cmddata(data);
  if (ret < 0)
    {
      return ret;
    }

  return priv->bus->write(buffer, buflen);
}

static int st7796_command(FAR struct st7796_dev_s *priv, uint8_t command,
                          FAR const uint8_t *data, size_t datalen)
{
  int ret;

  ret = st7796_select(priv, true);
  if (ret < 0)
    {
      return ret;
    }

  ret = st7796_write(priv, false, &command, 1);
  if (ret >= 0 && datalen > 0)
    {
      ret = st7796_write(priv, true, data, datalen);
    }

  if (st7796_select(priv, false) < 0 && ret >= 0)
    {
      ret = -EIO;
    }

  return ret;
}

static int st7796_setwindow(FAR struct st7796_dev_s *priv,
                            uint16_t col_start, uint16_t col_end,
                            uint16_t row_start, uint16_t row_end)
{
  uint8_t data[4];
  int ret;

  data[0] = col_start >> 8;
  data[1] = col_start & 0xff;
  data[2] = col_end >> 8;
  data[3] = col_end & 0xff;
  ret = st7796_command(priv, ST7796_CASET, data, sizeof(data));
  if (ret < 0)
    {
      return ret;
    }

  data[0] = row_start >> 8;
  data[1] = row_start & 0xff;
  data[2] = row_end >> 8;
  data[3] = row_end & 0xff;
  return st7796_command(priv, ST7796_RASET, data, sizeof(data));
}

static void st7796_convert_rgb565(FAR struct st7796_dev_s *priv,
                                  FAR const uint8_t *source,
                                  size_t npixels)
{
  size_t i;

  for (i = 0; i < npixels; i++)
    {
      priv->txbuffer[2 * i] = source[2 * i + 1];
      priv->txbuffer[2 * i + 1] = source[2 * i];
    }
}

static int st7796_writearea(FAR struct st7796_dev_s *priv,
                            fb_coord_t row_start, fb_coord_t row_end,
                            fb_coord_t col_start, fb_coord_t col_end,
                            FAR const uint8_t *buffer, fb_coord_t stride)
{
  size_t width = col_end - col_start + 1;
  size_t rows = row_end - row_start + 1;
  size_t rowbytes = width * 2;
  size_t row;
  uint8_t command = ST7796_RAMWR;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = st7796_setwindow(priv, col_start, col_end, row_start, row_end);
  if (ret < 0)
    {
      goto out;
    }

  ret = st7796_select(priv, true);
  if (ret < 0)
    {
      goto out;
    }

  ret = st7796_write(priv, false, &command, 1);
  if (ret < 0)
    {
      goto deselect;
    }

  ret = priv->bus->cmddata(true);
  if (ret < 0)
    {
      goto deselect;
    }

  for (row = 0; row < rows; row++)
    {
      st7796_convert_rgb565(priv, buffer + row * stride, width);
      ret = priv->bus->write(priv->txbuffer, rowbytes);
      if (ret < 0)
        {
          break;
        }
    }

deselect:
  if (st7796_select(priv, false) < 0 && ret >= 0)
    {
      ret = -EIO;
    }

out:
  nxmutex_unlock(&priv->lock);
  return ret;
}

static int st7796_initcommand(FAR struct st7796_dev_s *priv,
                               uint8_t command,
                               FAR const uint8_t *data, size_t datalen)
{
  int ret = st7796_command(priv, command, data, datalen);

  if (ret < 0)
    {
      syslog(LOG_ERR, "st7796: command 0x%02x failed: %d\n",
             command, ret);
    }

  return ret;
}

static int st7796_panelinit(FAR struct st7796_dev_s *priv)
{
  int ret;

  ret = priv->bus->backlight(false);
  if (ret < 0)
    {
      return ret;
    }

  ret = priv->bus->reset(true);
  if (ret < 0)
    {
      return ret;
    }

  up_udelay(ST7796_RESET_LOW_US);
  ret = priv->bus->reset(false);
  if (ret < 0)
    {
      return ret;
    }

  up_udelay(ST7796_RESET_RELEASE_US);

#define ST7796_INIT(command, data) \
  do \
    { \
      ret = st7796_initcommand(priv, command, data, sizeof(data)); \
      if (ret < 0) \
        { \
          return ret; \
        } \
    } \
  while (0)

  ST7796_INIT(0xe0, g_lcd_e0);
  ST7796_INIT(0xe1, g_lcd_e1);
  ST7796_INIT(0xc0, g_lcd_c0);
  ST7796_INIT(0xc1, g_lcd_c1);
  ST7796_INIT(0xc5, g_lcd_c5);
  ST7796_INIT(0x1a, g_lcd_1a);
  ST7796_INIT(ST7796_MADCTL, g_lcd_madctl);
  ST7796_INIT(ST7796_COLMOD, g_lcd_colmod);
  ST7796_INIT(0xb0, g_lcd_b0);
  ST7796_INIT(0xb1, g_lcd_b1);
  ST7796_INIT(0xb4, g_lcd_b4);
  ST7796_INIT(0xb6, g_lcd_b6);
  ST7796_INIT(0xe9, g_lcd_e9);
  ST7796_INIT(0xf7, g_lcd_f7);

#undef ST7796_INIT

  ret = st7796_command(priv, ST7796_INVON, NULL, 0);
  if (ret < 0)
    {
      return ret;
    }

  up_udelay(ST7796_SLEEP_OUT_US);
  ret = st7796_command(priv, ST7796_SLPOUT, NULL, 0);
  if (ret < 0)
    {
      return ret;
    }

  up_udelay(ST7796_SLEEP_OUT_US);
  ret = st7796_command(priv, ST7796_MADCTL, g_lcd_madctl,
                       sizeof(g_lcd_madctl));
  if (ret < 0)
    {
      return ret;
    }

  ret = st7796_command(priv, ST7796_CASET, g_lcd_columns,
                       sizeof(g_lcd_columns));
  if (ret < 0)
    {
      return ret;
    }

  ret = st7796_command(priv, ST7796_RASET, g_lcd_rows,
                       sizeof(g_lcd_rows));
  if (ret < 0)
    {
      return ret;
    }

  ret = st7796_command(priv, ST7796_DISPON, NULL, 0);
  if (ret < 0)
    {
      return ret;
    }

  priv->sleeping = false;
  return OK;
}

static int st7796_getvideoinfo(FAR struct lcd_dev_s *dev,
                               FAR struct fb_videoinfo_s *vinfo)
{
  if (dev == NULL || vinfo == NULL)
    {
      return -EINVAL;
    }

  memset(vinfo, 0, sizeof(*vinfo));
  vinfo->fmt = FB_FMT_RGB16_565;
  vinfo->xres = R528_ST7796_XRES;
  vinfo->yres = R528_ST7796_YRES;
  vinfo->nplanes = 1;
  return OK;
}

static int st7796_getplaneinfo(FAR struct lcd_dev_s *dev,
                               unsigned int planeno,
                               FAR struct lcd_planeinfo_s *pinfo)
{
  FAR struct st7796_dev_s *priv = (FAR struct st7796_dev_s *)dev;

  if (dev == NULL || pinfo == NULL || planeno != 0)
    {
      return -EINVAL;
    }

  memset(pinfo, 0, sizeof(*pinfo));
  pinfo->putrun = st7796_putrun;
  pinfo->putarea = st7796_putarea;
  pinfo->buffer = priv->runbuffer;
  pinfo->bpp = R528_ST7796_BPP;
  pinfo->dev = dev;
  return OK;
}

static int st7796_putrun(FAR struct lcd_dev_s *dev, fb_coord_t row,
                         fb_coord_t col, FAR const uint8_t *buffer,
                         size_t npixels)
{
  FAR struct st7796_dev_s *priv = (FAR struct st7796_dev_s *)dev;

  if (priv == NULL || buffer == NULL || row < 0 || col < 0 || npixels == 0 ||
      row >= R528_ST7796_YRES || col >= R528_ST7796_XRES ||
      npixels > R528_ST7796_XRES - col)
    {
      return -EINVAL;
    }

  return st7796_writearea(priv, row, row, col, col + npixels - 1,
                          buffer, npixels * 2);
}

static int st7796_putarea(FAR struct lcd_dev_s *dev, fb_coord_t row_start,
                          fb_coord_t row_end, fb_coord_t col_start,
                          fb_coord_t col_end, FAR const uint8_t *buffer,
                          fb_coord_t stride)
{
  FAR struct st7796_dev_s *priv = (FAR struct st7796_dev_s *)dev;
  size_t rowbytes;

  if (priv == NULL || buffer == NULL || row_start < 0 || col_start < 0 ||
      stride <= 0 || row_end < row_start || col_end < col_start ||
      row_end >= R528_ST7796_YRES || col_end >= R528_ST7796_XRES)
    {
      return -EINVAL;
    }

  rowbytes = (col_end - col_start + 1) * 2;
  if (stride < rowbytes)
    {
      return -EINVAL;
    }

  return st7796_writearea(priv, row_start, row_end, col_start, col_end,
                          buffer, stride);
}

static int st7796_getpower(FAR struct lcd_dev_s *dev)
{
  FAR struct st7796_dev_s *priv = (FAR struct st7796_dev_s *)dev;

  return priv == NULL ? -EINVAL : priv->power;
}

static int st7796_setpower(FAR struct lcd_dev_s *dev, int power)
{
  FAR struct st7796_dev_s *priv = (FAR struct st7796_dev_s *)dev;
  int ret;

  if (priv == NULL || power < 0 || power > CONFIG_LCD_MAXPOWER)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (power == 0 && priv->power != 0)
    {
      ret = priv->bus->backlight(false);
      if (ret >= 0)
        {
          ret = st7796_command(priv, ST7796_DISPOFF, NULL, 0);
        }

      if (ret >= 0)
        {
          ret = st7796_command(priv, ST7796_SLPIN, NULL, 0);
        }

      if (ret >= 0)
        {
          up_udelay(ST7796_SLEEP_IN_US);
          priv->sleeping = true;
          priv->power = 0;
        }
    }
  else if (power > 0 && priv->power == 0)
    {
      if (priv->sleeping)
        {
          ret = st7796_command(priv, ST7796_SLPOUT, NULL, 0);
          if (ret >= 0)
            {
              up_udelay(ST7796_SLEEP_OUT_US);
              priv->sleeping = false;
            }
        }
      else
        {
          ret = OK;
        }

      if (ret >= 0)
        {
          ret = st7796_command(priv, ST7796_DISPON, NULL, 0);
        }

      if (ret >= 0)
        {
          ret = priv->bus->backlight(true);
        }

      if (ret >= 0)
        {
          priv->power = power;
        }
    }
  else
    {
      ret = OK;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

static int st7796_getcontrast(FAR struct lcd_dev_s *dev)
{
  return dev == NULL ? -EINVAL : 0;
}

static int st7796_setcontrast(FAR struct lcd_dev_s *dev,
                              unsigned int contrast)
{
  return dev == NULL ? -EINVAL : -ENOSYS;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int r528_st7796_lcdinitialize(FAR const struct r528_st7796_bus_s *bus,
                              FAR struct lcd_dev_s **lcd)
{
  FAR struct st7796_dev_s *priv = &g_st7796;
  int ret;

  if (bus == NULL || lcd == NULL || bus->select == NULL ||
      bus->cmddata == NULL || bus->write == NULL || bus->reset == NULL ||
      bus->backlight == NULL)
    {
      return -EINVAL;
    }

  if (!priv->initialized)
    {
      priv->bus = bus;
      priv->power = 0;
      ret = st7796_panelinit(priv);
      if (ret < 0)
        {
          syslog(LOG_ERR, "st7796: panel initialization failed: %d\n", ret);
          bus->backlight(false);
          bus->select(false);
          bus->reset(true);
          priv->bus = NULL;
          return ret;
        }

      priv->initialized = true;
      syslog(LOG_INFO,
             "lcd: initialized %dx%d RGB565, working backup sequence, "
             "MADCTL=0x%02x\n",
             R528_ST7796_XRES, R528_ST7796_YRES,
             CONFIG_R528_ST7796_MADCTL);
    }

  *lcd = &priv->dev;
  return OK;
}

void r528_st7796_lcduninitialize(void)
{
  FAR struct st7796_dev_s *priv = &g_st7796;

  if (!priv->initialized)
    {
      return;
    }

  st7796_setpower(&priv->dev, 0);
  priv->bus->backlight(false);
  priv->bus->select(false);
  priv->bus->reset(true);
  priv->bus = NULL;
  priv->initialized = false;
}
