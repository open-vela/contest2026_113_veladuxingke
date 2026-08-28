/****************************************************************************
 * apps/st7796_test/st7796_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/video/fb.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ST7796_TEST_DEVICE       "/dev/fb0"
#define ST7796_TEST_BPP          16
#define ST7796_TEST_DELAY_US     600000

#define RGB565_BLACK             0x0000
#define RGB565_WHITE             0xffff
#define RGB565_RED               0xf800
#define RGB565_GREEN             0x07e0
#define RGB565_BLUE              0x001f
#define RGB565_YELLOW            0xffe0
#define RGB565_CYAN              0x07ff
#define RGB565_MAGENTA           0xf81f

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct st7796_test_s
{
  int fd;
  FAR uint16_t *fb;
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint64_t st7796_now_us(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int st7796_update(FAR struct st7796_test_s *test,
                          fb_coord_t x, fb_coord_t y,
                          fb_coord_t width, fb_coord_t height,
                          FAR const char *name)
{
  struct fb_area_s area;
  uint64_t start;
  uint64_t elapsed;
  int ret;

  area.x = x;
  area.y = y;
  area.w = width;
  area.h = height;

  start = st7796_now_us();
  ret = ioctl(test->fd, FBIO_UPDATE, (unsigned long)(uintptr_t)&area);
  elapsed = st7796_now_us() - start;
  printf("st7796_test: %-12s update ret=%d elapsed=%" PRIu64 " us\n",
         name, ret, elapsed);
  return ret < 0 ? -errno : OK;
}

static void st7796_fill(FAR struct st7796_test_s *test, uint16_t color)
{
  size_t x;
  size_t y;
  size_t stride = test->pinfo.stride / sizeof(uint16_t);

  for (y = 0; y < test->vinfo.yres; y++)
    {
      for (x = 0; x < test->vinfo.xres; x++)
        {
          test->fb[y * stride + x] = color;
        }
    }
}

static void st7796_rectangle(FAR struct st7796_test_s *test,
                              fb_coord_t x, fb_coord_t y,
                              fb_coord_t width, fb_coord_t height,
                              uint16_t color)
{
  size_t stride = test->pinfo.stride / sizeof(uint16_t);
  fb_coord_t px;
  fb_coord_t py;

  for (py = y; py < y + height; py++)
    {
      for (px = x; px < x + width; px++)
        {
          test->fb[py * stride + px] = color;
        }
    }
}

static int st7796_solid(FAR struct st7796_test_s *test,
                         uint16_t color, FAR const char *name)
{
  int ret;

  st7796_fill(test, color);
  ret = st7796_update(test, 0, 0, test->vinfo.xres, test->vinfo.yres,
                      name);
  if (ret >= 0)
    {
      usleep(ST7796_TEST_DELAY_US);
    }

  return ret;
}

static int st7796_quadrants(FAR struct st7796_test_s *test)
{
  fb_coord_t halfw = test->vinfo.xres / 2;
  fb_coord_t halfh = test->vinfo.yres / 2;

  st7796_rectangle(test, 0, 0, halfw, halfh, RGB565_RED);
  st7796_rectangle(test, halfw, 0, halfw, halfh, RGB565_GREEN);
  st7796_rectangle(test, 0, halfh, halfw, halfh, RGB565_BLUE);
  st7796_rectangle(test, halfw, halfh, halfw, halfh, RGB565_WHITE);
  return st7796_update(test, 0, 0, test->vinfo.xres, test->vinfo.yres,
                       "quadrants");
}

static int st7796_corners(FAR struct st7796_test_s *test)
{
  const fb_coord_t marker = 32;

  st7796_fill(test, RGB565_BLACK);
  st7796_rectangle(test, 0, 0, marker, marker, RGB565_RED);
  st7796_rectangle(test, test->vinfo.xres - marker, 0,
                    marker, marker, RGB565_GREEN);
  st7796_rectangle(test, 0, test->vinfo.yres - marker,
                    marker, marker, RGB565_BLUE);
  st7796_rectangle(test, test->vinfo.xres - marker,
                    test->vinfo.yres - marker,
                    marker, marker, RGB565_WHITE);
  return st7796_update(test, 0, 0, test->vinfo.xres, test->vinfo.yres,
                       "corners");
}

static int st7796_partial(FAR struct st7796_test_s *test)
{
  fb_coord_t x = test->vinfo.xres / 4;
  fb_coord_t y = test->vinfo.yres / 4;
  fb_coord_t width = test->vinfo.xres / 2;
  fb_coord_t height = test->vinfo.yres / 2;
  fb_coord_t inset_x = width / 6;
  fb_coord_t inset_y = height / 6;

  st7796_rectangle(test, x, y, width, height, RGB565_YELLOW);
  st7796_rectangle(test, x + inset_x, y + inset_y,
                    width - 2 * inset_x, height - 2 * inset_y,
                    RGB565_MAGENTA);
  return st7796_update(test, x, y, width, height, "partial");
}

static int st7796_validate(FAR struct st7796_test_s *test)
{
  printf("st7796_test: fmt=%u xres=%d yres=%d bpp=%u stride=%d "
         "fblen=%zu\n",
         test->vinfo.fmt, test->vinfo.xres, test->vinfo.yres,
         test->pinfo.bpp, test->pinfo.stride, test->pinfo.fblen);

  if (test->vinfo.fmt != FB_FMT_RGB16_565 ||
      test->vinfo.xres <= 0 || test->vinfo.yres <= 0 ||
      test->pinfo.bpp != ST7796_TEST_BPP ||
      test->pinfo.stride < test->vinfo.xres * 2 ||
      test->pinfo.fblen < (size_t)test->pinfo.stride * test->vinfo.yres)
    {
      fprintf(stderr, "st7796_test: unexpected framebuffer geometry\n");
      return -EINVAL;
    }

  return OK;
}

static int st7796_parse_cycles(int argc, FAR char *argv[])
{
  long cycles = 1;
  FAR char *end;

  if (argc == 1)
    {
      return 1;
    }

  if (argc != 3 || strcmp(argv[1], "--cycles") != 0)
    {
      return -EINVAL;
    }

  errno = 0;
  cycles = strtol(argv[2], &end, 10);
  if (errno != 0 || *end != '\0' || cycles < 1 || cycles > 1000)
    {
      return -EINVAL;
    }

  return cycles;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct st7796_test_s test;
  int cycles;
  int cycle;
  int ret;

  cycles = st7796_parse_cycles(argc, argv);
  if (cycles < 0)
    {
      fprintf(stderr, "Usage: st7796_test [--cycles 1..1000]\n");
      return EXIT_FAILURE;
    }

  memset(&test, 0, sizeof(test));
  test.fd = open(ST7796_TEST_DEVICE, O_RDWR);
  if (test.fd < 0)
    {
      fprintf(stderr, "st7796_test: open %s failed: %d\n",
              ST7796_TEST_DEVICE, errno);
      return EXIT_FAILURE;
    }

  ret = ioctl(test.fd, FBIOGET_VIDEOINFO,
              (unsigned long)(uintptr_t)&test.vinfo);
  if (ret < 0)
    {
      fprintf(stderr, "st7796_test: FBIOGET_VIDEOINFO failed: %d\n", errno);
      goto out_close;
    }

  ret = ioctl(test.fd, FBIOGET_PLANEINFO,
              (unsigned long)(uintptr_t)&test.pinfo);
  if (ret < 0)
    {
      fprintf(stderr, "st7796_test: FBIOGET_PLANEINFO failed: %d\n", errno);
      goto out_close;
    }

  ret = st7796_validate(&test);
  if (ret < 0)
    {
      goto out_close;
    }

  test.fb = mmap(NULL, test.pinfo.fblen, PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_FILE, test.fd, 0);
  if (test.fb == MAP_FAILED)
    {
      test.fb = NULL;
      fprintf(stderr, "st7796_test: mmap failed: %d\n", errno);
      ret = -errno;
      goto out_close;
    }

  for (cycle = 0; cycle < cycles; cycle++)
    {
      printf("st7796_test: cycle %d/%d\n", cycle + 1, cycles);
      if ((ret = st7796_solid(&test, RGB565_BLACK, "black")) < 0 ||
          (ret = st7796_solid(&test, RGB565_WHITE, "white")) < 0 ||
          (ret = st7796_solid(&test, RGB565_RED, "red")) < 0 ||
          (ret = st7796_solid(&test, RGB565_GREEN, "green")) < 0 ||
          (ret = st7796_solid(&test, RGB565_BLUE, "blue")) < 0 ||
          (ret = st7796_quadrants(&test)) < 0 ||
          (ret = st7796_corners(&test)) < 0 ||
          (ret = st7796_partial(&test)) < 0)
        {
          break;
        }

      usleep(ST7796_TEST_DELAY_US);
    }

  printf("st7796_test: software updates complete; "
         "verify the physical panel\n");
  munmap(test.fb, test.pinfo.fblen);

out_close:
  close(test.fd);
  return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
