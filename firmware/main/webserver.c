/**
 * @file webserver.c
 * @brief HTTP + WebSocket server, REST API and OTA upload implementation.
 */
#include "webserver.h"
#include "app_config.h"
#include "config.h"
#include "auth.h"
#include "wifi.h"
#include "csi.h"
#include "motion.h"
#include "logger.h"
#include "ota.h"
#include "json.h"
#include "utils.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

static const char *TAG = "web";

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/* ---- Embedded web assets (see main/CMakeLists.txt EMBED_FILES) ---- */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t login_html_start[] asm("_binary_login_html_start");
extern const uint8_t login_html_end[]   asm("_binary_login_html_end");
extern const uint8_t style_css_start[]  asm("_binary_style_css_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_end");
extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_end");

static httpd_handle_t    s_server;

/* One connected WebSocket client. The session token is kept so the push task can
 * confirm the client is still logged in, rather than trusting a handshake that
 * may have happened an hour ago. */
typedef struct {
    int  fd;
    char token[NEXUS_SESSION_TOKEN_HEX_LEN + 1];
} ws_client_t;

static ws_client_t       s_ws_clients[NEXUS_WS_MAX_CLIENTS];
static SemaphoreHandle_t s_ws_lock;
static TaskHandle_t      s_ws_task;
static volatile bool     s_ws_task_stop;

/* ================================================================== */
/* Small response helpers                                              */
/* ================================================================== */
static void set_common_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
}

static esp_err_t send_static(httpd_req_t *req, const uint8_t *start,
                             const uint8_t *end, const char *ctype)
{
    set_common_headers(req);
    httpd_resp_set_type(req, ctype);
    return httpd_resp_send(req, (const char *)start, end - start);
}

static esp_err_t send_json(httpd_req_t *req, int status, json_builder_t *b)
{
    size_t len = 0;
    const char *s = json_builder_str(b, &len);
    set_common_headers(req);
    switch (status) {
        case 200: httpd_resp_set_status(req, "200 OK"); break;
        case 400: httpd_resp_set_status(req, "400 Bad Request"); break;
        case 401: httpd_resp_set_status(req, "401 Unauthorized"); break;
        case 403: httpd_resp_set_status(req, "403 Forbidden"); break;
        case 404: httpd_resp_set_status(req, "404 Not Found"); break;
        case 413: httpd_resp_set_status(req, "413 Payload Too Large"); break;
        case 429: httpd_resp_set_status(req, "429 Too Many Requests"); break;
        case 500: httpd_resp_set_status(req, "500 Internal Server Error"); break;
        default:  httpd_resp_set_status(req, "200 OK"); break;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s, len);
}

static esp_err_t send_json_msg(httpd_req_t *req, int status, bool ok, const char *msg)
{
    json_builder_t b;
    json_builder_init(&b, 96);
    json_obj_open(&b);
    json_kv_bool(&b, "ok", ok);
    json_kv_str(&b, "message", msg ? msg : "");
    json_obj_close(&b);
    esp_err_t e = send_json(req, status, &b);
    json_builder_free(&b);
    return e;
}

/* ================================================================== */
/* Request introspection                                               */
/* ================================================================== */
static void get_client_ip(httpd_req_t *req, char *out, size_t out_sz)
{
    out[0] = '\0';
    int sock = httpd_req_to_sockfd(req);
    if (sock < 0) return;
    struct sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    if (getpeername(sock, (struct sockaddr *)&ss, &len) != 0) {
        return;
    }
    if (ss.ss_family == AF_INET) {
        struct sockaddr_in *a = (struct sockaddr_in *)&ss;
        inet_ntop(AF_INET, &a->sin_addr, out, out_sz);
    }
#if defined(CONFIG_LWIP_IPV6) && CONFIG_LWIP_IPV6
    else if (ss.ss_family == AF_INET6) {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)&ss;
        inet_ntop(AF_INET6, &a->sin6_addr, out, out_sz);
    }
#endif
}

/* Find a cookie by name.
 *
 * A bare strstr(cookie, "session=") also matches inside "mysession=", so the
 * match has to sit at the start of the header or just after a "; " separator. */
