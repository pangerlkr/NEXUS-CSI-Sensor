/**
 * @file config.h
 * @brief Persistent, runtime-editable device configuration.
 *
 * The configuration is stored as a single versioned blob in NVS. On boot it is
 * loaded into a RAM copy guarded by a mutex; readers take a snapshot and
 * writers validate + persist. Compile-time defaults come from app_config.h.
 */
#ifndef NEXUS_CONFIG_H
#define NEXUS_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "app_config.h"
#include "json.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bumped whenever the on-flash layout of @ref nexus_config_t changes. */
#define NEXUS_CONFIG_MAGIC   0x4E455831u   /* "NEX1" */
#define NEXUS_CONFIG_VERSION 1u

/**
 * @brief Full device configuration. Persisted verbatim as an NVS blob;
 *        keep it POD (no pointers) so it can be memcpy'd safely.
 */
typedef struct {
    uint32_t magic;                       /**< NEXUS_CONFIG_MAGIC */
    uint32_t version;                     /**< NEXUS_CONFIG_VERSION */

    /* Identity / network */
    char     device_name[32];
    char     wifi_ssid[33];
    char     wifi_pass[65];

    /* Sensing */
    float    motion_threshold;            /**< normalised score 0..1 */
    float    presence_threshold;          /**< normalised score 0..1 */
    float    high_motion_threshold;       /**< normalised score 0..1 */
    uint16_t sampling_rate_hz;            /**< traffic-generation rate */
    bool     auto_calibration;

    /* Display */
    uint8_t  display_brightness;          /**< 0..100 % */

    /* Security */
    char     admin_user[32];
    char     admin_pass_hash[65];         /**< SHA-256(salt + password), hex */
    char     admin_salt[NEXUS_SALT_HEX_LEN + 1];

    uint32_t crc;                         /**< CRC32 of all preceding bytes */
} nexus_config_t;

/**
 * @brief Load configuration from NVS, or synthesise defaults on first boot /
 *        corruption. Must be called after storage_init().
 * @return ESP_OK always (falls back to defaults on error).
 */
esp_err_t config_init(void);

/** Copy the current configuration into @p out (thread-safe). */
void config_get(nexus_config_t *out);

/**
 * @brief Validate and persist @p in as the new configuration.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if validation failed.
 */
esp_err_t config_set(const nexus_config_t *in);

/** Populate @p out with compile-time factory defaults (not persisted). */
void config_defaults(nexus_config_t *out);

/** Reset to factory defaults and persist. */
esp_err_t config_factory_reset(void);

/**
 * @brief Set the admin password: generates a new salt and stores the hash.
 * @return ESP_OK on success.
 */
esp_err_t config_set_password(const char *plaintext);

/**
 * @brief Clamp/normalise every field of @p c to its valid range.
 * @return true if the input was already valid, false if anything was clamped.
 */
bool config_validate(nexus_config_t *c);

/** Serialise the (public) configuration into @p b. Secrets are never emitted. */
void config_to_json(const nexus_config_t *c, json_builder_t *b);

/**
 * @brief Apply a partial update from a parsed JSON document. Only present keys
 *        are changed; unknown keys are ignored. Does not persist.
 * @return true if at least one field was updated.
 */
bool config_apply_json(nexus_config_t *c, const json_doc_t *doc);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_CONFIG_H */
