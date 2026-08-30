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

#include "tools/tool_routine.h"
#include "agent_compat.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"

#define ROUTINE_CONFIG_PATH "/data/routine/config.json"
#define ROUTINE_CONFIG_TMP_PATH "/data/routine/config.json.ai-agent.tmp"
#define ROUTINE_CONFIG_DIR "/data/routine"
#define ROUTINE_CONFIG_MAX_BYTES 2048
#define ROUTINE_SCHEDULE_MAX 3
#define ROUTINE_SCHEDULE_TEXT_MAX 48
#define ROUTINE_DEFAULT_HISTORY_INTERVAL 60
#define ROUTINE_DEFAULT_TEMPERATURE_LOW_X100 1600
#define ROUTINE_DEFAULT_TEMPERATURE_HIGH_X100 3000

static const char *TAG = "tool_routine";
static pthread_mutex_t s_config_lock = PTHREAD_MUTEX_INITIALIZER;

static bool routine_json_integer(const cJSON *item, int64_t minimum,
    int64_t maximum, int64_t *value)
{
    char *end;
    long long parsed;

    if (item == NULL || value == NULL)
        return false;

    if (cJSON_IsNumber(item)) {
        if (item->valuedouble != (double)item->valueint)
            return false;
        parsed = item->valueint;
    } else if (cJSON_IsString(item) && item->valuestring != NULL) {
        errno = 0;
        parsed = strtoll(item->valuestring, &end, 10);
        if (errno != 0 || end == item->valuestring || *end != '\0')
            return false;
    } else {
        return false;
    }

    if (parsed < minimum || parsed > maximum)
        return false;

    *value = (int64_t)parsed;
    return true;
}

static bool routine_utf8_valid(const char *text, size_t length)
{
    size_t offset = 0;

    if (text == NULL || length == 0 || length > ROUTINE_SCHEDULE_TEXT_MAX)
        return false;

    while (offset < length) {
        uint8_t first = (uint8_t)text[offset++];
        uint32_t codepoint;
        uint8_t continuation;

        if (first < 0x80) {
            if (first < 0x20 || first == 0x7f)
                return false;
            continue;
        }

        if (first >= 0xc2 && first <= 0xdf) {
            codepoint = first & 0x1f;
            continuation = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            codepoint = first & 0x0f;
            continuation = 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            codepoint = first & 0x07;
            continuation = 3;
        } else {
            return false;
        }

        if (offset + continuation > length)
            return false;

        while (continuation-- > 0) {
            uint8_t next = (uint8_t)text[offset++];
            if ((next & 0xc0) != 0x80)
                return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }

        if ((first <= 0xdf && codepoint < 0x80) ||
            (first <= 0xef && codepoint < 0x800) ||
            (first >= 0xf0 && codepoint < 0x10000) ||
            codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff))
            return false;
    }

    return true;
}

static bool routine_history_interval_valid(int64_t value)
{
    return value == 5 || value == 10 ||
           (value >= 60 && value <= 3600 && value % 60 == 0);
}

static int routine_read_config(char *data, size_t data_size, size_t *length)
{
    struct stat info;
    size_t used = 0;
    int fd;

    if (data == NULL || length == NULL || data_size < 2)
        return -EINVAL;

    if (stat(ROUTINE_CONFIG_PATH, &info) < 0)
        return -errno;

    if (!S_ISREG(info.st_mode) || info.st_size <= 0 ||
        info.st_size >= (off_t)data_size)
        return -EINVAL;

    fd = open(ROUTINE_CONFIG_PATH, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -errno;

    while (used < (size_t)info.st_size) {
        ssize_t count = read(fd, data + used, (size_t)info.st_size - used);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            int ret = count == 0 ? -EIO : -errno;
            close(fd);
            return ret;
        }
        used += (size_t)count;
    }

    close(fd);
    data[used] = '\0';
    *length = used;
    return OK;
}

