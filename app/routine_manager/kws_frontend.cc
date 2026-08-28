/*
 * Fixed-point feature frontend shared by host training and R528 inference.
 * The constants intentionally match TensorFlow Lite Micro's Micro Speech
 * frontend.
 */

#include "kws_frontend.h"

#include <string.h>

#include "tensorflow/lite/experimental/microfrontend/lib/frontend.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"

namespace
{

constexpr int kSampleRate = 16000;
constexpr size_t kWindowSamples = 480;
constexpr size_t kStrideSamples = 320;
constexpr size_t kFeatureChannels = 40;
constexpr int32_t kFeatureValueScale = 256;
constexpr int32_t kFeatureValueDivisor = 666;
constexpr size_t kVadFrameSamples = 320;
constexpr size_t kVadSearchStartSamples = 0;
constexpr size_t kVadWindowFrames = 5;
constexpr size_t kVadRequiredActiveFrames = 3;
constexpr uint32_t kVadMinimumPeak = 900;
constexpr uint64_t kVadMinimumMeanSquare = 350 * 350;
constexpr size_t kAlignmentAnchorSamples = 9600;
constexpr size_t kAlignmentPrerollSamples = 1600;

bool VadFrameActive(const int16_t *samples)
{
  int64_t sum = 0;
  for (size_t index = 0; index < kVadFrameSamples; ++index)
    {
      sum += samples[index];
    }

  const int32_t mean = static_cast<int32_t>(
    sum / static_cast<int64_t>(kVadFrameSamples));
  uint64_t energy = 0;
  uint32_t peak = 0;

  for (size_t index = 0; index < kVadFrameSamples; ++index)
    {
      const int32_t value = static_cast<int32_t>(samples[index]) - mean;
      const uint32_t magnitude =
        static_cast<uint32_t>(value < 0 ? -value : value);
      if (magnitude > peak)
        {
          peak = magnitude;
        }

      energy += static_cast<uint64_t>(value * value);
    }

  return peak >= kVadMinimumPeak &&
         energy / kVadFrameSamples >= kVadMinimumMeanSquare;
}

int8_t QuantizeFeature(uint16_t feature)
{
  int32_t value =
    (static_cast<int32_t>(feature) * kFeatureValueScale +
     kFeatureValueDivisor / 2) / kFeatureValueDivisor - 128;

  if (value < -128)
    {
      value = -128;
    }
  else if (value > 127)
    {
      value = 127;
    }

  return static_cast<int8_t>(value);
}

}  // namespace

extern "C" size_t r528_kws_feature_channels(void)
{
  return kFeatureChannels;
}

extern "C" size_t r528_kws_feature_frames(size_t sample_count)
{
  if (sample_count < kWindowSamples)
    {
      return 0;
    }

  return 1 + (sample_count - kWindowSamples) / kStrideSamples;
}

extern "C" int r528_kws_align_audio(const int16_t *samples,
                                      size_t sample_count,
                                      int16_t *output,
                                      size_t output_capacity,
                                      size_t *speech_onset)
{
  if (samples == nullptr || output == nullptr || speech_onset == nullptr ||
      output_capacity <= kAlignmentAnchorSamples ||
      sample_count < kVadSearchStartSamples +
                     kVadWindowFrames * kVadFrameSamples)
    {
      return -1;
    }

  const size_t frame_count = sample_count / kVadFrameSamples;
  const size_t search_start = kVadSearchStartSamples / kVadFrameSamples;
  size_t onset_frame = frame_count;
  for (size_t start = search_start;
       start + kVadWindowFrames <= frame_count; ++start)
    {
      size_t active_frames = 0;
      size_t first_active = frame_count;
      for (size_t offset = 0; offset < kVadWindowFrames; ++offset)
        {
          const size_t frame = start + offset;
          if (VadFrameActive(samples + frame * kVadFrameSamples))
            {
              if (first_active == frame_count)
                {
                  first_active = frame;
                }

              ++active_frames;
            }
        }

      if (active_frames >= kVadRequiredActiveFrames)
        {
          onset_frame = first_active;
          break;
        }
    }

  if (onset_frame == frame_count)
    {
      *speech_onset = sample_count;
      memset(output, 0, output_capacity * sizeof(output[0]));
      return 1;
    }

  *speech_onset = onset_frame * kVadFrameSamples;
  const size_t retained_preroll =
    *speech_onset < kAlignmentPrerollSamples ? *speech_onset :
                                               kAlignmentPrerollSamples;
  const size_t source_start = *speech_onset - retained_preroll;
  const size_t output_start = kAlignmentAnchorSamples - retained_preroll;
  size_t copy_samples = sample_count - source_start;
  const size_t output_remaining = output_capacity - output_start;
  if (copy_samples > output_remaining)
    {
      copy_samples = output_remaining;
    }

  memmove(output + output_start, samples + source_start,
          copy_samples * sizeof(output[0]));
  memset(output, 0, output_start * sizeof(output[0]));
  memset(output + output_start + copy_samples, 0,
         (output_capacity - output_start - copy_samples) * sizeof(output[0]));
  return 0;
}

extern "C" int r528_kws_extract_features(const int16_t *samples,
                                          size_t sample_count,
                                          int8_t *output,
                                          size_t output_capacity,
                                          size_t *output_frames)
{
  if (samples == nullptr || output == nullptr || output_frames == nullptr)
    {
      return -1;
    }

  const size_t expected_frames = r528_kws_feature_frames(sample_count);
  if (expected_frames == 0 ||
      expected_frames > output_capacity / kFeatureChannels)
    {
      return -2;
    }

  FrontendConfig config;
  FrontendFillConfigWithDefaults(&config);
  config.window.size_ms = 30;
  config.window.step_size_ms = 20;
  config.filterbank.num_channels = kFeatureChannels;
  config.filterbank.lower_band_limit = 125.0f;
  config.filterbank.upper_band_limit = 7500.0f;
  config.pcan_gain_control.enable_pcan = 1;
  config.log_scale.enable_log = 1;
  config.log_scale.scale_shift = 6;

  FrontendState state;
  if (!FrontendPopulateState(&config, &state, kSampleRate))
    {
      return -3;
    }

  size_t sample_offset = 0;
  size_t frame = 0;
  int status = 0;
  while (sample_offset < sample_count && frame < expected_frames)
    {
      size_t samples_read = 0;
      const FrontendOutput frontend_output = FrontendProcessSamples(
        &state, samples + sample_offset, sample_count - sample_offset,
        &samples_read);
      sample_offset += samples_read;

      if (frontend_output.size == 0)
        {
          if (samples_read == 0)
            {
              status = -4;
              break;
            }

          continue;
        }

      if (frontend_output.size != kFeatureChannels)
        {
          status = -4;
          break;
        }

      for (size_t channel = 0; channel < kFeatureChannels; ++channel)
        {
          output[frame * kFeatureChannels + channel] =
            QuantizeFeature(frontend_output.values[channel]);
        }

      ++frame;
    }

  FrontendFreeStateContents(&state);
  if (status == 0 && frame != expected_frames)
    {
      status = -4;
    }

  *output_frames = frame;
  return status;
}
