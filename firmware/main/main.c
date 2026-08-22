/**
 * @file main.c
 * @brief NEXUS CSI Sensor - application entry point and subsystem bring-up.
 *
 * Boot order is deliberate: persistent storage and configuration first, then
 * the logger, then the radio (WiFi), then CSI capture which depends on the
 * radio, then the motion engine which consumes CSI, then the optional display,
 * OTA bookkeeping and finally the web server. Each long-running subsystem runs
 * in its own FreeRTOS task and subscribes to the Task Watchdog independently;
 * this function only wires them up and then confirms the running firmware image
 * is healthy (cancelling any pending OTA rollback).
 *
 * @copyright MIT License. See LICENSE at the repository root.
 */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_log.h"

#include "app_config.h"
#include "storage.h"
#include "config.h"
#include "logger.h"
#include "auth.h"
#include "wifi.h"
#include "csi.h"
#include "motion.h"
#include "display.h"
#include "ota.h"
#include "webserver.h"

static const char *TAG = "main";

/** Delay before a self-reboot on an unrecoverable startup error. */
#define NEXUS_BOOT_FAIL_REBOOT_MS 3000

/** How long to wait for a station connection before declaring the image good. */
#define NEXUS_HEALTH_WIFI_WAIT_MS 15000

static void print_banner(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    printf("\n");
    printf("  =============================================\n");
    printf("   NEXUS CSI Sensor  v%s\n", NEXUS_FW_VERSION);
    printf("   WiFi CSI Human Presence & Motion Detection\n");
    printf("  ---------------------------------------------\n");
    printf("   ESP-IDF     : %s\n", esp_get_idf_version());
    printf("   Chip        : %s, %d core(s)\n",
           (chip.model == CHIP_ESP32) ? "ESP32" : "ESP32-family", chip.cores);
    printf("   Free heap   : %u bytes\n", (unsigned)esp_get_free_heap_size());
    printf("  =============================================\n\n");
}

void app_main(void)
{
    print_banner();

    /* --- Persistent storage (NVS) --------------------------------- */
    esp_err_t err = storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage_init failed: %s (using volatile defaults)",
                 esp_err_to_name(err));
    }

    /* --- Configuration -------------------------------------------- */
    config_init();  /* always succeeds; falls back to factory defaults */

    /* --- Event logger --------------------------------------------- */
    if (logger_init() == ESP_OK) {
        logger_start_task();
    }
    LOG_EVENT("Boot: %s v%s (%s)", NEXUS_FW_NAME, NEXUS_FW_VERSION,
              esp_get_idf_version());

    /* --- Authentication / sessions -------------------------------- */
#if NEXUS_ENABLE_AUTH
    if (auth_init() != ESP_OK) {
        LOG_ERR("auth_init failed");
    }
#endif

    /* --- WiFi (required: CSI cannot run without the radio) -------- */
    err = wifi_start();
    if (err != ESP_OK) {
        LOG_ERR("WiFi start failed: %s - rebooting", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(NEXUS_BOOT_FAIL_REBOOT_MS));
        esp_restart();
    }

    /* --- CSI capture ---------------------------------------------- */
    err = csi_init();
    if (err != ESP_OK) {
        LOG_ERR("CSI init failed: %s (sensing disabled)", esp_err_to_name(err));
    } else if (csi_start_task() != ESP_OK) {
        LOG_ERR("CSI task could not start");
    }

    /* --- Motion / presence engine --------------------------------- */
    if (motion_init() == ESP_OK) {
        if (motion_start_task() != ESP_OK) {
            LOG_ERR("Motion task could not start");
        }
    } else {
        LOG_ERR("motion_init failed");
    }

    /* --- Optional TFT display ------------------------------------- */
    if (display_init() == ESP_OK) {
        display_start_task();
    } else {
        LOG_WARN("Display unavailable - running headless");
    }

    /* --- OTA bookkeeping ------------------------------------------ */
    ota_init();

    /* --- Web server (dashboard + REST + WebSocket + OTA) ---------- */
    err = webserver_start();
    if (err != ESP_OK) {
        LOG_ERR("Web server failed: %s - rebooting", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(NEXUS_BOOT_FAIL_REBOOT_MS));
        esp_restart();
    }

    /* --- Confirm image health (cancel pending OTA rollback) ------- *
     * Reaching this point means every subsystem initialised and the
     * dashboard is being served. Wait briefly for connectivity, then
     * mark the running image valid so the bootloader will not roll it
     * back on the next reset.                                        */

    /* Only worth waiting if there is a station connection to wait for. In
     * soft-AP fallback there is no join in flight, so the full timeout would be
     * 15 seconds of dead air on exactly the boot where somebody is standing
     * there waiting for the setup network to appear. */
    bool connected = false;
    if (wifi_get_role() == WIFI_ROLE_STA) {
        connected = wifi_wait_connected(NEXUS_HEALTH_WIFI_WAIT_MS);
    }

    char ip[16];
    wifi_get_ip_str(ip, sizeof(ip));
    ESP_LOGI(TAG, "Startup complete - %s (%s), dashboard at http://%s/",
             wifi_role_str(), connected ? "connected" : "AP/offline",
             ip[0] ? ip : "0.0.0.0");

    /* Marked valid even when the station never associated, and that is
     * deliberate. Rollback exists to escape an image that cannot run; this one
     * has booted every subsystem and is serving the dashboard. A failed join is
     * far more likely to be wrong credentials or an absent router than a bad
     * build, and rolling back for that would strand the user on old firmware
     * for a reason the update did not cause. It would also take away the
     * soft-AP recovery path, which lives in this image and is the only way to
     * fix those credentials. */
    ota_mark_valid();

    LOG_EVENT("System ready (%s, ip %s)", wifi_role_str(), ip[0] ? ip : "-");

    /* app_main returns here; the FreeRTOS scheduler keeps the spawned
     * subsystem tasks running. */
}
