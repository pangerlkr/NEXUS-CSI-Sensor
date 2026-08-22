/**
 * @file csi.c
 * @brief CSI acquisition implementation.
 */
#include "csi.h"
#include "app_config.h"
#include "config.h"
#include "ringbuffer.h"
#include "wifi.h"
#include "logger.h"
#include "utils.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"

static const char *TAG = "csi";

static ringbuffer_t      s_samples;
static SemaphoreHandle_t s_metrics_lock;
static csi_metrics_t     s_metrics;
static esp_ping_handle_t s_ping;
static uint16_t          s_ping_rate_hz;

/* Counted outside the metrics lock. The callback only try-locks, so folding the
 * total into s_metrics there would silently drop every packet that arrived while
 * a reader held the lock, and the reported rate would read low under load. */
static volatile uint64_t s_packets_total;

/* Samples the ring buffer refused because its mutex was busy. Non-zero here
 * means a reader is holding the sample buffer for too long. */
static volatile uint32_t s_packets_dropped;

/* ------------------------------------------------------------------ */
/* CSI receive callback (runs in the WiFi task context)               */
/* ------------------------------------------------------------------ */
static void csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    (void)ctx;
    if (info == NULL || info->buf == NULL || info->len == 0) {
        return;
    }

    const int8_t *buf = info->buf;
    int pairs = info->len / 2;             /* [imag, real] int8 pairs */
    if (pairs > NEXUS_CSI_MAX_SUBCARRIERS) {
        pairs = NEXUS_CSI_MAX_SUBCARRIERS;
    }

    float sum = 0.0f, sumsq = 0.0f;
    int cnt = 0;
    for (int i = 0; i < pairs; ++i) {
        int im = buf[2 * i];
        int re = buf[2 * i + 1];
        if (im == 0 && re == 0) {
            continue;                       /* null / guard subcarrier */
        }
        float amp = sqrtf((float)(re * re + im * im));
        sum += amp;
        sumsq += amp * amp;
        cnt++;
    }
    if (cnt == 0) {
        return;
    }

    float mean = sum / (float)cnt;
    float var  = (sumsq / (float)cnt) - (mean * mean);
    if (var < 0.0f) {
        var = 0.0f;
    }
    float std = sqrtf(var);

    csi_sample_t s = {
        .timestamp_us = info->rx_ctrl.timestamp,
        .amp_mean     = mean,
        .amp_std      = std,
        .rssi         = (int8_t)info->rx_ctrl.rssi,
        .subcarriers  = (uint8_t)cnt,
    };
    if (!ringbuffer_push_timeout(&s_samples, &s, NEXUS_CSI_PUSH_TIMEOUT_MS)) {
        /* Reader held the buffer too long. Drop the sample rather than block the
         * WiFi task, and count it so the dashboard can show that it happened. */
        s_packets_dropped++;
    }

    /* Always counted, even if the metrics lock is busy. */
    s_packets_total++;

    if (xSemaphoreTake(s_metrics_lock, 0) == pdTRUE) {
        s_metrics.rssi          = s.rssi;
        s_metrics.amp_mean      = mean;
        s_metrics.amp_std       = std;
        s_metrics.last_packet_ms = utils_millis();
        xSemaphoreGive(s_metrics_lock);
    }
}