static const char *cookie_find(const char *cookie, const char *needle, size_t needle_len)
{
    for (const char *p = strstr(cookie, needle); p != NULL;
         p = strstr(p + 1, needle)) {
        /* Walk back over any optional whitespace, then insist on either the
         * start of the header or a ';' separator. */
        const char *q = p;
        while (q > cookie && (q[-1] == ' ' || q[-1] == '\t')) {
            q--;
        }
        if (q == cookie || q[-1] == ';') {
            return p + needle_len;
        }
    }
    return NULL;
}

static bool get_cookie_value(httpd_req_t *req, const char *name, char *out, size_t out_sz)
{
    size_t clen = httpd_req_get_hdr_value_len(req, "Cookie");
    if (clen == 0) {
        return false;
    }
    char *cookie = (char *)malloc(clen + 1);
    if (!cookie) {
        return false;
    }
    bool found = false;
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, clen + 1) == ESP_OK) {
        char needle[40];
        int nl = snprintf(needle, sizeof(needle), "%s=", name);
        const char *p = (nl > 0 && (size_t)nl < sizeof(needle))
                            ? cookie_find(cookie, needle, (size_t)nl)
                            : NULL;
        if (p) {
            size_t i = 0;
            while (p[i] && p[i] != ';' && p[i] != ' ' && i + 1 < out_sz) {
                out[i] = p[i];
                i++;
            }
            out[i] = '\0';
            found = (i > 0);
        }
    }
    free(cookie);
    return found;
}

static bool get_header(httpd_req_t *req, const char *name, char *out, size_t out_sz)
{
    size_t len = httpd_req_get_hdr_value_len(req, name);
    if (len == 0 || len + 1 > out_sz) {
        return false;
    }
    return httpd_req_get_hdr_value_str(req, name, out, out_sz) == ESP_OK;
}

/**
 * @brief Gate a request behind a valid session (and optionally CSRF).
 * @return true if allowed; otherwise a 401/403 response has been sent.
 */
static bool require_auth(httpd_req_t *req, bool need_csrf)
{
#if NEXUS_ENABLE_AUTH
    char token[NEXUS_SESSION_TOKEN_HEX_LEN + 1];
    if (!get_cookie_value(req, "session", token, sizeof(token)) ||
        !auth_validate_session(token, NULL, 0)) {
        send_json_msg(req, 401, false, "authentication required");
        return false;
    }
    if (need_csrf) {
        char csrf[NEXUS_SESSION_TOKEN_HEX_LEN + 1];
        if (!get_header(req, "X-CSRF-Token", csrf, sizeof(csrf)) ||
            !auth_validate_csrf(token, csrf)) {
            send_json_msg(req, 403, false, "invalid CSRF token");
            return false;
        }
    }
#else
    (void)req; (void)need_csrf;
#endif
    return true;
}

static char *read_body(httpd_req_t *req, size_t max_len, size_t *out_len)
{
    size_t len = req->content_len;
    if (len == 0 || len > max_len) {
        return NULL;
    }
    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        return NULL;
    }
    size_t off = 0;
    while (off < len) {
        int r = httpd_req_recv(req, buf + off, len - off);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(buf);
            return NULL;
        }
        off += (size_t)r;
    }
    buf[len] = '\0';
    if (out_len) {
        *out_len = len;
    }
    return buf;
}

/* ================================================================== */
/* Deferred reboot                                                     */
/* ================================================================== */
static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(800));
    logger_persist();
    esp_restart();
}

static void schedule_reboot(void)
{
    /* 3072 bytes: reboot_task calls logger_persist(), which copies the held log
     * entries into one buffer and writes it to NVS. */
    xTaskCreate(reboot_task, "nexus_reboot", 3072, NULL, 5, NULL);
}

