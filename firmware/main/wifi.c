/**
 * @file wifi.c
 * @brief WiFi station/soft-AP implementation.
 */
#include "wifi.h"
#include "app_config.h"
#include "config.h"
#include "logger.h"
#include "utils.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_events;
static esp_netif_t       *s_sta_netif;
static esp_netif_t       *s_ap_netif;
static wifi_role_t        s_role;   /* single word: read without the lock */
static int                s_retry;
static bool               s_stack_ready;

/* Status fields are written by the bring-up paths and the event handler, and
 * read by the web, display and CSI layers from their own tasks. s_state_lock
 * keeps a reader from seeing a half-copied SSID or a torn address. */
static SemaphoreHandle_t  s_state_lock;
static esp_ip4_addr_t     s_ip;
static esp_ip4_addr_t     s_gw;
static char               s_ssid[33];

/* Work the event handler must not do inline. The default event loop task runs
 * every WiFi and IP event, so blocking in it delays IP_EVENT_STA_GOT_IP and
 * everything else. The handler posts a command and returns immediately. */
typedef enum {
    WIFI_CMD_RETRY_CONNECT,
    WIFI_CMD_FALLBACK_AP,
} wifi_cmd_t;

static QueueHandle_t s_cmd_q;

/* Set while wifi_reconfigure() is tearing the interface down. esp_wifi_disconnect()
 * posts an asynchronous disconnect event, and without this flag its handler would
 * race the reconfigure by reconnecting to the SSID we are replacing. */
static volatile bool s_reconfiguring;

static void start_softap_fallback(void);

static inline void state_lock(void)
{
    if (s_state_lock) {
        xSemaphoreTake(s_state_lock, portMAX_DELAY);
    }
}

static inline void state_unlock(void)
{
    if (s_state_lock) {
        xSemaphoreGive(s_state_lock);
    }
}

/* ------------------------------------------------------------------ */
/* Deferred work                                                      */
/* ------------------------------------------------------------------ */
static void wifi_worker_task(void *arg)
{
    (void)arg;
    wifi_cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_cmd_q, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (s_reconfiguring) {
            continue;
        }
        switch (cmd) {
            case WIFI_CMD_RETRY_CONNECT:
                vTaskDelay(pdMS_TO_TICKS(NEXUS_WIFI_RETRY_BACKOFF_MS));
                if (!s_reconfiguring) {
                    esp_wifi_connect();
                }
                break;

            case WIFI_CMD_FALLBACK_AP:
                start_softap_fallback();
                break;
        }
    }
}

static void wifi_post_cmd(wifi_cmd_t cmd)
{
    if (s_cmd_q == NULL) {
        return;
    }
    /* Never block the event loop task, even for a full queue. */
    (void)xQueueSend(s_cmd_q, &cmd, 0);
}

/* ------------------------------------------------------------------ */
/* Event handling                                                     */
/* ------------------------------------------------------------------ */
static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    (void)arg;
    if (base != WIFI_EVENT) {
        return;
    }
    switch (id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_CONNECTED: {
            wifi_event_sta_connected_t *e = (wifi_event_sta_connected_t *)data;
            LOG_INFO("STA connected to '%.*s' (ch %u)",
                     e->ssid_len, (char *)e->ssid, e->channel);
            break;
        }

        case WIFI_EVENT_STA_DISCONNECTED: {
            xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);
            if (s_reconfiguring) {
                /* Expected: we asked for this disconnect. Do not reconnect. */
                break;
            }
            if (s_retry < NEXUS_WIFI_CONNECT_RETRY_MAX) {
                s_retry++;
                ESP_LOGW(TAG, "STA disconnected, retry %d/%d",
                         s_retry, NEXUS_WIFI_CONNECT_RETRY_MAX);
                wifi_post_cmd(WIFI_CMD_RETRY_CONNECT);
            } else {
                LOG_WARN("STA join failed after %d retries, starting soft-AP",
                         s_retry);
                xEventGroupSetBits(s_events, WIFI_FAIL_BIT);
                wifi_post_cmd(WIFI_CMD_FALLBACK_AP);
            }
            break;
        }

        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
            LOG_INFO("AP client joined " MACSTR, MAC2STR(e->mac));
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)data;
            LOG_INFO("AP client left " MACSTR, MAC2STR(e->mac));
            break;
        }
        default:
            break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base,
                        int32_t id, void *data)
{
    (void)arg;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        state_lock();
        s_ip = e->ip_info.ip;
        s_gw = e->ip_info.gw;
        s_role = WIFI_ROLE_STA;
        state_unlock();
        s_retry = 0;
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
        LOG_EVENT("Got IP " IPSTR " (gw " IPSTR ")",
                  IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.gw));
    }
}

