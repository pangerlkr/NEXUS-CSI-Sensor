/**
 * @file ota.c
 * @brief Streamed OTA update implementation with rollback support.
 */
#include "ota.h"
#include "logger.h"
#include "utils.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_log.h"

static const char *TAG = "ota";

static SemaphoreHandle_t   s_lock;
static esp_ota_handle_t    s_handle;
static const esp_partition_t *s_target;
static ota_status_t        s_status;

esp_err_t ota_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_status, 0, sizeof(s_status));

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (running && esp_ota_get_state_partition(running, &st) == ESP_OK) {
        if (st == ESP_OTA_IMG_PENDING_VERIFY) {
            LOG_WARN("Running a freshly-updated image (pending verify)");
        }
        ESP_LOGI(TAG, "running from '%s'", running->label);
    }
    utils_strlcpy(s_status.message, "idle", sizeof(s_status.message));
    return ESP_OK;
}

const char *ota_running_partition(void)
{
    const esp_partition_t *r = esp_ota_get_running_partition();
    return r ? r->label : "?";
}

esp_err_t ota_begin(size_t total_size)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_status.in_progress) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    s_target = esp_ota_get_next_update_partition(NULL);
    if (s_target == NULL) {
        utils_strlcpy(s_status.message, "no OTA partition", sizeof(s_status.message));
        xSemaphoreGive(s_lock);
        LOG_ERR("OTA: no target partition");
        return ESP_ERR_NOT_FOUND;
    }

    /* Reject an oversized image before esp_ota_begin() erases the partition.
     * Without this the flash is wiped first and the write only fails part-way
     * through, so a single bad upload destroys the rollback copy for nothing. */
    if (total_size > 0 && total_size > s_target->size) {
        snprintf(s_status.message, sizeof(s_status.message),
                 "image too large (%u > %u)",
                 (unsigned)total_size, (unsigned)s_target->size);
        xSemaphoreGive(s_lock);
        LOG_ERR("OTA rejected: %u bytes exceeds '%s' (%u bytes)",
                (unsigned)total_size, s_target->label, (unsigned)s_target->size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* OTA_SIZE_UNKNOWN erases the whole partition, which is what we want: the
     * declared length is a hint from the client, not something to trust as the
     * exact extent of the write. */
    esp_err_t err = esp_ota_begin(s_target, OTA_SIZE_UNKNOWN, &s_handle);
    if (err != ESP_OK) {
        snprintf(s_status.message, sizeof(s_status.message),
                 "begin failed: %s", esp_err_to_name(err));
        xSemaphoreGive(s_lock);
        LOG_ERR("OTA begin failed: %s", esp_err_to_name(err));
        return err;
    }

    s_status.in_progress = true;
    s_status.last_ok = false;
    s_status.received = 0;
    s_status.total = total_size;
    s_status.percent = 0;
    utils_strlcpy(s_status.message, "receiving", sizeof(s_status.message));
    xSemaphoreGive(s_lock);

    LOG_EVENT("OTA started -> '%s' (%u bytes expected)",
              s_target->label, (unsigned)total_size);
    return ESP_OK;
}

esp_err_t ota_write(const void *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_status.in_progress) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_ota_write(s_handle, data, len);
    if (err == ESP_OK) {
        s_status.received += len;
        if (s_status.total > 0) {
            s_status.percent = (int)((uint64_t)s_status.received * 100U / s_status.total);
            if (s_status.percent > 100) s_status.percent = 100;
        }
    } else {
        snprintf(s_status.message, sizeof(s_status.message),
                 "write failed: %s", esp_err_to_name(err));
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t ota_end(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_status.in_progress) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_ota_end(s_handle);
    if (err != ESP_OK) {
        snprintf(s_status.message, sizeof(s_status.message),
                 "image invalid: %s", esp_err_to_name(err));
        s_status.in_progress = false;
        s_status.last_ok = false;
        xSemaphoreGive(s_lock);
        LOG_ERR("OTA end/validate failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ota_set_boot_partition(s_target);
    if (err != ESP_OK) {
        snprintf(s_status.message, sizeof(s_status.message),
                 "set boot failed: %s", esp_err_to_name(err));
        s_status.in_progress = false;
        s_status.last_ok = false;
        xSemaphoreGive(s_lock);
        LOG_ERR("OTA set_boot_partition failed: %s", esp_err_to_name(err));
        return err;
    }

    s_status.in_progress = false;
    s_status.last_ok = true;
    s_status.percent = 100;
    utils_strlcpy(s_status.message, "complete, rebooting", sizeof(s_status.message));
    xSemaphoreGive(s_lock);

    LOG_EVENT("OTA complete -> booting '%s'", s_target->label);
    return ESP_OK;
}

void ota_abort(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_status.in_progress) {
        esp_ota_abort(s_handle);
        s_status.in_progress = false;
        s_status.last_ok = false;
        utils_strlcpy(s_status.message, "aborted", sizeof(s_status.message));
        LOG_WARN("OTA aborted");
    }
    xSemaphoreGive(s_lock);
}

void ota_get_status(ota_status_t *out)
{
    if (out == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
}

esp_err_t ota_mark_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (running && esp_ota_get_state_partition(running, &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            LOG_EVENT("Image confirmed healthy (rollback cancelled)");
        } else {
            LOG_ERR("mark_app_valid failed: %s", esp_err_to_name(err));
        }
        return err;
    }
    return ESP_OK;
}