static cJSON *routine_default_config(void)
{
    static const uint8_t hours[ROUTINE_SCHEDULE_MAX] = {9, 14, 18};
    static const uint8_t minutes[ROUTINE_SCHEDULE_MAX] = {0, 30, 0};
    static const char *texts[ROUTINE_SCHEDULE_MAX] = {
        "喝水", "专注", "通风"
    };
    cJSON *root = cJSON_CreateObject();
    cJSON *schedule = cJSON_CreateArray();

    if (root == NULL || schedule == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(schedule);
        return NULL;
    }

    cJSON_AddNumberToObject(root, "schema", 1);
    cJSON_AddNumberToObject(root, "generation", 0);
    cJSON_AddNumberToObject(root, "temperature_low_x100",
                            ROUTINE_DEFAULT_TEMPERATURE_LOW_X100);
    cJSON_AddNumberToObject(root, "temperature_high_x100",
                            ROUTINE_DEFAULT_TEMPERATURE_HIGH_X100);
    cJSON_AddNumberToObject(root, "history_interval_seconds",
                            ROUTINE_DEFAULT_HISTORY_INTERVAL);
    cJSON_AddItemToObject(root, "schedule", schedule);

    for (int i = 0; i < ROUTINE_SCHEDULE_MAX; i++) {
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddNumberToObject(item, "id", i);
        cJSON_AddNumberToObject(item, "hour", hours[i]);
        cJSON_AddNumberToObject(item, "minute", minutes[i]);
        cJSON_AddStringToObject(item, "text", texts[i]);
        cJSON_AddItemToArray(schedule, item);
    }

    return root;
}