/* ================================================================== */
/* JSON builders                                                       */
/* ================================================================== */
static void build_status_full(json_builder_t *b)
{
    nexus_config_t cfg;
    config_get(&cfg);
    motion_result_t mr;
    motion_get_result(&mr);
    csi_metrics_t cm;
    csi_get_metrics(&cm);

    uint32_t up_s = utils_millis() / 1000U;
    char up_str[24];
    utils_format_uptime(up_s, up_str, sizeof(up_str));
    char ip[16], gw[16], ssid[33];
    wifi_get_ip_str(ip, sizeof(ip));
    wifi_get_gateway_str(gw, sizeof(gw));
    wifi_get_ssid(ssid, sizeof(ssid));

    json_obj_open(b);
    json_kv_str(b, "device", cfg.device_name);
    json_kv_str(b, "fw", NEXUS_FW_VERSION);
    json_kv_uint(b, "uptime_s", up_s);
    json_kv_str(b, "uptime", up_str);
    json_kv_uint(b, "heap_free", esp_get_free_heap_size());
    json_kv_uint(b, "heap_min", esp_get_minimum_free_heap_size());
    json_kv_str(b, "partition", ota_running_partition());
    json_kv_int(b, "sessions", auth_active_sessions());

    json_kv_obj_open(b, "wifi");
    json_kv_str(b, "role", wifi_role_str());
    json_kv_bool(b, "connected", wifi_is_connected());
    json_kv_str(b, "ssid", ssid);
    json_kv_str(b, "ip", ip);
    json_kv_str(b, "gateway", gw);
    json_kv_int(b, "rssi", wifi_get_rssi());
    json_kv_int(b, "channel", wifi_get_channel());
    json_obj_close(b);

    json_kv_obj_open(b, "csi");
    json_kv_int(b, "rssi", cm.rssi);
    json_kv_float(b, "amp_mean", cm.amp_mean, 2);
    json_kv_float(b, "amp_std", cm.amp_std, 2);
    json_kv_float(b, "pps", cm.packets_per_sec, 1);
    json_kv_uint(b, "packets_total", cm.packets_total);
    json_kv_uint(b, "packets_dropped", cm.packets_dropped);
    json_kv_bool(b, "active", csi_is_active());
    json_obj_close(b);

    json_kv_obj_open(b, "motion");
    json_kv_int(b, "state", (int)mr.state);
    json_kv_str(b, "state_str", motion_state_str(mr.state));
    json_kv_bool(b, "presence", mr.presence);
    json_kv_float(b, "score", mr.motion_score, 3);
    json_kv_float(b, "intensity", mr.motion_intensity, 1);
    json_kv_float(b, "activity", mr.activity_level, 1);
    json_kv_float(b, "variance", mr.variance, 3);
    json_kv_float(b, "baseline", mr.baseline, 3);
    json_kv_float(b, "signal_quality", mr.signal_quality, 1);
    json_kv_uint(b, "state_since_s", (utils_millis() - mr.state_since_ms) / 1000U);
    json_obj_close(b);

    json_obj_close(b);
}

/* ================================================================== */
/* Static asset handlers                                               */
/* ================================================================== */
static esp_err_t h_root(httpd_req_t *req)
{
    return send_static(req, index_html_start, index_html_end, "text/html");
}
static esp_err_t h_login_page(httpd_req_t *req)
{
    return send_static(req, login_html_start, login_html_end, "text/html");
}
static esp_err_t h_css(httpd_req_t *req)
{
    return send_static(req, style_css_start, style_css_end, "text/css");
}
static esp_err_t h_js(httpd_req_t *req)
{
    return send_static(req, app_js_start, app_js_end, "application/javascript");
}

/* ================================================================== */
/* REST handlers                                                       */
/* ================================================================== */
static esp_err_t h_api_login(httpd_req_t *req)
{
    size_t blen = 0;
    char *body = read_body(req, NEXUS_HTTP_MAX_BODY, &blen);
    if (!body) {
        return send_json_msg(req, 400, false, "missing body");
    }
    json_doc_t *doc = json_parse(body, blen);
    free(body);
    if (!doc) {
        return send_json_msg(req, 400, false, "invalid JSON");
    }

    char user[32] = {0}, pass[65] = {0};
    json_get_str(doc, "user", user, sizeof(user));
    json_get_str(doc, "pass", pass, sizeof(pass));
    json_free(doc);

    char ip[48];
    get_client_ip(req, ip, sizeof(ip));

    char token[NEXUS_SESSION_TOKEN_HEX_LEN + 1];
    char csrf[NEXUS_SESSION_TOKEN_HEX_LEN + 1];
    if (!auth_login(user, pass, ip, token, sizeof(token), csrf, sizeof(csrf))) {
        /* Distinguish "wrong password" from "stop asking". A flat 401 for both
         * left the dashboard telling a locked-out user to check their password
         * for the next two minutes. The lockout state is already observable by
         * anyone who can try to log in, so saying so leaks nothing. */
        if (auth_is_rate_limited(ip)) {
            return send_json_msg(req, 429, false,
                                 "too many attempts, try again shortly");
        }
        return send_json_msg(req, 401, false, "invalid credentials");
    }

    char cookie[128];
    snprintf(cookie, sizeof(cookie),
             "session=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=%d",
             token, NEXUS_SESSION_TTL_S);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);

    json_builder_t b;
    json_builder_init(&b, 128);
    json_obj_open(&b);
    json_kv_bool(&b, "ok", true);
    json_kv_str(&b, "csrf", csrf);
    json_obj_close(&b);
    esp_err_t e = send_json(req, 200, &b);
    json_builder_free(&b);
    return e;
}

