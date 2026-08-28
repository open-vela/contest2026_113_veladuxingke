/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/voice_reply.h
 ****************************************************************************/

#ifndef __VOICE_REPLY_H
#define __VOICE_REPLY_H

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>

enum voice_reply_type_e
{
  VOICE_REPLY_TEMPERATURE = 0,
  VOICE_REPLY_HUMIDITY,
  VOICE_REPLY_SHT40,
  VOICE_REPLY_BH1750,
  VOICE_REPLY_SGP30,
  VOICE_REPLY_TEMPERATURE_LOW_WARNING,
  VOICE_REPLY_TEMPERATURE_HIGH_WARNING,
  VOICE_REPLY_SCHEDULE_REMINDER
};

typedef bool (*voice_reply_cancel_t)(FAR void *arg);

struct voice_reply_values_s
{
  int32_t temperature_x100;
  uint16_t humidity_x100;
  uint32_t light_x10;
  uint16_t eco2_ppm;
  uint16_t tvoc_ppb;
  uint8_t schedule_text_id;
  uint8_t schedule_hour;
  uint8_t schedule_minute;
  uint8_t schedule_second;
};

int voice_reply_format_temperature(FAR char *buffer, size_t size,
                                   int32_t temperature_x100);
int voice_reply_format(FAR char *buffer, size_t size,
                       enum voice_reply_type_e type,
                       FAR const struct voice_reply_values_s *values);
int voice_reply_build_cancelable(
  FAR const char *asset_dir, FAR const char *output_path,
  enum voice_reply_type_e type,
  FAR const struct voice_reply_values_s *values,
  FAR uint32_t *duration_ms, voice_reply_cancel_t cancel,
  FAR void *cancel_arg);
int voice_reply_build(FAR const char *asset_dir,
                      FAR const char *output_path,
                      enum voice_reply_type_e type,
                      FAR const struct voice_reply_values_s *values,
                      FAR uint32_t *duration_ms);
int voice_reply_build_temperature(FAR const char *asset_dir,
                                  FAR const char *output_path,
                                  int32_t temperature_x100,
                                  FAR uint32_t *duration_ms);
int voice_reply_selftest(void);

#endif /* __VOICE_REPLY_H */
