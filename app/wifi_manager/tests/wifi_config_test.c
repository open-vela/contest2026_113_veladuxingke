/****************************************************************************
 * contest2026_113_veladuxingke/app/wifi_manager/tests/wifi_config_test.c
 ****************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../wifi_config.h"

static void expect_valid(const char *text, const char *ssid,
                         const char *password)
{
  struct wifi_config_s config;
  int ret;

  memset(&config, 0xa5, sizeof(config));
  ret = wifi_config_parse(text, strlen(text), &config);
  assert(ret == 0);
  assert(strcmp(config.ssid, ssid) == 0);
  assert(strcmp(config.password, password) == 0);
  wifi_config_clear(&config);
}

static void expect_invalid(const char *text)
{
  struct wifi_config_s before;
  struct wifi_config_s after;
  int ret;

  memset(&before, 0x5a, sizeof(before));
  after = before;
  ret = wifi_config_parse(text, strlen(text), &after);
  assert(ret < 0);
  assert(memcmp(&before, &after, sizeof(before)) == 0);
}

int main(void)
{
  char ssid32[33];
  char pass63[64];
  char boundary[160];

  expect_valid("SSID=Home\nPASSWORD=12345678\n",
               "Home", "12345678");
  expect_valid("SSID=\"My Home\"\r\nPASSWORD=\"pass word\"\r\n",
               "My Home", "pass word");
  expect_valid("# comment\n\nSSID=\"a;`$()<>\"\n"
               "PASSWORD=\"1234;`$()<>5678\"\n",
               "a;`$()<>", "1234;`$()<>5678");
  expect_valid("SSID=\"a\\\"b\"\nPASSWORD=\"1234\\\\5678\"\n",
               "a\"b", "1234\\5678");

  memset(ssid32, 's', 32);
  ssid32[32] = '\0';
  memset(pass63, 'p', 63);
  pass63[63] = '\0';
  snprintf(boundary, sizeof(boundary), "SSID=%s\nPASSWORD=%s\n",
           ssid32, pass63);
  expect_valid(boundary, ssid32, pass63);

  expect_invalid("SSID=\nPASSWORD=12345678\n");
  expect_invalid("SSID=Home\nPASSWORD=short\n");
  expect_invalid("SSID=Home\n");
  expect_invalid("PASSWORD=12345678\n");
  expect_invalid("SSID=A\nSSID=B\nPASSWORD=12345678\n");
  expect_invalid("SSID=\"broken\nPASSWORD=12345678\n");
  expect_invalid("SSID=Home\nPASSWORD=12345678\nUNKNOWN=value\n");
  expect_invalid("SSID=Home\nPASSWORD=12345678\nSSID=Again\n");

  puts("wifi_config_test: PASS");
  return 0;
}