static esp_err_t h_api_logout(httpd_req_t *req)
{
    char token[NEXUS_SESSION_TOKEN_HEX_LEN + 1];
    if (get_cookie_value(req, "session", token, sizeof(token))) {
        auth_logout(token);
    }
    httpd_resp_set_hdr(req, "Set-Cookie",
                       "session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    return send_json_msg(req, 200, true, "logged out");
}

/**
 * @brief Report whether the caller has a valid session and, if so, hand back
 *        the CSRF token bound to it. The dashboard calls this on load so it can
 *        authorise mutating requests without relying on client-side storage.
 */
static esp_err_t h_api_session(httpd_req_t *req)
{
    char csrf[NEXUS_SESSION_TOKEN_HEX_LEN + 1] = {0};
#if NEXUS_ENABLE_AUTH
    char token[NEXUS_SESSION_TOKEN_HEX_LEN + 1];
    if (!get_cookie_value(req, "session", token, sizeof(token)) ||
        !auth_validate_session(token, csrf, sizeof(csrf))) {
        return send_json_msg(req, 401, false, "not authenticated");
    }
#endif
    json_builder_t b;
    json_builder_init(&b, 96);
    json_obj_open(&b);
    json_kv_bool(&b, "authenticated", true);
    json_kv_bool(&b, "auth_required", NEXUS_ENABLE_AUTH ? true : false);
    json_kv_str(&b, "csrf", csrf);
    json_obj_close(&b);
    esp_err_t e = send_json(req, 200, &b);
    json_builder_free(&b);
    return e;
}

static esp_err_t h_api_status(httpd_req_t *req)
{
    if (!require_auth(req, false)) {
        return ESP_OK;
    }
    json_builder_t b;
    json_builder_init(&b, 640);
    build_status_full(&b);
    esp_err_t e = send_json(req, 200, &b);
    json_builder_free(&b);
    return e;
}

static esp_err_t h_api_config_get(httpd_req_t *req)
{
    if (!require_auth(req, false)) {
        return ESP_OK;
    }
    nexus_config_t cfg;
    config_get(&cfg);
    json_builder_t b;
    json_builder_init(&b, 384);
    config_to_json(&cfg, &b);
    esp_err_t e = send_json(req, 200, &b);
    json_builder_free(&b);
    return e;
}

static esp_err_t h_api_config_post(httpd_req_t *req)
{
    if (!require_auth(req, true)) {
        return ESP_OK;
    }
    size_t blen = 0;
    char *body = read_body(req, NEXUS_HTTP_MAX_BODY, &blen);
    if (!body) {
        return send_json_msg(req, 400, false, "missing body");
    }
    json_doc_t *doc = json_parse(body, blen);
    if (!doc) {
        free(body);
        return send_json_msg(req, 400, false, "invalid JSON");
    }

    nexus_config_t cfg;
    config_get(&cfg);
    char old_ssid[33], old_pass[65];
    utils_strlcpy(old_ssid, cfg.wifi_ssid, sizeof(old_ssid));
    utils_strlcpy(old_pass, cfg.wifi_pass, sizeof(old_pass));

    bool changed = config_apply_json(&cfg, doc);

    /* Optional password change (separate, salted+hashed path). */
    char newpass[65] = {0};
    bool pw_change = json_get_str(doc, "admin_pass", newpass, sizeof(newpass)) &&
                     newpass[0] != '\0';
    json_free(doc);
    free(body);

    if (changed) {
        config_set(&cfg);
    }
    if (pw_change) {
        config_set_password(newpass);
    }

    /* Reconfigure WiFi if network settings changed. */
    bool wifi_changed = (strcmp(old_ssid, cfg.wifi_ssid) != 0) ||
                        (strcmp(old_pass, cfg.wifi_pass) != 0);
    if (wifi_changed) {
        LOG_EVENT("WiFi settings changed, reconnecting");
        wifi_reconfigure();
    }

    return send_json_msg(req, 200, true,
                         wifi_changed ? "saved (reconnecting WiFi)" : "saved");
}

static esp_err_t h_api_reboot(httpd_req_t *req)
{
    if (!require_auth(req, true)) {
        return ESP_OK;
    }
    LOG_EVENT("Reboot requested via API");
    esp_err_t e = send_json_msg(req, 200, true, "rebooting");
    schedule_reboot();
    return e;
}

static esp_err_t h_api_calibrate(httpd_req_t *req)
{
    if (!require_auth(req, true)) {
        return ESP_OK;
    }
    motion_calibrate();
    return send_json_msg(req, 200, true, "calibration started");
}

static esp_err_t h_api_factory_reset(httpd_req_t *req)
{
    if (!require_auth(req, true)) {
        return ESP_OK;
    }
    config_factory_reset();
    logger_clear();
    LOG_EVENT("Factory reset via API");
    esp_err_t e = send_json_msg(req, 200, true, "factory reset, rebooting");
    schedule_reboot();
    return e;
}

static esp_err_t h_api_history(httpd_req_t *req)
{
    if (!require_auth(req, false)) {
        return ESP_OK;
    }
    history_point_t *pts = (history_point_t *)calloc(NEXUS_HISTORY_CAPACITY,
                                                     sizeof(history_point_t));
    if (!pts) {
        return send_json_msg(req, 500, false, "out of memory");
    }
    size_t n = motion_history_snapshot(pts, NEXUS_HISTORY_CAPACITY);

    json_builder_t b;
    json_builder_init(&b, 2048);
    json_obj_open(&b);
    json_kv_int(&b, "count", (long long)n);
    json_arr_open(&b, "t");
    for (size_t i = 0; i < n; ++i) json_elem_int(&b, pts[i].t_ms);
    json_arr_close(&b);
    json_arr_open(&b, "score");
    for (size_t i = 0; i < n; ++i) json_elem_float(&b, pts[i].score, 3);
    json_arr_close(&b);
    json_arr_open(&b, "variance");
    for (size_t i = 0; i < n; ++i) json_elem_float(&b, pts[i].variance, 3);
    json_arr_close(&b);
    json_arr_open(&b, "rssi");
    for (size_t i = 0; i < n; ++i) json_elem_int(&b, pts[i].rssi);
    json_arr_close(&b);
    json_arr_open(&b, "state");
    for (size_t i = 0; i < n; ++i) json_elem_int(&b, pts[i].state);
    json_arr_close(&b);
    json_obj_close(&b);
    free(pts);

    esp_err_t e = send_json(req, 200, &b);
    json_builder_free(&b);
    return e;
}

static esp_err_t h_api_logs_json(httpd_req_t *req)
{
    if (!require_auth(req, false)) {
        return ESP_OK;
    }
    json_builder_t b;
    json_builder_init(&b, 2048);
    json_obj_open(&b);
    logger_to_json(&b);
    json_obj_close(&b);
    esp_err_t e = send_json(req, 200, &b);
    json_builder_free(&b);
    return e;
}

static esp_err_t h_api_logs_csv(httpd_req_t *req)
{
    if (!require_auth(req, false)) {
        return ESP_OK;
    }
    /* Streamed a row at a time. The response is chunked anyway, so there is no
     * reason to hold a copy of the whole ring (~6.9 KB) in the heap while a slow
     * client drains it. Rows come out oldest first, and an event logged during
     * the download shifts the remaining positions down, so a busy device can
     * leave one row out of a single export. */
    size_t n = logger_count();

    set_common_headers(req);
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"nexus-logs.csv\"");

    httpd_resp_sendstr_chunk(req, "seq,uptime_ms,level,message\r\n");
    /* Worst case: every character in the message is a quote and doubles, plus
     * the numeric columns, the level, the quotes and the CRLF. Sizing this at
     * 160 truncated long messages mid-field and could cut the closing quote,
     * which corrupts the rest of the CSV for a strict parser. */
    char line[48 + NEXUS_LOG_MSG_LEN * 2];
    for (size_t i = 0; i < n; ++i) {
        log_entry_t e;
        if (!logger_peek(i, &e)) {
            continue;
        }
        /* Escape embedded quotes in the message for CSV. */
        char safe[NEXUS_LOG_MSG_LEN * 2];
        size_t j = 0;
        for (size_t k = 0; e.msg[k] && j + 2 < sizeof(safe); ++k) {
            if (e.msg[k] == '"') safe[j++] = '"';
            safe[j++] = e.msg[k];
        }
        safe[j] = '\0';
        snprintf(line, sizeof(line), "%u,%u,%s,\"%s\"\r\n",
                 (unsigned)e.seq, (unsigned)e.uptime_ms,
                 logger_level_str((log_level_t)e.level), safe);
        httpd_resp_sendstr_chunk(req, line);
    }
    httpd_resp_sendstr_chunk(req, NULL); /* end */
    return ESP_OK;
}

