/**
 * @file config.c
 * @brief Persistent configuration implementation.
 */
#include "config.h"
#include "storage.h"
#include "utils.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_mac.h"

static const char *TAG = "config";

static nexus_config_t   s_cfg;
static SemaphoreHandle_t s_lock;

/* ------------------------------------------------------------------ */
/* CRC32 (IEEE 802.3) for blob integrity                              */
/* ------------------------------------------------------------------ */
static uint32_t crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static uint32_t config_compute_crc(const nexus_config_t *c)
{
    /* CRC covers everything except the trailing crc field itself. */
    size_t covered = offsetof(nexus_config_t, crc);
    return crc32((const uint8_t *)c, covered);
}

/* ------------------------------------------------------------------ */
/* Defaults                                                           */
/* ------------------------------------------------------------------ */
void config_defaults(nexus_config_t *out)
{
    memset(out, 0, sizeof(*out));
    out->magic   = NEXUS_CONFIG_MAGIC;
    out->version = NEXUS_CONFIG_VERSION;

    /* Append the last two MAC octets to the default device name so multiple
     * units on one network are distinguishable out of the box. */
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        /* leave zeros */
    }
    snprintf(out->device_name, sizeof(out->device_name), "%s-%02x%02x",
             NEXUS_DEFAULT_DEVICE_NAME, mac[4], mac[5]);

    utils_strlcpy(out->wifi_ssid, NEXUS_DEFAULT_WIFI_SSID, sizeof(out->wifi_ssid));
    utils_strlcpy(out->wifi_pass, NEXUS_DEFAULT_WIFI_PASS, sizeof(out->wifi_pass));

    out->motion_threshold      = NEXUS_MOTION_THRESHOLD;
    out->presence_threshold    = NEXUS_PRESENCE_THRESHOLD;
    out->high_motion_threshold = NEXUS_HIGH_MOTION_THRESHOLD;
    out->sampling_rate_hz      = NEXUS_SAMPLING_RATE_DEFAULT;
    out->auto_calibration      = true;
    out->display_brightness    = NEXUS_DEFAULT_BRIGHTNESS;

    utils_strlcpy(out->admin_user, NEXUS_DEFAULT_ADMIN_USER, sizeof(out->admin_user));

    /* Hash the default password with a fresh random salt. */
    utils_random_hex(out->admin_salt, NEXUS_SALT_HEX_LEN);
    char salted[128];
    snprintf(salted, sizeof(salted), "%s%s", out->admin_salt, NEXUS_DEFAULT_ADMIN_PASS);
    utils_sha256_hex(salted, strlen(salted), out->admin_pass_hash);

    out->crc = config_compute_crc(out);
}