/* ------------------------------------------------------------------ */
/* Stack bring-up                                                     */
/* ------------------------------------------------------------------ */
static esp_err_t ensure_stack(void)
{
    if (s_stack_ready) {
        return ESP_OK;
    }

    s_state_lock = xSemaphoreCreateMutex();
    if (s_state_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_cmd_q = xQueueCreate(4, sizeof(wifi_cmd_t));
    if (s_cmd_q == NULL) {
        vSemaphoreDelete(s_state_lock);
        s_state_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, NULL, NULL));

    /* Storing config in RAM avoids surprise reconnections to stale networks. */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(wifi_worker_task, "nexus_wifi", NEXUS_TASK_STACK_WIFI, NULL,
                    NEXUS_TASK_PRIO_WIFI, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_stack_ready = true;
    return ESP_OK;
}

static void start_station(const nexus_config_t *cfg)
{
    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    utils_strlcpy((char *)wc.sta.ssid, cfg->wifi_ssid, sizeof(wc.sta.ssid));
    utils_strlcpy((char *)wc.sta.password, cfg->wifi_pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = cfg->wifi_pass[0] ? WIFI_AUTH_WPA2_PSK
                                                  : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.pmf_cfg.required = false;

    state_lock();
    utils_strlcpy(s_ssid, cfg->wifi_ssid, sizeof(s_ssid));
    s_role = WIFI_ROLE_STA;
    state_unlock();
    s_retry = 0;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    LOG_INFO("Joining network '%s' ...", cfg->wifi_ssid);
}

static void start_softap(const nexus_config_t *cfg)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    snprintf((char *)wc.ap.ssid, sizeof(wc.ap.ssid), "%s-%02X%02X",
             NEXUS_AP_SSID_PREFIX, mac[4], mac[5]);
    wc.ap.ssid_len = strlen((char *)wc.ap.ssid);
    utils_strlcpy((char *)wc.ap.password, NEXUS_AP_PASSWORD, sizeof(wc.ap.password));
    wc.ap.channel = NEXUS_AP_CHANNEL;
    wc.ap.max_connection = NEXUS_AP_MAX_CONN;
    wc.ap.authmode = strlen(NEXUS_AP_PASSWORD) >= 8 ? WIFI_AUTH_WPA2_PSK
                                                    : WIFI_AUTH_OPEN;
    (void)cfg;

    state_lock();
    utils_strlcpy(s_ssid, (char *)wc.ap.ssid, sizeof(s_ssid));
    s_role = WIFI_ROLE_AP;
    state_unlock();

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* The default AP netif hands out 192.168.4.1. Record it for status. */
    esp_ip4_addr_t shown = { 0 };
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_ap_netif, &info) == ESP_OK) {
        state_lock();
        s_ip = info.ip;
        s_gw = info.gw;
        shown = info.ip;
        state_unlock();
    }
    LOG_EVENT("Soft-AP '%s' up (connect and browse to http://" IPSTR ")",
              wc.ap.ssid, IP2STR(&shown));
}

static void start_softap_fallback(void)
{
    nexus_config_t cfg;
    config_get(&cfg);
    esp_wifi_stop();
    start_softap(&cfg);
}

esp_err_t wifi_start(void)
{
    esp_err_t err = ensure_stack();
    if (err != ESP_OK) {
        return err;
    }

    nexus_config_t cfg;
    config_get(&cfg);

    if (cfg.wifi_ssid[0] != '\0') {
        start_station(&cfg);
    } else {
        LOG_WARN("No WiFi SSID configured; starting provisioning soft-AP");
        start_softap(&cfg);
    }
    return ESP_OK;
}

esp_err_t wifi_reconfigure(void)
{
    if (!s_stack_ready) {
        return wifi_start();
    }

    /* Suppress the auto-reconnect that our own disconnect is about to trigger. */
    s_reconfiguring = true;
    esp_wifi_disconnect();
    esp_wifi_stop();
    xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    nexus_config_t cfg;
    config_get(&cfg);
    if (cfg.wifi_ssid[0] != '\0') {
        start_station(&cfg);
    } else {
        start_softap(&cfg);
    }
    s_reconfiguring = false;
    return ESP_OK;
}

bool wifi_wait_connected(uint32_t timeout_ms)
{
    if (s_events == NULL) {
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(
        s_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

bool wifi_is_connected(void)
{
    if (s_role == WIFI_ROLE_AP) {
        return true;
    }
    if (s_events == NULL) {
        return false;
    }
    return (xEventGroupGetBits(s_events) & WIFI_CONNECTED_BIT) != 0;
}

wifi_role_t wifi_get_role(void) { return s_role; }

const char *wifi_role_str(void)
{
    switch (s_role) {
        case WIFI_ROLE_STA: return "STA";
        case WIFI_ROLE_AP:  return "AP";
        default:            return "-";
    }
}

void wifi_get_ip_str(char *out, size_t out_sz)
{
    if (out == NULL || out_sz == 0) return;
    state_lock();
    esp_ip4_addr_t ip = s_ip;
    state_unlock();
    esp_ip4addr_ntoa(&ip, out, out_sz);
}

void wifi_get_gateway_str(char *out, size_t out_sz)
{
    if (out == NULL || out_sz == 0) return;
    state_lock();
    esp_ip4_addr_t gw = s_gw;
    state_unlock();
    esp_ip4addr_ntoa(&gw, out, out_sz);
}

esp_ip4_addr_t wifi_get_gateway(void)
{
    state_lock();
    esp_ip4_addr_t gw = s_gw;
    state_unlock();
    return gw;
}

void wifi_get_ssid(char *out, size_t out_sz)
{
    if (out == NULL || out_sz == 0) return;
    state_lock();
    utils_strlcpy(out, s_ssid, out_sz);
    state_unlock();
}

int wifi_get_rssi(void)
{
    if (s_role != WIFI_ROLE_STA) {
        return 0;
    }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}

uint8_t wifi_get_channel(void)
{
    uint8_t primary = 0;
    wifi_second_chan_t second;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK) {
        return primary;
    }
    return 0;
}
