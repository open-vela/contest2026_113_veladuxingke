/****************************************************************************
 * contest2026_113_veladuxingke/app/routine_manager/routine_storage.h
 ****************************************************************************/

#ifndef __ROUTINE_STORAGE_H
#define __ROUTINE_STORAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "routine_model.h"

#define ROUTINE_STORAGE_DIR            "/data/routine"
#define ROUTINE_STORAGE_SCHEDULE_PATH  ROUTINE_STORAGE_DIR "/schedule.dat"
#define ROUTINE_STORAGE_STATUS_PATH    ROUTINE_STORAGE_DIR "/status.json"
#define ROUTINE_STORAGE_HISTORY_PATH   ROUTINE_STORAGE_DIR "/history.jsonl"
#define ROUTINE_STORAGE_EVENTS_PATH    ROUTINE_STORAGE_DIR "/events.log"
#define ROUTINE_STORAGE_EXPORT_DEVICE  "/dev/mmcsd1"
#define ROUTINE_STORAGE_EXPORT_MOUNT   "/sdcard"
#define ROUTINE_STORAGE_EXPORT_FSTYPE  "vfat"
#define ROUTINE_STORAGE_EXPORT_DIR     ROUTINE_STORAGE_EXPORT_MOUNT "/routine_history"

struct routine_storage_s
{
  uint64_t generation;
  int64_t last_history_slot;
  uint32_t last_history_interval_seconds;
  time_t schedule_mtime;
  bool history_valid;
};

int routine_storage_init(struct routine_storage_s *storage);
int routine_storage_load(struct routine_storage_s *storage,
                         struct routine_model_s *model);
int routine_storage_save(struct routine_storage_s *storage,
                         const struct routine_model_s *model);
int routine_storage_publish_status(const struct routine_storage_s *storage,
                                   const struct routine_snapshot_s *snapshot);
int routine_storage_append_history(struct routine_storage_s *storage,
                                   const struct routine_snapshot_s *snapshot,
                                   uint32_t interval_seconds);
int routine_storage_export_history(struct routine_storage_s *storage,
                                   char *path, size_t path_size);
int routine_storage_append_event(struct routine_storage_s *storage,
                                 const char *event, int result);
uint64_t routine_storage_generation(const struct routine_storage_s *storage);

#endif /* __ROUTINE_STORAGE_H */
