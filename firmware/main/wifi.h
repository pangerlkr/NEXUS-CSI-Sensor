/**
 * @file wifi.h
 * @brief WiFi station/soft-AP management.
 *
 * On start the device attempts to join the configured network in station mode.
 * If no credentials are stored, or the join fails repeatedly, it falls back to
 * a soft-AP so the dashboard remains reachable for (re)configuration. The
 * module owns all WiFi/IP event handling and exposes read-only status getters
 * consumed by the CSI, display and web layers.
 */
#ifndef NEXUS_WIFI_H
#define NEXUS_WIFI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif_ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Current radio role. */
typedef enum {
    WIFI_ROLE_NONE = 0,
    WIFI_ROLE_STA,      /**< joined an existing network */
    WIFI_ROLE_AP,       /**< provisioning soft-AP fallback */
} wifi_role_t;

/** Initialise the netif/event stack and start WiFi per stored configuration. */
esp_err_t wifi_start(void);

/** Reconnect using the latest configuration (e.g. after settings change). */
esp_err_t wifi_reconfigure(void);

/** @return true if associated (STA) or running as AP. */
bool wifi_is_connected(void);

/** @return the active role. */
wifi_role_t wifi_get_role(void);

/** @return human-readable role string ("STA", "AP", "-"). */
const char *wifi_role_str(void);

/** Copy the current IPv4 address as text into @p out (e.g. "192.168.1.42"). */
void wifi_get_ip_str(char *out, size_t out_sz);

/** Copy the gateway IPv4 as text; useful for CSI traffic generation. */
void wifi_get_gateway_str(char *out, size_t out_sz);

/** @return the gateway IPv4 address (zero if unknown). */
esp_ip4_addr_t wifi_get_gateway(void);

/** Copy the connected SSID (STA) or AP SSID into @p out. */
void wifi_get_ssid(char *out, size_t out_sz);

/** @return last known RSSI in dBm (0 if unavailable). */
int wifi_get_rssi(void);

/** @return current primary channel (1..13). */
uint8_t wifi_get_channel(void);

/**
 * @brief Block until the station obtains an IP, or timeout.
 * @param timeout_ms Maximum wait in milliseconds.
 * @return true if connected within the timeout.
 */
bool wifi_wait_connected(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_WIFI_H */
