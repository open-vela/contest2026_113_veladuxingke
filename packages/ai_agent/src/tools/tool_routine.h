/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Update one of the three persistent routine-manager schedule slots.
 * Input: {"schedule_id":0,"hour":15,"minute":30,"text":"休息"}
 * Optional expected_text prevents an update against a stale slot.
 */
int tool_routine_schedule_update_execute(const char *input_json,
    char *output, size_t output_size);

#ifdef __cplusplus
}
#endif