static cJSON *routine_parse_config(const char *data, size_t length)
{
    const char *end = NULL;
    cJSON *root;
    int64_t number;
    int64_t generation;
    int64_t low;
    int64_t high;
    int64_t history;
    bool ids[ROUTINE_SCHEDULE_MAX] = {false};
    cJSON *schedule;
    cJSON *item;

    root = cJSON_ParseWithLengthOpts(data, length, &end, false);
    if (root == NULL || !cJSON_IsObject(root) || end == NULL)
        goto invalid;

    while (end < data + length &&
           (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        end++;
    if (end != data + length)
        goto invalid;

    if (!routine_json_integer(cJSON_GetObjectItemCaseSensitive(root, "schema"),
                              1, 1, &number) ||
        !routine_json_integer(cJSON_GetObjectItemCaseSensitive(root,
                              "generation"), 0, INT_MAX, &generation) ||
        !routine_json_integer(cJSON_GetObjectItemCaseSensitive(root,
                              "temperature_low_x100"), -4000, 12400, &low) ||
        !routine_json_integer(cJSON_GetObjectItemCaseSensitive(root,
                              "temperature_high_x100"), -3900, 12500, &high) ||
        high - low < 100)
        goto invalid;

    history = ROUTINE_DEFAULT_HISTORY_INTERVAL;
    if (cJSON_GetObjectItemCaseSensitive(root, "history_interval_seconds") !=
        NULL &&
        (!routine_json_integer(cJSON_GetObjectItemCaseSensitive(root,
                               "history_interval_seconds"), 5, 3600,
                               &history) ||
         !routine_history_interval_valid(history)))
        goto invalid;

    schedule = cJSON_GetObjectItemCaseSensitive(root, "schedule");
    if (!cJSON_IsArray(schedule) ||
        cJSON_GetArraySize(schedule) != ROUTINE_SCHEDULE_MAX)
        goto invalid;

    cJSON_ArrayForEach(item, schedule) {
        cJSON *text = cJSON_GetObjectItemCaseSensitive(item, "text");
        int64_t id;
        int64_t hour;
        int64_t minute;
        size_t text_length;

        if (!cJSON_IsObject(item) ||
            !routine_json_integer(cJSON_GetObjectItemCaseSensitive(item, "id"),
                                  0, ROUTINE_SCHEDULE_MAX - 1, &id) ||
            ids[id] ||
            !routine_json_integer(cJSON_GetObjectItemCaseSensitive(item,
                                  "hour"), 0, 23, &hour) ||
            !routine_json_integer(cJSON_GetObjectItemCaseSensitive(item,
                                  "minute"), 0, 59, &minute) ||
            !cJSON_IsString(text) || text->valuestring == NULL)
            goto invalid;

        text_length = strlen(text->valuestring);
        if (!routine_utf8_valid(text->valuestring, text_length))
            goto invalid;
        ids[id] = true;
    }

    for (int i = 0; i < ROUTINE_SCHEDULE_MAX; i++) {
        if (!ids[i])
            goto invalid;
    }

    return root;

invalid:
    cJSON_Delete(root);
    return NULL;
}

static int routine_write_all(int fd, const char *data, size_t length)
{
    while (length > 0) {
        ssize_t written = write(fd, data, length);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return written == 0 ? -EIO : -errno;
        data += written;
        length -= (size_t)written;
    }
    return OK;
}

static int routine_write_config(cJSON *root)
{
    char *rendered;
    size_t length;
    int fd;
    int ret;

    rendered = cJSON_PrintUnformatted(root);
    if (rendered == NULL)
        return -ENOMEM;

    length = strlen(rendered);
    if (length == 0 || length >= ROUTINE_CONFIG_MAX_BYTES) {
        free(rendered);
        return -E2BIG;
    }

    if (mkdir(ROUTINE_CONFIG_DIR, 0700) < 0 && errno != EEXIST) {
        ret = -errno;
        free(rendered);
        return ret;
    }

    fd = open(ROUTINE_CONFIG_TMP_PATH,
              O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        ret = -errno;
        free(rendered);
        return ret;
    }

    ret = routine_write_all(fd, rendered, length);
    if (ret == OK && fsync(fd) < 0)
        ret = -errno;
    if (close(fd) < 0 && ret == OK)
        ret = -errno;
    if (ret == OK && rename(ROUTINE_CONFIG_TMP_PATH, ROUTINE_CONFIG_PATH) < 0)
        ret = -errno;
    if (ret < 0)
        unlink(ROUTINE_CONFIG_TMP_PATH);

    free(rendered);
    return ret;
}

static int routine_result(cJSON *result, char *output, size_t output_size)
{
    char *rendered;
    size_t length;

    if (result == NULL || output == NULL || output_size == 0)
        return ERROR;

    rendered = cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    if (rendered == NULL) {
        snprintf(output, output_size, "{\"ok\":false,\"error\":\"OOM\"}");
        return ERROR;
    }
    length = strlen(rendered);
    if (length >= output_size) {
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"result buffer too small\"}");
        free(rendered);
        return ERROR;
    }
    strlcpy(output, rendered, output_size);
    free(rendered);
    return OK;
}

int tool_routine_schedule_update_execute(const char *input_json,
    char *output, size_t output_size)
{
    cJSON *input = NULL;
    cJSON *root = NULL;
    cJSON *schedule;
    cJSON *target = NULL;
    cJSON *item;
    cJSON *result = NULL;
    const char *text;
    const char *expected_text;
    const char *old_text;
    char data[ROUTINE_CONFIG_MAX_BYTES + 1];
    size_t data_length = 0;
    int64_t schedule_id;
    int64_t hour;
    int64_t minute;
    int ret;

    if (output == NULL || output_size == 0)
        return ERROR;
    output[0] = '\0';

    input = input_json == NULL ? NULL : cJSON_Parse(input_json);
    text = input == NULL ? NULL : cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(input, "text"));
    expected_text = input == NULL ? NULL : cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(input, "expected_text"));

    if (input == NULL ||
        !routine_json_integer(cJSON_GetObjectItemCaseSensitive(input,
                               "schedule_id"), 0,
                               ROUTINE_SCHEDULE_MAX - 1, &schedule_id) ||
        !routine_json_integer(cJSON_GetObjectItemCaseSensitive(input, "hour"),
                              0, 23, &hour) ||
        !routine_json_integer(cJSON_GetObjectItemCaseSensitive(input,
                              "minute"), 0, 59, &minute) ||
        text == NULL || !routine_utf8_valid(text, strlen(text)) ||
        (expected_text != NULL &&
         !routine_utf8_valid(expected_text, strlen(expected_text)))) {
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"schedule_id 0-2, hour 0-23, minute 0-59, and non-empty UTF-8 text are required\"}");
        cJSON_Delete(input);
        return ERROR;
    }

    pthread_mutex_lock(&s_config_lock);
    ret = routine_read_config(data, sizeof(data), &data_length);
    if (ret == -ENOENT) {
        root = routine_default_config();
    } else if (ret == OK) {
        root = routine_parse_config(data, data_length);
    }

    if (root == NULL) {
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"existing routine config is unavailable or invalid; unchanged\"}");
        pthread_mutex_unlock(&s_config_lock);
        cJSON_Delete(input);
        return ERROR;
    }

    schedule = cJSON_GetObjectItemCaseSensitive(root, "schedule");
    cJSON_ArrayForEach(item, schedule) {
        int64_t id;
        if (routine_json_integer(cJSON_GetObjectItemCaseSensitive(item, "id"),
                                 0, ROUTINE_SCHEDULE_MAX - 1, &id) &&
            id == schedule_id) {
            target = item;
            break;
        }
    }

    old_text = target == NULL ? NULL : cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(target, "text"));
    if (old_text == NULL ||
        (expected_text != NULL && strcmp(old_text, expected_text) != 0)) {
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"schedule slot not found or expected_text did not match; unchanged\"}");
        cJSON_Delete(root);
        pthread_mutex_unlock(&s_config_lock);
        cJSON_Delete(input);
        return ERROR;
    }

    if (!cJSON_ReplaceItemInObjectCaseSensitive(target, "hour",
                                                 cJSON_CreateNumber((double)hour)) ||
        !cJSON_ReplaceItemInObjectCaseSensitive(target, "minute",
                                                 cJSON_CreateNumber((double)minute)) ||
        !cJSON_ReplaceItemInObjectCaseSensitive(target, "text",
                                                 cJSON_CreateString(text))) {
        snprintf(output, output_size,
                 "{\"ok\":false,\"error\":\"cannot allocate routine update; unchanged\"}");
        cJSON_Delete(root);
        pthread_mutex_unlock(&s_config_lock);
        cJSON_Delete(input);
        return ERROR;
    }

    {
        cJSON *generation = cJSON_GetObjectItemCaseSensitive(root, "generation");
        int64_t value;
        uint32_t next;
        if (!routine_json_integer(generation, 0, UINT32_MAX, &value)) {
            snprintf(output, output_size,
                     "{\"ok\":false,\"error\":\"invalid routine generation; unchanged\"}");
            cJSON_Delete(root);
            pthread_mutex_unlock(&s_config_lock);
            cJSON_Delete(input);
            return ERROR;
        }
        next = value == INT_MAX ? 1 : (uint32_t)value + 1;
        if (!cJSON_ReplaceItemInObjectCaseSensitive(root, "generation",
                                                     cJSON_CreateNumber((double)next))) {
            snprintf(output, output_size,
                     "{\"ok\":false,\"error\":\"cannot update routine generation; unchanged\"}");
            cJSON_Delete(root);
            pthread_mutex_unlock(&s_config_lock);
            cJSON_Delete(input);
            return ERROR;
        }

        /* Build the response before committing the file.  If memory is
         * exhausted, report an unchanged configuration rather than writing
         * successfully and then returning a misleading error. */
        result = cJSON_CreateObject();
        if (result == NULL ||
            cJSON_AddBoolToObject(result, "ok", true) == NULL ||
            cJSON_AddNumberToObject(result, "schedule_id", schedule_id) == NULL ||
            cJSON_AddNumberToObject(result, "hour", hour) == NULL ||
            cJSON_AddNumberToObject(result, "minute", minute) == NULL ||
            cJSON_AddStringToObject(result, "text", text) == NULL ||
            cJSON_AddNumberToObject(result, "generation", next) == NULL ||
            cJSON_AddBoolToObject(result, "persistent_daily", true) == NULL ||
            cJSON_AddStringToObject(result, "note",
                                    "routine_mgr applies the new template on its next refresh") == NULL) {
            cJSON_Delete(result);
            result = NULL;
            snprintf(output, output_size,
                     "{\"ok\":false,\"error\":\"cannot allocate routine result; unchanged\"}");
            cJSON_Delete(root);
            pthread_mutex_unlock(&s_config_lock);
            cJSON_Delete(input);
            return ERROR;
        }

        ret = routine_write_config(root);
        if (ret != OK) {
            snprintf(output, output_size,
                     "{\"ok\":false,\"error\":\"routine config write failed\",\"errno\":%d}",
                     -ret);
            cJSON_Delete(result);
            result = NULL;
            cJSON_Delete(root);
            pthread_mutex_unlock(&s_config_lock);
            cJSON_Delete(input);
            return ERROR;
        }
    }

    syslog(LOG_INFO, "[%s] Updated schedule id=%ld to %02ld:%02ld text=%s\n",
           TAG, (long)schedule_id, (long)hour, (long)minute, text);
    cJSON_Delete(root);
    pthread_mutex_unlock(&s_config_lock);
    cJSON_Delete(input);

    return routine_result(result, output, output_size);
}
