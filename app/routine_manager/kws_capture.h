/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/kws_capture.h
 ****************************************************************************/

#ifndef __KWS_CAPTURE_H
#define __KWS_CAPTURE_H

#include <nuttx/config.h>

#include <stdint.h>

#define KWS_CAPTURE_ID_MAX 24
#define KWS_CAPTURE_LABEL_MAX 24

struct kws_capture_options_s
{
  FAR const char *label;
  FAR const char *speaker_id;
  FAR const char *session_id;
  FAR const char *device_id;
  FAR const char *capture_device;
  FAR const char *playback_device;
  FAR const char *output_root;
  FAR const char *storage_mountpoint;
  uint32_t record_ms;
  uint16_t playback_volume;
  uint16_t count;
};

int kws_capture_validate_options(
  FAR const struct kws_capture_options_s *options);
int kws_capture_run(FAR const struct kws_capture_options_s *options);
int kws_capture_selftest(void);

#endif /* __KWS_CAPTURE_H */