/* ------------------------------------------------------------------ */
/* Init                                                               */
/* ------------------------------------------------------------------ */
esp_err_t csi_init(void)
{
    s_metrics_lock = xSemaphoreCreateMutex();
    if (s_metrics_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_metrics, 0, sizeof(s_metrics));
    s_packets_total = 0;
    s_packets_dropped = 0;

    esp_err_t err = ringbuffer_init(&s_samples, sizeof(csi_sample_t),
                                    NEXUS_CSI_RING_CAPACITY);
    if (err != ESP_OK) {
        vSemaphoreDelete(s_metrics_lock);
        s_metrics_lock = NULL;
        return err;
    }

    /* CSI acquisition parameters. Enabling both L-LTF and HT-LTF captures the
     * widest set of subcarriers; channel filtering and LTF merge reduce noise. */
    wifi_csi_config_t csi_config = {
        .lltf_en           = true,
        .htltf_en          = true,
        .stbc_htltf2_en    = true,
        .ltf_merge_en      = true,
        .channel_filter_en = true,
        .manu_scale        = false,
        .shift             = 0,
    };

    err = esp_wifi_set_csi_config(&csi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_csi_config: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_set_csi_rx_cb(&csi_rx_cb, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_csi_rx_cb: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_set_csi(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_csi(true): %s", esp_err_to_name(err));
        return err;
    }

    LOG_INFO("CSI capture enabled");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Traffic generator (ICMP ping to the gateway)                       */
/* ------------------------------------------------------------------ */
#if NEXUS_ENABLE_TRAFFIC_GEN
static void ping_on_success(esp_ping_handle_t hdl, void *args) { (void)hdl; (void)args; }
static void ping_on_timeout(esp_ping_handle_t hdl, void *args) { (void)hdl; (void)args; }
static void ping_on_end(esp_ping_handle_t hdl, void *args)     { (void)hdl; (void)args; }

static void restart_traffic_generator(uint16_t rate_hz)
{
    if (wifi_get_role() != WIFI_ROLE_STA) {
        return;   /* no gateway to ping in soft-AP mode */
    }
    esp_ip4_addr_t gw = wifi_get_gateway();
    if (gw.addr == 0) {
        return;
    }
    if (s_ping) {
        esp_ping_stop(s_ping);
        esp_ping_delete_session(s_ping);
        s_ping = NULL;
    }

    ip_addr_t target;
    memset(&target, 0, sizeof(target));
    target.type = IPADDR_TYPE_V4;
    target.u_addr.ip4.addr = gw.addr;

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr = target;
    cfg.count = ESP_PING_COUNT_INFINITE;
    cfg.interval_ms = 1000U / (rate_hz ? rate_hz : NEXUS_SAMPLING_RATE_DEFAULT);
    if (cfg.interval_ms == 0) {
        cfg.interval_ms = 1;
    }
    cfg.data_size = 32;

    esp_ping_callbacks_t cbs = {
        .on_ping_success = ping_on_success,
        .on_ping_timeout = ping_on_timeout,
        .on_ping_end     = ping_on_end,
        .cb_args         = NULL,
    };
    if (esp_ping_new_session(&cfg, &cbs, &s_ping) == ESP_OK) {
        esp_ping_start(s_ping);
        s_ping_rate_hz = rate_hz;
        LOG_INFO("Traffic generator pinging gateway at %u Hz", rate_hz);
    } else {
        ESP_LOGW(TAG, "failed to start ping session");
    }
}
#endif /* NEXUS_ENABLE_TRAFFIC_GEN */

/* ------------------------------------------------------------------ */
/* Housekeeping task                                                  */
/* ------------------------------------------------------------------ */
static void csi_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    uint64_t last_total = 0;
    uint32_t last_tick = utils_millis();

    for (;;) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));

        /* Packet-rate computation (per second). */
        uint32_t now = utils_millis();
        float dt = (now - last_tick) / 1000.0f;
        if (dt <= 0.0f) {
            dt = 1.0f;
        }
        uint64_t total = s_packets_total;
        xSemaphoreTake(s_metrics_lock, portMAX_DELAY);
        s_metrics.packets_total = total;
        s_metrics.packets_per_sec = (float)(total - last_total) / dt;
        xSemaphoreGive(s_metrics_lock);
        last_total = total;
        last_tick = now;

#if NEXUS_ENABLE_TRAFFIC_GEN
        /* (Re)configure the traffic generator when the sampling rate changes
         * or once the station has associated. */
        nexus_config_t cfg;
        config_get(&cfg);
        if (wifi_get_role() == WIFI_ROLE_STA && wifi_is_connected() &&
            (s_ping == NULL || s_ping_rate_hz != cfg.sampling_rate_hz)) {
            restart_traffic_generator(cfg.sampling_rate_hz);
        }
#endif
    }
}

esp_err_t csi_start_task(void)
{
    BaseType_t ok = xTaskCreate(csi_task, "nexus_csi",
                                NEXUS_TASK_STACK_CSI, NULL,
                                NEXUS_TASK_PRIO_CSI, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

/* ------------------------------------------------------------------ */
/* Accessors                                                          */
/* ------------------------------------------------------------------ */
void csi_get_metrics(csi_metrics_t *out)
{
    if (out == NULL) {
        return;
    }
    xSemaphoreTake(s_metrics_lock, portMAX_DELAY);
    *out = s_metrics;
    xSemaphoreGive(s_metrics_lock);
    /* The task refreshes these once a second; report the live values. */
    out->packets_total = s_packets_total;
    out->packets_dropped = s_packets_dropped;
}

size_t csi_snapshot_samples(csi_sample_t *out, size_t max)
{
    return ringbuffer_snapshot(&s_samples, out, max);
}

size_t csi_sample_count(void)
{
    return ringbuffer_count(&s_samples);
}

bool csi_is_active(void)
{
    csi_metrics_t m;
    csi_get_metrics(&m);
    if (m.packets_total == 0) {
        return false;
    }
    return (utils_millis() - m.last_packet_ms) < 3000U;
}
