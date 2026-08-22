/**
 * @file storage.h
 * @brief Thin, error-checked wrapper around the ESP-IDF NVS API.
 *
 * All persistent state (device configuration, event log snapshot) is stored in
 * a single NVS namespace. This module centralises the open/commit/close dance
 * and exposes typed getters/setters plus blob helpers, so higher layers never
 * touch nvs_* directly.
 */
#ifndef NEXUS_STORAGE_H
#define NEXUS_STORAGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the NVS flash partition. Handles the
 *        "new version / no free pages" case by erasing and retrying.
 * @return ESP_OK on success.
 */
esp_err_t storage_init(void);

/** Store a variable-length blob under @p key. */
esp_err_t storage_set_blob(const char *key, const void *data, size_t len);

/**
 * @brief Read a blob under @p key into @p out.
 * @param[in,out] len On input: capacity of @p out. On output: bytes read.
 * @return ESP_OK, ESP_ERR_NVS_NOT_FOUND, or another esp_err_t.
 */
esp_err_t storage_get_blob(const char *key, void *out, size_t *len);

/** @return Size in bytes of the blob under @p key, or 0 if absent. */
size_t storage_blob_size(const char *key);

esp_err_t storage_set_str(const char *key, const char *value);
esp_err_t storage_get_str(const char *key, char *out, size_t out_sz);

esp_err_t storage_set_u32(const char *key, uint32_t value);
esp_err_t storage_get_u32(const char *key, uint32_t *out);

/** Erase a single key. Returns ESP_OK even if the key was absent. */
esp_err_t storage_erase_key(const char *key);

/** Erase the entire NEXUS namespace (factory reset of persisted data). */
esp_err_t storage_erase_all(void);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_STORAGE_H */