static esp_err_t h_api_ota(httpd_req_t *req)
{
    if (!require_auth(req, true)) {
        return ESP_OK;
    }
    size_t total = req->content_len;
    if (total == 0) {
        return send_json_msg(req, 400, false, "empty upload");
    }
    esp_err_t began = ota_begin(total);
    if (began == ESP_ERR_INVALID_SIZE) {
        return send_json_msg(req, 413, false, "image larger than the OTA partition");
    }
    if (began == ESP_ERR_INVALID_STATE) {
        return send_json_msg(req, 429, false, "an update is already running");
    }
    if (began != ESP_OK) {
        return send_json_msg(req, 500, false, "could not start update");
    }

    char *buf = (char *)malloc(1024);
    if (!buf) {
        ota_abort();
        return send_json_msg(req, 500, false, "out of memory");
    }

    size_t remaining = total;
    bool failed = false;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf, MIN(remaining, 1024));
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            failed = true;
            break;
        }
        if (ota_write(buf, (size_t)r) != ESP_OK) {
            failed = true;
            break;
        }
        remaining -= (size_t)r;
    }
    free(buf);

    if (failed) {
        ota_abort();
        return send_json_msg(req, 400, false, "upload failed");
    }
    if (ota_end() != ESP_OK) {
        return send_json_msg(req, 400, false, "image validation failed");
    }

    esp_err_t e = send_json_msg(req, 200, true, "update applied, rebooting");
    schedule_reboot();
    return e;
}

