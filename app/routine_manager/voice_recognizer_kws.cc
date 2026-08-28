/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/voice_recognizer_kws.cc
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "tensorflow/lite/core/c/common.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "kws_frontend.h"
#include "kws_model_data.h"
#include "voice_recognizer.h"

#define KWS_AUDIO_SAMPLES       57344
#define KWS_AUDIO_BYTES         (KWS_AUDIO_SAMPLES * sizeof(int16_t))
#define KWS_MIN_AUDIO_SAMPLES   16000
#define KWS_TENSOR_ARENA_BYTES  (128 * 1024)

namespace
{

alignas(16) uint8_t g_kws_tensor_arena[KWS_TENSOR_ARENA_BYTES];
int16_t g_kws_audio[KWS_AUDIO_SAMPLES];

using KwsOpResolver = tflite::MicroMutableOpResolver<5>;

static_assert(R528_KWS_CATEGORY_COUNT == 7,
              "KWS output category contract changed");
static_assert(R528_KWS_WAKE_INDEX < R528_KWS_CATEGORY_COUNT,
              "KWS wake index is outside the output tensor");

TfLiteStatus RegisterKwsOps(KwsOpResolver &resolver)
{
  TF_LITE_ENSURE_STATUS(resolver.AddConv2D());
  TF_LITE_ENSURE_STATUS(resolver.AddAveragePool2D());
  TF_LITE_ENSURE_STATUS(resolver.AddReshape());
  TF_LITE_ENSURE_STATUS(resolver.AddFullyConnected());
  TF_LITE_ENSURE_STATUS(resolver.AddSoftmax());
  return kTfLiteOk;
}

bool TensorShapeEquals(const TfLiteTensor *tensor, const int *dimensions,
                       size_t dimension_count)
{
  if (tensor == nullptr || tensor->dims == nullptr ||
      tensor->dims->size != static_cast<int>(dimension_count))
    {
      return false;
    }

  for (size_t index = 0; index < dimension_count; index++)
    {
      if (tensor->dims->data[index] != dimensions[index])
        {
          return false;
        }
    }

  return true;
}

int LoadPcm(FAR const char *path)
{
  uint8_t *destination = reinterpret_cast<uint8_t *>(g_kws_audio);
  size_t total = 0;
  int fd;

  memset(g_kws_audio, 0, sizeof(g_kws_audio));
  fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    {
      return -errno;
    }

  while (total < KWS_AUDIO_BYTES)
    {
      ssize_t nread = read(fd, destination + total, KWS_AUDIO_BYTES - total);
      if (nread < 0)
        {
          int error = errno;
          if (error == EINTR)
            {
              continue;
            }

          close(fd);
          return -error;
        }

      if (nread == 0)
        {
          break;
        }

      total += nread;
    }

  close(fd);
  if ((total & 1) != 0)
    {
      return -EINVAL;
    }

  if (total / sizeof(int16_t) < KWS_MIN_AUDIO_SAMPLES)
    {
      return -ENODATA;
    }

  return OK;
}

int RecognizeSamples(FAR struct voice_recognition_result_s *result)
{
  static const int input_shape[] =
  {
    1, R528_KWS_FEATURE_FRAMES, R528_KWS_FEATURE_CHANNELS, 1
  };
  static const int output_shape[] =
  {
    1, R528_KWS_CATEGORY_COUNT
  };
  const tflite::Model *model;
  TfLiteTensor *input;
  TfLiteTensor *output;
  KwsOpResolver resolver;
  size_t feature_frames = 0;
  size_t speech_onset = 0;
  int prediction_index = 0;
  int8_t prediction;
  int16_t threshold;
  int ret;

  memset(result, 0, sizeof(*result));
  result->command = VOICE_COMMAND_UNKNOWN;
  if (g_r528_kws_model_data_len != R528_KWS_MODEL_EXPECTED_BYTES)
    {
      return -EINVAL;
    }

  model = tflite::GetModel(g_r528_kws_model_data);
  if (model == nullptr || model->version() != TFLITE_SCHEMA_VERSION ||
      RegisterKwsOps(resolver) != kTfLiteOk)
    {
      return -ENOTSUP;
    }

  memset(g_kws_tensor_arena, 0, sizeof(g_kws_tensor_arena));
  tflite::MicroInterpreter interpreter(model, resolver, g_kws_tensor_arena,
                                       sizeof(g_kws_tensor_arena));
  if (interpreter.AllocateTensors() != kTfLiteOk)
    {
      return -ENOMEM;
    }

  input = interpreter.input(0);
  output = interpreter.output(0);
  if (!TensorShapeEquals(input, input_shape,
                         sizeof(input_shape) / sizeof(input_shape[0])) ||
      !TensorShapeEquals(output, output_shape,
                         sizeof(output_shape) / sizeof(output_shape[0])) ||
      input->type != kTfLiteInt8 || output->type != kTfLiteInt8 ||
      input->bytes != R528_KWS_FEATURE_FRAMES * R528_KWS_FEATURE_CHANNELS ||
      output->bytes != R528_KWS_CATEGORY_COUNT ||
      input->params.scale != 1.0f || input->params.zero_point != 0 ||
      output->params.scale != (1.0f / 256.0f) ||
      output->params.zero_point != -128)
    {
      return -EINVAL;
    }

  ret = r528_kws_align_audio(g_kws_audio, KWS_AUDIO_SAMPLES,
                             g_kws_audio, KWS_AUDIO_SAMPLES,
                             &speech_onset);
  if (ret > 0)
    {
      return -ENODATA;
    }
  else if (ret < 0)
    {
      return -EIO;
    }

  syslog(LOG_INFO, "routine_mgr: kws speech_onset_ms=%lu\n",
         static_cast<unsigned long>(speech_onset * 1000 / 16000));
  ret = r528_kws_extract_features(
    g_kws_audio, KWS_AUDIO_SAMPLES, input->data.int8, input->bytes,
    &feature_frames);
  if (ret < 0 || feature_frames != R528_KWS_FEATURE_FRAMES)
    {
      return -EIO;
    }

  if (interpreter.Invoke() != kTfLiteOk)
    {
      return -EIO;
    }

  for (int index = 1; index < R528_KWS_CATEGORY_COUNT; index++)
    {
      if (output->data.int8[index] > output->data.int8[prediction_index])
        {
          prediction_index = index;
        }
    }

  prediction = output->data.int8[prediction_index];
  result->score = static_cast<uint16_t>(
    (static_cast<int>(prediction) + 128) * 1000 / 256);
  syslog(LOG_INFO,
         "routine_mgr: kws raw background=%d speech=%d humidity=%d "
         "light=%d air=%d temperature=%d wake=%d argmax=%d "
         "arena=%lu\n",
         output->data.int8[0], output->data.int8[1], output->data.int8[2],
         output->data.int8[3], output->data.int8[4], output->data.int8[5],
         output->data.int8[6], prediction_index,
         static_cast<unsigned long>(interpreter.arena_used_bytes()));

  switch (prediction_index)
    {
      case R528_KWS_HUMIDITY_INDEX:
        threshold = R528_KWS_HUMIDITY_THRESHOLD;
        result->command = VOICE_COMMAND_QUERY_HUMIDITY;
        break;

      case R528_KWS_LIGHT_INDEX:
        threshold = R528_KWS_LIGHT_THRESHOLD;
        result->command = VOICE_COMMAND_QUERY_LIGHT;
        break;

      case R528_KWS_AIR_QUALITY_INDEX:
        threshold = R528_KWS_AIR_QUALITY_THRESHOLD;
        result->command = VOICE_COMMAND_QUERY_AIR_QUALITY;
        break;

      case R528_KWS_TEMPERATURE_INDEX:
        threshold = R528_KWS_TEMPERATURE_THRESHOLD;
        result->command = VOICE_COMMAND_QUERY_TEMPERATURE;
        break;

      case R528_KWS_WAKE_INDEX:
        threshold = R528_KWS_WAKE_THRESHOLD;
        result->command = VOICE_COMMAND_WAKE;
        break;

      default:
        return -ENODATA;
    }

  if (prediction < threshold)
    {
      result->command = VOICE_COMMAND_UNKNOWN;
      return -ENODATA;
    }

  result->semantic_verified = true;
  return OK;
}

}  // namespace

extern "C" int voice_recognizer_recognize(
  FAR const char *pcm_path, FAR struct voice_recognition_result_s *result)
{
  int ret;

  if (pcm_path == nullptr || result == nullptr)
    {
      return -EINVAL;
    }

  ret = LoadPcm(pcm_path);
  if (ret < 0)
    {
      return ret;
    }

  return RecognizeSamples(result);
}

extern "C" int voice_recognizer_selftest(void)
{
  struct voice_recognition_result_s result;
  int ret;

  memset(g_kws_audio, 0, sizeof(g_kws_audio));
  ret = RecognizeSamples(&result);
  if (ret != -ENODATA || result.command != VOICE_COMMAND_UNKNOWN ||
      result.semantic_verified)
    {
      return -EINVAL;
    }

  return OK;
}
