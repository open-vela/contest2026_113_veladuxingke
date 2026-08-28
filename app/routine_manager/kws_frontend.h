/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/kws_frontend.h
 ****************************************************************************/

#ifndef __KWS_FRONTEND_H
#define __KWS_FRONTEND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

size_t r528_kws_feature_channels(void);
size_t r528_kws_feature_frames(size_t sample_count);
int r528_kws_align_audio(const int16_t *samples, size_t sample_count,
                         int16_t *output, size_t output_capacity,
                         size_t *speech_onset);
int r528_kws_extract_features(const int16_t *samples, size_t sample_count,
                              int8_t *output, size_t output_capacity,
                              size_t *output_frames);

#ifdef __cplusplus
}
#endif

#endif /* __KWS_FRONTEND_H */