static esp_err_t h_api_ota_status(httpd_req_t *req)
{
    if (!require_auth(req, false)) {
        return ESP_OK;
    }
    ota_status_t st;
    ota_get_status(&st);
    json_builder_t b;
    json_builder_init(&b, 160);
    json_obj_open(&b);
    json_kv_bool(&b, "in_progress", st.in_progress);
    json_kv_int(&b, "percent", st.percent);
    json_kv_uint(&b, "received", st.received);
    json_kv_uint(&b, "total", st.total);
    json_kv_str(&b, "message", st.message);
    json_obj_close(&b);
    esp_err_t e = send_json(req, 200, &b);
    json_builder_free(&b);
    return e;
}

/* ================================================================== */
/* WebSocket                                                           */
/* ================================================================== */

/* A socket descriptor alone is not enough to identify a client.
 *
 * lwIP reuses fd numbers, so a descriptor left behind by an abrupt disconnect
 * can be handed to an entirely different connection, and the push task would
 * write a raw WebSocket frame onto it. Every push therefore re-checks that the
 * fd is still a WebSocket, and that the session it was opened with is still
 * valid, so logging out or letting the TTL lapse actually stops the stream. */
static void ws_add_client(int fd, const char *token)
{
    xSemaphoreTake(s_ws_lock, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < NEXUS_WS_MAX_CLIENTS; ++i) {
        if (s_ws_clients[i].fd == fd) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < NEXUS_WS_MAX_CLIENTS; ++i) {
            if (s_ws_clients[i].fd < 0) {
                slot = i;
                break;
            }
        }
    }
    if (slot >= 0) {
        s_ws_clients[slot].fd = fd;
        utils_strlcpy(s_ws_clients[slot].token, token ? token : "",
                      sizeof(s_ws_clients[slot].token));
    }
    xSemaphoreGive(s_ws_lock);
}