/* ------------------------------------------------------------------ */
/* Validation                                                         */
/* ------------------------------------------------------------------ */
bool config_validate(nexus_config_t *c)
{
    bool ok = true;

    if (c->device_name[0] == '\0') {
        utils_strlcpy(c->device_name, NEXUS_DEFAULT_DEVICE_NAME, sizeof(c->device_name));
        ok = false;
    }

    float pt = utils_clampf(c->presence_threshold, 0.01f, 0.95f);
    float mt = utils_clampf(c->motion_threshold, 0.02f, 0.98f);
    float ht = utils_clampf(c->high_motion_threshold, 0.05f, 0.99f);
    if (pt != c->presence_threshold || mt != c->motion_threshold ||
        ht != c->high_motion_threshold) {
        ok = false;
    }
    /* Enforce monotonic ordering presence < motion < high_motion. */
    if (!(pt < mt)) { mt = pt + 0.05f; ok = false; }
    if (!(mt < ht)) { ht = mt + 0.05f; ok = false; }
    c->presence_threshold    = utils_clampf(pt, 0.01f, 0.95f);
    c->motion_threshold      = utils_clampf(mt, 0.02f, 0.98f);
    c->high_motion_threshold = utils_clampf(ht, 0.05f, 0.99f);

    uint16_t sr = (uint16_t)utils_clampi(c->sampling_rate_hz,
                                         NEXUS_SAMPLING_RATE_MIN,
                                         NEXUS_SAMPLING_RATE_MAX);
    if (sr != c->sampling_rate_hz) {
        c->sampling_rate_hz = sr;
        ok = false;
    }

    uint8_t br = (uint8_t)utils_clampi(c->display_brightness, 0, 100);
    if (br != c->display_brightness) {
        c->display_brightness = br;
        ok = false;
    }

    if (c->admin_user[0] == '\0') {
        utils_strlcpy(c->admin_user, NEXUS_DEFAULT_ADMIN_USER, sizeof(c->admin_user));
        ok = false;
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* Persistence                                                        */
/* ------------------------------------------------------------------ */
static esp_err_t config_persist_locked(void)
{
    s_cfg.magic   = NEXUS_CONFIG_MAGIC;
    s_cfg.version = NEXUS_CONFIG_VERSION;
    s_cfg.crc     = config_compute_crc(&s_cfg);
    return storage_set_blob(NEXUS_NVS_KEY_CONFIG, &s_cfg, sizeof(s_cfg));
}

esp_err_t config_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    nexus_config_t loaded;
    size_t len = sizeof(loaded);
    esp_err_t err = storage_get_blob(NEXUS_NVS_KEY_CONFIG, &loaded, &len);

    bool valid = (err == ESP_OK) &&
                 (len == sizeof(loaded)) &&
                 (loaded.magic == NEXUS_CONFIG_MAGIC) &&
                 (loaded.version == NEXUS_CONFIG_VERSION) &&
                 (loaded.crc == config_compute_crc(&loaded));

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (valid) {
        s_cfg = loaded;
        config_validate(&s_cfg);
        ESP_LOGI(TAG, "loaded config: device='%s' ssid='%s'",
                 s_cfg.device_name, s_cfg.wifi_ssid);
    } else {
        ESP_LOGW(TAG, "no valid config (err=%s), using defaults", esp_err_to_name(err));
        config_defaults(&s_cfg);
        config_persist_locked();
    }
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

void config_get(nexus_config_t *out)
{
    if (out == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_cfg;
    xSemaphoreGive(s_lock);
}

esp_err_t config_set(const nexus_config_t *in)
{
    if (in == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nexus_config_t tmp = *in;
    config_validate(&tmp);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* Preserve credential fields - they are managed via config_set_password
     * and are never overwritten by a plain config_set (which comes from the
     * public settings form). */
    memcpy(tmp.admin_pass_hash, s_cfg.admin_pass_hash, sizeof(tmp.admin_pass_hash));
    memcpy(tmp.admin_salt, s_cfg.admin_salt, sizeof(tmp.admin_salt));
    s_cfg = tmp;
    esp_err_t err = config_persist_locked();
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t config_factory_reset(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    config_defaults(&s_cfg);
    esp_err_t err = config_persist_locked();
    xSemaphoreGive(s_lock);
    ESP_LOGW(TAG, "factory reset applied");
    return err;
}

esp_err_t config_set_password(const char *plaintext)
{
    if (plaintext == NULL || plaintext[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    char salt[NEXUS_SALT_HEX_LEN + 1];
    utils_random_hex(salt, NEXUS_SALT_HEX_LEN);

    char salted[192];
    snprintf(salted, sizeof(salted), "%s%s", salt, plaintext);
    char hash[65];
    if (!utils_sha256_hex(salted, strlen(salted), hash)) {
        return ESP_FAIL;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    utils_strlcpy(s_cfg.admin_salt, salt, sizeof(s_cfg.admin_salt));
    utils_strlcpy(s_cfg.admin_pass_hash, hash, sizeof(s_cfg.admin_pass_hash));
    esp_err_t err = config_persist_locked();
    xSemaphoreGive(s_lock);
    return err;
}

/* ------------------------------------------------------------------ */
/* JSON                                                               */
/* ------------------------------------------------------------------ */
void config_to_json(const nexus_config_t *c, json_builder_t *b)
{
    json_obj_open(b);
    json_kv_str(b, "device_name", c->device_name);
    json_kv_str(b, "wifi_ssid", c->wifi_ssid);
    /* Password intentionally omitted; expose only whether one is set. */
    json_kv_bool(b, "wifi_pass_set", c->wifi_pass[0] != '\0');
    json_kv_float(b, "presence_threshold", c->presence_threshold, 3);
    json_kv_float(b, "motion_threshold", c->motion_threshold, 3);
    json_kv_float(b, "high_motion_threshold", c->high_motion_threshold, 3);
    json_kv_int(b, "sampling_rate_hz", c->sampling_rate_hz);
    json_kv_bool(b, "auto_calibration", c->auto_calibration);
    json_kv_int(b, "display_brightness", c->display_brightness);
    json_kv_str(b, "admin_user", c->admin_user);
    json_obj_close(b);
}

bool config_apply_json(nexus_config_t *c, const json_doc_t *doc)
{
    bool changed = false;
    char sbuf[65];
    int ival;
    float fval;
    bool bval;

    if (json_get_str(doc, "device_name", sbuf, sizeof(sbuf))) {
        utils_strlcpy(c->device_name, sbuf, sizeof(c->device_name));
        changed = true;
    }
    if (json_get_str(doc, "wifi_ssid", sbuf, sizeof(sbuf))) {
        utils_strlcpy(c->wifi_ssid, sbuf, sizeof(c->wifi_ssid));
        changed = true;
    }
    if (json_get_str(doc, "wifi_pass", sbuf, sizeof(sbuf))) {
        /* Only overwrite if a non-empty password was supplied. */
        if (sbuf[0] != '\0') {
            utils_strlcpy(c->wifi_pass, sbuf, sizeof(c->wifi_pass));
            changed = true;
        }
    }
    if (json_get_float(doc, "presence_threshold", &fval)) {
        c->presence_threshold = fval; changed = true;
    }
    if (json_get_float(doc, "motion_threshold", &fval)) {
        c->motion_threshold = fval; changed = true;
    }
    if (json_get_float(doc, "high_motion_threshold", &fval)) {
        c->high_motion_threshold = fval; changed = true;
    }
    if (json_get_int(doc, "sampling_rate_hz", &ival)) {
        c->sampling_rate_hz = (uint16_t)ival; changed = true;
    }
    if (json_get_bool(doc, "auto_calibration", &bval)) {
        c->auto_calibration = bval; changed = true;
    }
    if (json_get_int(doc, "display_brightness", &ival)) {
        c->display_brightness = (uint8_t)ival; changed = true;
    }
    return changed;
}
