/**
 * @file webserver.h
 * @brief Embedded HTTP + WebSocket server and REST API.
 *
 * Serves the embedded dashboard assets, exposes the JSON REST API, streams live
 * telemetry over a WebSocket, and handles browser-driven OTA uploads. All
 * mutating endpoints require an authenticated session and a matching CSRF
 * token.
 */
#ifndef NEXUS_WEBSERVER_H
#define NEXUS_WEBSERVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the HTTP server and register all handlers. */
esp_err_t webserver_start(void);

/** Stop the HTTP server. */
void webserver_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_WEBSERVER_H */