static void ws_remove_fd(int fd)
{
    xSemaphoreTake(s_ws_lock, portMAX_DELAY);
    for (int i = 0; i < NEXUS_WS_MAX_CLIENTS; ++i) {
        if (s_ws_clients[i].fd == fd) {
            s_ws_clients[i].fd = -1;
            s_ws_clients[i].token[0] = '\0';
        }
    }
    xSemaphoreGive(s_ws_lock);
}

static esp_err_t h_ws(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* Handshake completed. Authenticate via the session cookie. */
        char token[NEXUS_SESSION_TOKEN_HEX_LEN + 1] = {0};
#if NEXUS_ENABLE_AUTH
        if (!get_cookie_value(req, "session", token, sizeof(token)) ||
            !auth_validate_session(token, NULL, 0)) {
            ESP_LOGW(TAG, "WS handshake rejected (unauthorised)");
            return ESP_FAIL;
        }
#endif
        ws_add_client(httpd_req_to_sockfd(req), token);
        return ESP_OK;
    }

    /* Incoming frame: read and handle control frames. */
    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        return err;
    }
    if (frame.len > 0) {
        if (frame.len >= NEXUS_WS_MAX_FRAME_LEN) {
            /* Leaving an unread payload in the socket desynchronises every
             * later frame on it, so drop the client instead. */
            ESP_LOGW(TAG, "WS frame too large (%u bytes), closing client",
                     (unsigned)frame.len);
            ws_remove_fd(httpd_req_to_sockfd(req));
            return ESP_FAIL;
        }
        uint8_t *payload = (uint8_t *)calloc(1, frame.len + 1);
        if (payload == NULL) {
            ws_remove_fd(httpd_req_to_sockfd(req));
            return ESP_ERR_NO_MEM;
        }
        frame.payload = payload;
        err = httpd_ws_recv_frame(req, &frame, frame.len);
        free(payload);
        if (err != ESP_OK) {
            return err;
        }
    }
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        ws_remove_fd(httpd_req_to_sockfd(req));
    }
    return ESP_OK;
}

static void ws_push_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(NEXUS_WS_PUSH_INTERVAL_MS));

        if (s_ws_task_stop) {
            s_ws_task_stop = false;   /* acknowledge, then stand down */
            vTaskDelete(NULL);
        }

        httpd_handle_t server = s_server;
        if (server == NULL) {
            continue;
        }

        /* Snapshot the client list, then release the lock. Sending can block for
         * the full send timeout, and holding s_ws_lock across that would stall
         * every handshake and disconnect. */
        ws_client_t snap[NEXUS_WS_MAX_CLIENTS];
        xSemaphoreTake(s_ws_lock, portMAX_DELAY);
        memcpy(snap, s_ws_clients, sizeof(snap));
        xSemaphoreGive(s_ws_lock);

        int live = 0;
        for (int i = 0; i < NEXUS_WS_MAX_CLIENTS; ++i) {
            if (snap[i].fd >= 0) {
                live++;
            }
        }
        if (live == 0) {
            continue;
        }

        json_builder_t b;
        if (!json_builder_init(&b, 640)) {
            continue;
        }
        build_status_full(&b);
        size_t len = 0;
        const char *payload = json_builder_str(&b, &len);

        httpd_ws_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.type = HTTPD_WS_TYPE_TEXT;
        frame.payload = (uint8_t *)payload;
        frame.len = len;

        for (int i = 0; i < NEXUS_WS_MAX_CLIENTS; ++i) {
            int fd = snap[i].fd;
            if (fd < 0) {
                continue;
            }
            /* Still a WebSocket, and not a recycled descriptor? */
            if (httpd_ws_get_fd_info(server, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
                ws_remove_fd(fd);
                continue;
            }
#if NEXUS_ENABLE_AUTH
            /* Session still alive? Checked without sliding the expiry, so an
             * open tab does not keep itself logged in forever. */
            if (!auth_peek_session(snap[i].token)) {
                ESP_LOGI(TAG, "WS client fd %d dropped (session ended)", fd);
                httpd_sess_trigger_close(server, fd);
                ws_remove_fd(fd);
                continue;
            }
#endif
            if (httpd_ws_send_frame_async(server, fd, &frame) != ESP_OK) {
                ws_remove_fd(fd);
            }
        }
        json_builder_free(&b);
    }
}

