/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/routine_config.h
 ****************************************************************************/

#ifndef __ROUTINE_CONFIG_H
#define __ROUTINE_CONFIG_H

#include <nuttx/config.h>

#include "routine_model.h"

#define ROUTINE_CONFIG_PATH "/data/routine/config.json"

void routine_config_defaults(FAR struct routine_settings_s *settings);
int routine_config_load(FAR struct routine_settings_s *settings);
int routine_config_save(FAR const struct routine_settings_s *settings);

#endif /* __ROUTINE_CONFIG_H */
