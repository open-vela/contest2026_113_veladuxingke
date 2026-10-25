/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/vad_detector.h
 *
 * 简单的 VAD (Voice Activity Detection) 语音活动检测
 ****************************************************************************/

#ifndef __VAD_DETECTOR_H
#define __VAD_DETECTOR_H

#include <nuttx/config.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* VAD 配置 */
struct vad_config_s
{
  uint32_t sample_rate;        /* 采样率 (Hz) */
  uint16_t frame_samples;      /* 每帧样本数 */
  uint16_t minimum_rms;        /* 最小 RMS 能量阈值 */
  uint16_t minimum_peak;       /* 最小峰值阈值 */
  uint16_t speech_frames;      /* 连续多少帧判定为语音开始 */
  uint16_t silence_frames;     /* 连续多少帧判定为语音结束 */
};

/* VAD 状态 */
struct vad_state_s
{
  bool in_speech;              /* 当前是否在语音段中 */
  uint16_t speech_count;       /* 连续语音帧计数 */
  uint16_t silence_count;      /* 连续静音帧计数 */
  uint32_t total_frames;       /* 总帧数 */
};

/* VAD 检测器 */
struct vad_detector_s
{
  struct vad_config_s config;
  struct vad_state_s state;
  bool initialized;
};

/**
 * 初始化 VAD 检测器
 */
int vad_detector_init(FAR struct vad_detector_s *detector,
                      FAR const struct vad_config_s *config);

/**
 * 处理一帧音频数据
 * @param detector VAD 检测器
 * @param samples PCM 样本数据 (16-bit signed)
 * @param count 样本数量
 * @return true = 检测到语音活动, false = 静音
 */
bool vad_detector_process_frame(FAR struct vad_detector_s *detector,
                                FAR const int16_t *samples,
                                uint16_t count);

/**
 * 重置 VAD 状态
 */
void vad_detector_reset(FAR struct vad_detector_s *detector);

/**
 * 获取当前状态
 */
void vad_detector_get_state(FAR struct vad_detector_s *detector,
                            FAR struct vad_state_s *state);

#ifdef __cplusplus
}
#endif

#endif /* __VAD_DETECTOR_H */
