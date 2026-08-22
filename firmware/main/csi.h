/**
 * @file csi.h
 * @brief WiFi Channel State Information (CSI) acquisition.
 *
 * Registers a CSI receive callback with the WiFi driver. For every captured
 * packet it derives a compact per-packet feature (mean subcarrier amplitude and
 * spatial spread), together with RSSI and a timestamp, and stores it in a
 * circular buffer for the motion engine to consume. An optional ICMP traffic
 * generator keeps packets - and therefore CSI - flowing on an otherwise idle
 * link.
 *
 * @note Developed and tested against ESP-IDF v5.1.x / v5.2.x. The
 *       @c wifi_csi_config_t layout used here matches that API surface.
 */
#ifndef NEXUS_CSI_H
#define NEXUS_CSI_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One per-packet CSI feature sample stored in the circular buffer. */
typedef struct {
    uint32_t timestamp_us;  /**< driver RX timestamp (microseconds) */
    float    amp_mean;      /**< mean amplitude across valid subcarriers */
    float    amp_std;       /**< spatial std-dev across subcarriers */
    int8_t   rssi;          /**< RSSI of the packet (dBm) */
    uint8_t  subcarriers;   /**< number of valid subcarriers used */
} csi_sample_t;

/** Aggregate CSI metrics for the dashboard / display. */
typedef struct {
    int8_t   rssi;
    float    amp_mean;
    float    amp_std;
    uint64_t packets_total;
    float    packets_per_sec;
    uint32_t last_packet_ms;
    uint32_t packets_dropped;  /**< samples the ring buffer could not accept */
} csi_metrics_t;

/**
 * @brief Configure and enable CSI capture. WiFi must already be started.
 * @return ESP_OK on success.
 */
esp_err_t csi_init(void);

/** Spawn the CSI housekeeping task (packet-rate stats + traffic generator). */
esp_err_t csi_start_task(void);

/** Copy the latest aggregate metrics into @p out (thread-safe). */
void csi_get_metrics(csi_metrics_t *out);

/**
 * @brief Copy up to @p max recent samples (oldest first) into @p out.
 * @return number of samples copied.
 */
size_t csi_snapshot_samples(csi_sample_t *out, size_t max);

/** @return number of samples currently buffered. */
size_t csi_sample_count(void);

/** @return true if packets have been received recently (link is live). */
bool csi_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_CSI_H */
