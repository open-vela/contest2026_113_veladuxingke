/****************************************************************************
 * contest2026_113_veladuxingke/app/wifi_manager/wifi_config.h
 ****************************************************************************/

#ifndef __WIFI_CONFIG_H
#define __WIFI_CONFIG_H

#include <stddef.h>

#define WIFI_CONFIG_SSID_MAX       32
#define WIFI_CONFIG_PASSWORD_MIN   8
#define WIFI_CONFIG_PASSWORD_MAX   63
#define WIFI_CONFIG_FILE_MAX       512

struct wifi_config_s
{
  char ssid[WIFI_CONFIG_SSID_MAX + 1];
  char password[WIFI_CONFIG_PASSWORD_MAX + 1];
};

int wifi_config_parse(const char *data, size_t length,
                      struct wifi_config_s *config);
void wifi_config_clear(struct wifi_config_s *config);
int wifi_config_equal(const struct wifi_config_s *left,
                      const struct wifi_config_s *right);

#endif /* __WIFI_CONFIG_H */
