/**
 * @file logger.c
 * @brief Event logger implementation.
 */
#include "logger.h"
#include "ringbuffer.h"
#include "storage.h"
#include "utils.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "logger";

static ringbuffer_t     s_ring;
static SemaphoreHandle_t s_lock;
static uint32_t          s_seq;
static volatile bool     s_dirty;
static bool              s_ready;

/* Persist cadence: flush to NVS at most this often when dirty. */
#define LOGGER_PERSIST_INTERVAL_MS 30000

esp_err_t logger_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = ringbuffer_init(&s_ring, sizeof(log_entry_t), NEXUS_LOG_CAPACITY);
    if (err != ESP_OK) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return err;
    }

    /* Restore persisted entries if the blob size is an exact multiple. */
    size_t blob_sz = storage_blob_size(NEXUS_NVS_KEY_LOGS);
    if (blob_sz > 0 && (blob_sz % sizeof(log_entry_t)) == 0) {
        size_t n = blob_sz / sizeof(log_entry_t);
        if (n > NEXUS_LOG_CAPACITY) {
            n = NEXUS_LOG_CAPACITY;
        }
        log_entry_t *tmp = (log_entry_t *)calloc(n, sizeof(log_entry_t));
        if (tmp) {
            /* nvs_get_blob treats *rd as the capacity of the caller's buffer, so
             * this must describe tmp, not the stored blob. Passing blob_sz here
             * would let NVS write past the end of tmp whenever n was clamped. */
            size_t rd = n * sizeof(log_entry_t);
            if (storage_get_blob(NEXUS_NVS_KEY_LOGS, tmp, &rd) == ESP_OK) {
                size_t got = rd / sizeof(log_entry_t);
                for (size_t i = 0; i < got && i < n; ++i) {
                    ringbuffer_push(&s_ring, &tmp[i]);
                    if (tmp[i].seq >= s_seq) {
                        s_seq = tmp[i].seq + 1;
                    }
                }
                ESP_LOGI(TAG, "restored %u log entries", (unsigned)got);
            }
            free(tmp);
        }
    }
    s_ready = true;
    return ESP_OK;
}

const char *logger_level_str(log_level_t level)
{
    switch (level) {
        case LOG_LVL_INFO:  return "INFO";
        case LOG_LVL_EVENT: return "EVENT";
        case LOG_LVL_WARN:  return "WARN";
        case LOG_LVL_ERROR: return "ERROR";
        default:            return "?";
    }
}

void logger_log(log_level_t level, const char *fmt, ...)
{
    if (!s_ready) {
        return;
    }
    log_entry_t e;
    memset(&e, 0, sizeof(e));
    e.uptime_ms = utils_millis();
    e.level = (uint8_t)level;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e.msg, sizeof(e.msg), fmt, ap);
    va_end(ap);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    e.seq = s_seq++;
    ringbuffer_push(&s_ring, &e);
    s_dirty = true;
    xSemaphoreGive(s_lock);

    /* Mirror to the console for developers. */
    switch (level) {
        case LOG_LVL_ERROR: ESP_LOGE(TAG, "%s", e.msg); break;
        case LOG_LVL_WARN:  ESP_LOGW(TAG, "%s", e.msg); break;
        default:            ESP_LOGI(TAG, "%s", e.msg); break;
    }
}

size_t logger_snapshot(log_entry_t *out, size_t max)
{
    return ringbuffer_snapshot(&s_ring, out, max);
}

size_t logger_count(void)
{
    return ringbuffer_count(&s_ring);
}

bool logger_peek(size_t index, log_entry_t *out)
{
    return ringbuffer_peek(&s_ring, index, out);
}

void logger_to_json(json_builder_t *b)
{
    /* One entry at a time rather than a copy of the whole ring. At 64 entries of
     * ~108 bytes that copy was a ~6.9 KB allocation on an HTTP path, and when it
     * failed the response was a silently empty log list. Walking the ring costs
     * one lock per entry and a single struct of stack. */
    size_t n = logger_count();

    json_arr_open(b, "logs");
    /* Emit newest first for display convenience. */
    uint32_t prev_seq = 0;
    bool have_prev = false;
    for (size_t i = n; i > 0; --i) {
        log_entry_t e;
        if (!logger_peek(i - 1, &e)) {
            continue;
        }
        /* Entries logged while this loop runs shift every index down by one, so
         * without this the same entry could be emitted twice. Sequence numbers
         * only ever rise, so anything not below the last one emitted is a repeat.
         */
        if (have_prev && e.seq >= prev_seq) {
            continue;
        }
        prev_seq = e.seq;
        have_prev = true;

        json_elem_obj_open(b);
        json_kv_uint(b, "seq", e.seq);
        json_kv_uint(b, "t", e.uptime_ms);
        json_kv_str(b, "level", logger_level_str((log_level_t)e.level));
        json_kv_str(b, "msg", e.msg);
        json_obj_close(b);
    }
    json_arr_close(b);
}

esp_err_t logger_persist(void)
{
    /* The one place that still needs the entries contiguous: nvs_set_blob takes a
     * single pointer and length, so there is nothing to stream into. Allocate for
     * what is actually held instead of the full capacity. Anything logged between
     * the count and the snapshot is simply picked up by the next persist. */
    size_t n = logger_count();
    if (n == 0) {
        s_dirty = false;
        return ESP_OK;
    }
    log_entry_t *tmp = (log_entry_t *)calloc(n, sizeof(log_entry_t));
    if (tmp == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t got = logger_snapshot(tmp, n);
    esp_err_t err = ESP_OK;
    if (got > 0) {
        err = storage_set_blob(NEXUS_NVS_KEY_LOGS, tmp, got * sizeof(log_entry_t));
    }
    if (err == ESP_OK) {
        s_dirty = false;
    }
    free(tmp);
    return err;
}

void logger_clear(void)
{
    ringbuffer_clear(&s_ring);
    storage_erase_key(NEXUS_NVS_KEY_LOGS);
    s_dirty = false;
}

static void logger_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    uint32_t last_persist = utils_millis();

    for (;;) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(NEXUS_WDT_FEED_MS));

        uint32_t now = utils_millis();
        if (s_dirty && (now - last_persist) >= LOGGER_PERSIST_INTERVAL_MS) {
            if (logger_persist() == ESP_OK) {
                last_persist = now;
            }
        }
    }
}

esp_err_t logger_start_task(void)
{
    BaseType_t ok = xTaskCreate(logger_task, "nexus_logger",
                                NEXUS_TASK_STACK_LOGGER, NULL,
                                NEXUS_TASK_PRIO_LOGGER, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
