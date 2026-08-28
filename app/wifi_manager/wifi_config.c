/****************************************************************************
 * contest2026_113_veladuxingke/app/wifi_manager/wifi_config.c
 ****************************************************************************/

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "wifi_config.h"

static void wifi_config_wipe(void *buffer, size_t length)
{
  volatile unsigned char *cursor = buffer;

  while (length-- > 0)
    {
      *cursor++ = 0;
    }
}

static bool wifi_config_control_char(char value)
{
  unsigned char character = value;

  return character < 0x20 || character == 0x7f;
}

static int wifi_config_decode_value(const char *value, size_t length,
                                    char *output, size_t output_size,
                                    size_t *decoded_length)
{
  size_t input_index = 0;
  size_t output_index = 0;
  bool quoted = false;

  while (input_index < length &&
         (value[input_index] == ' ' || value[input_index] == '\t'))
    {
      input_index++;
    }

  while (length > input_index &&
         (value[length - 1] == ' ' || value[length - 1] == '\t'))
    {
      length--;
    }

  if (input_index < length && value[input_index] == '"')
    {
      quoted = true;
      input_index++;
    }

  while (input_index < length)
    {
      char character = value[input_index++];

      if (quoted && character == '"')
        {
          while (input_index < length &&
                 (value[input_index] == ' ' ||
                  value[input_index] == '\t'))
            {
              input_index++;
            }

          if (input_index != length)
            {
              return -EINVAL;
            }

          quoted = false;
          break;
        }

      if (quoted && character == '\\')
        {
          if (input_index >= length)
            {
              return -EINVAL;
            }

          character = value[input_index++];
          if (character != '\\' && character != '"')
            {
              return -EINVAL;
            }
        }

      if (wifi_config_control_char(character) ||
          output_index + 1 >= output_size)
        {
          return -E2BIG;
        }

      output[output_index++] = character;
    }

  if (quoted)
    {
      return -EINVAL;
    }

  output[output_index] = '\0';
  *decoded_length = output_index;
  return 0;
}

static int wifi_config_parse_line(const char *line, size_t length,
                                  struct wifi_config_s *candidate,
                                  bool *ssid_found,
                                  bool *password_found)
{
  char *destination;
  size_t destination_size;
  size_t decoded_length;
  size_t key_length;
  int ret;

  while (length > 0 && (*line == ' ' || *line == '\t'))
    {
      line++;
      length--;
    }

  while (length > 0 &&
         (line[length - 1] == ' ' || line[length - 1] == '\t'))
    {
      length--;
    }

  if (length == 0 || line[0] == '#')
    {
      return 0;
    }

  if (length > 5 && memcmp(line, "SSID=", 5) == 0)
    {
      if (*ssid_found)
        {
          return -EINVAL;
        }

      key_length = 5;
      destination = candidate->ssid;
      destination_size = sizeof(candidate->ssid);
      *ssid_found = true;
    }
  else if (length > 9 && memcmp(line, "PASSWORD=", 9) == 0)
    {
      if (*password_found)
        {
          return -EINVAL;
        }

      key_length = 9;
      destination = candidate->password;
      destination_size = sizeof(candidate->password);
      *password_found = true;
    }
  else
    {
      return -EINVAL;
    }

  ret = wifi_config_decode_value(line + key_length, length - key_length,
                                 destination, destination_size,
                                 &decoded_length);
  if (ret < 0)
    {
      return ret;
    }

  if (destination == candidate->ssid)
    {
      if (decoded_length == 0 || decoded_length > WIFI_CONFIG_SSID_MAX)
        {
          return -EINVAL;
        }
    }
  else if (decoded_length < WIFI_CONFIG_PASSWORD_MIN ||
           decoded_length > WIFI_CONFIG_PASSWORD_MAX)
    {
      return -EINVAL;
    }

  return 0;
}

int wifi_config_parse(const char *data, size_t length,
                      struct wifi_config_s *config)
{
  struct wifi_config_s candidate;
  bool ssid_found = false;
  bool password_found = false;
  size_t line_start = 0;
  size_t index;
  int ret = 0;

  if (data == NULL || config == NULL || length == 0 ||
      length > WIFI_CONFIG_FILE_MAX)
    {
      return -EINVAL;
    }

  memset(&candidate, 0, sizeof(candidate));

  for (index = 0; index <= length; index++)
    {
      if (index < length && data[index] == '\0')
        {
          ret = -EINVAL;
          goto out;
        }

      if (index == length || data[index] == '\n')
        {
          size_t line_length = index - line_start;

          if (line_length > 0 && data[line_start + line_length - 1] == '\r')
            {
              line_length--;
            }

          ret = wifi_config_parse_line(data + line_start, line_length,
                                       &candidate, &ssid_found,
                                       &password_found);
          if (ret < 0)
            {
              goto out;
            }

          line_start = index + 1;
        }
    }

  if (!ssid_found || !password_found)
    {
      ret = -EINVAL;
      goto out;
    }

  *config = candidate;
  wifi_config_wipe(&candidate, sizeof(candidate));
  return 0;

out:
  wifi_config_wipe(&candidate, sizeof(candidate));
  return ret;
}

void wifi_config_clear(struct wifi_config_s *config)
{
  if (config != NULL)
    {
      wifi_config_wipe(config, sizeof(*config));
    }
}

int wifi_config_equal(const struct wifi_config_s *left,
                      const struct wifi_config_s *right)
{
  if (left == NULL || right == NULL)
    {
      return 0;
    }

  return strcmp(left->ssid, right->ssid) == 0 &&
         strcmp(left->password, right->password) == 0;
}