/* ================================================================== */
/* 404 fallback (SPA-friendly)                                         */
/* ================================================================== */
static esp_err_t h_404(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    if (req->method == HTTP_GET) {
        /* Serve the app shell so client-side navigation / refresh works. */
        return send_static(req, index_html_start, index_html_end, "text/html");
    }
    return send_json_msg(req, 404, false, "not found");
}

/* ================================================================== */
/* Registration                                                        */
/* ================================================================== */
static void register_handlers(void)
{
    const httpd_uri_t uris[] = {
        { .uri = "/",            .method = HTTP_GET,  .handler = h_root },
        { .uri = "/login",       .method = HTTP_GET,  .handler = h_login_page },
        { .uri = "/css/style.css", .method = HTTP_GET, .handler = h_css },
        { .uri = "/js/app.js",   .method = HTTP_GET,  .handler = h_js },
        { .uri = "/api/login",   .method = HTTP_POST, .handler = h_api_login },
        { .uri = "/api/logout",  .method = HTTP_POST, .handler = h_api_logout },
        { .uri = "/api/session", .method = HTTP_GET,  .handler = h_api_session },
        { .uri = "/api/status",  .method = HTTP_GET,  .handler = h_api_status },
        { .uri = "/api/config",  .method = HTTP_GET,  .handler = h_api_config_get },
        { .uri = "/api/config",  .method = HTTP_POST, .handler = h_api_config_post },
        { .uri = "/api/reboot",  .method = HTTP_POST, .handler = h_api_reboot },
        { .uri = "/api/calibrate", .method = HTTP_POST, .handler = h_api_calibrate },
        { .uri = "/api/factory-reset", .method = HTTP_POST, .handler = h_api_factory_reset },
        { .uri = "/api/history", .method = HTTP_GET,  .handler = h_api_history },
        { .uri = "/api/logs",    .method = HTTP_GET,  .handler = h_api_logs_json },
        { .uri = "/api/logs.csv", .method = HTTP_GET, .handler = h_api_logs_csv },
        { .uri = "/api/ota",     .method = HTTP_POST, .handler = h_api_ota },
        { .uri = "/api/ota/status", .method = HTTP_GET, .handler = h_api_ota_status },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); ++i) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }

    httpd_uri_t ws = {
        .uri = "/live",
        .method = HTTP_GET,
        .handler = h_ws,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_server, &ws);

    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, h_404);
}

esp_err_t webserver_start(void)
{
    if (s_ws_lock == NULL) {
        s_ws_lock = xSemaphoreCreateMutex();
        if (s_ws_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    for (int i = 0; i < NEXUS_WS_MAX_CLIENTS; ++i) {
        s_ws_clients[i].fd = -1;
        s_ws_clients[i].token[0] = '\0';
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = NEXUS_HTTP_PORT;
    config.max_uri_handlers = 24;
    config.stack_size       = 8192;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 15;
    config.send_wait_timeout = 15;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }
    register_handlers();

    s_ws_task_stop = false;
    if (xTaskCreate(ws_push_task, "nexus_wspush", 4096, NULL, 4, &s_ws_task) != pdPASS) {
        ESP_LOGE(TAG, "failed to start WebSocket push task");
        httpd_stop(s_server);
        s_server = NULL;
        return ESP_ERR_NO_MEM;
    }

    LOG_EVENT("Web server started on port %d", NEXUS_HTTP_PORT);
    return ESP_OK;
}

void webserver_stop(void)
{
    /* Stop the push task before the server goes away, otherwise it keeps
     * running against a freed handle. */
    if (s_ws_task) {
        s_ws_task_stop = true;
        /* Give it long enough to notice on its own and exit cleanly. */
        for (int i = 0; i < 20 && s_ws_task_stop; ++i) {
            vTaskDelay(pdMS_TO_TICKS(NEXUS_WS_PUSH_INTERVAL_MS));
        }
        if (s_ws_task_stop) {
            vTaskDelete(s_ws_task);
        }
        s_ws_task = NULL;
        s_ws_task_stop = false;
    }

    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }

    if (s_ws_lock) {
        xSemaphoreTake(s_ws_lock, portMAX_DELAY);
        for (int i = 0; i < NEXUS_WS_MAX_CLIENTS; ++i) {
            s_ws_clients[i].fd = -1;
            s_ws_clients[i].token[0] = '\0';
        }
        xSemaphoreGive(s_ws_lock);
    }
}
