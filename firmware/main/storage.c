/**
 * @file storage.c
 * @brief NVS wrapper implementation.
 */
#include "storage.h"
#include "app_config.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "storage";

esp_err_t storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (%s), reinitialising", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t open_rw(nvs_handle_t *h)
{
    return nvs_open(NEXUS_NVS_NAMESPACE, NVS_READWRITE, h);
}

static esp_err_t open_ro(nvs_handle_t *h)
{
    return nvs_open(NEXUS_NVS_NAMESPACE, NVS_READONLY, h);
}

esp_err_t storage_set_blob(const char *key, const void *data, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = open_rw(&h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, key, data, len);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_blob(%s) failed: %s", key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t storage_get_blob(const char *key, void *out, size_t *len)
{
    nvs_handle_t h;
    esp_err_t err = open_ro(&h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_blob(h, key, out, len);
    nvs_close(h);
    return err;
}

size_t storage_blob_size(const char *key)
{
    nvs_handle_t h;
    if (open_ro(&h) != ESP_OK) {
        return 0;
    }
    size_t sz = 0;
    esp_err_t err = nvs_get_blob(h, key, NULL, &sz);
    nvs_close(h);
    return (err == ESP_OK) ? sz : 0;
}

esp_err_t storage_set_str(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = open_rw(&h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t storage_get_str(const char *key, char *out, size_t out_sz)
{
    nvs_handle_t h;
    esp_err_t err = open_ro(&h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_str(h, key, out, &out_sz);
    nvs_close(h);
    return err;
}

esp_err_t storage_set_u32(const char *key, uint32_t value)
{
    nvs_handle_t h;
    esp_err_t err = open_rw(&h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u32(h, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t storage_get_u32(const char *key, uint32_t *out)
{
    nvs_handle_t h;
    esp_err_t err = open_ro(&h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_u32(h, key, out);
    nvs_close(h);
    return err;
}

esp_err_t storage_erase_key(const char *key)
{
    nvs_handle_t h;
    esp_err_t err = open_rw(&h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t storage_erase_all(void)
{
    nvs_handle_t h;
    esp_err_t err = open_rw(&h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    ESP_LOGW(TAG, "namespace erased (factory reset): %s", esp_err_to_name(err));
    return err;
}
