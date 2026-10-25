/****************************************************************************
 * contest2026_113_veladuxingke/app/lan_panel/voice_config_api.h
 *
 * 语音配置 API 接口
 ****************************************************************************/

#ifndef __VOICE_CONFIG_API_H
#define __VOICE_CONFIG_API_H

#include <nuttx/config.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * 处理 GET /api/v1/voice/config
 * 获取当前语音配置
 */
int voice_config_api_get(int client_fd);

/**
 * 处理 POST /api/v1/voice/config
 * 更新语音配置
 */
int voice_config_api_post(int client_fd, const char *body, size_t body_len);

#ifdef __cplusplus
}
#endif

#endif /* __VOICE_CONFIG_API_H */
