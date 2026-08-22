/**
 * @file auth.c
 * @brief Authentication / session / rate-limit implementation.
 */
#include "auth.h"
#include "app_config.h"
#include "config.h"
#include "utils.h"
#include "logger.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "auth";

typedef struct {
    bool     used;
    char     token[NEXUS_SESSION_TOKEN_HEX_LEN + 1];
    char     csrf[NEXUS_SESSION_TOKEN_HEX_LEN + 1];
    uint32_t created_ms;
    uint32_t last_ms;
} session_t;

typedef struct {
    bool     used;
    char     ip[46];        /* room for IPv6 text form */
    uint32_t window_start_ms;
    uint16_t attempts;
    uint32_t lockout_until_ms;
} ratelimit_t;

static session_t   s_sessions[NEXUS_MAX_SESSIONS];
static ratelimit_t s_limits[NEXUS_WS_MAX_CLIENTS + 4];
static SemaphoreHandle_t s_lock;

esp_err_t auth_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(s_sessions, 0, sizeof(s_sessions));
    memset(s_limits, 0, sizeof(s_limits));
    return ESP_OK;
}

static bool session_expired(const session_t *s, uint32_t now)
{
    return (now - s->last_ms) > (uint32_t)(NEXUS_SESSION_TTL_S * 1000U);
}

/* ------------------------------------------------------------------ */
/* Rate limiting                                                      */
/* ------------------------------------------------------------------ */
#define LIMIT_SLOTS (sizeof(s_limits) / sizeof(s_limits[0]))

/* Bucket used when the peer address could not be resolved. Anonymous requests
 * share one budget rather than being exempt: returning "not limited" for an
 * unknown IP would leave that path unthrottled forever. */
static const char *k_unknown_ip = "?unknown";

static const char *limit_key(const char *ip)
{
    return (ip != NULL && ip[0] != '\0') ? ip : k_unknown_ip;
}

/** Pure lookup. Returns NULL when this key has no slot yet. */
static ratelimit_t *limit_find_locked(const char *key)
{
    for (size_t i = 0; i < LIMIT_SLOTS; ++i) {
        if (s_limits[i].used && strcmp(s_limits[i].ip, key) == 0) {
            return &s_limits[i];
        }
    }
    return NULL;
}

/** Lookup, or claim a slot for @p key.
 *
 * Returns NULL when every slot is occupied by an active lockout. Callers must
 * treat that as "limited": handing back a shared slot would let an attacker
 * fill the table and then clear somebody else's lockout, which is exactly what
 * reusing s_limits[0] unconditionally used to do.
 */
static ratelimit_t *limit_acquire_locked(const char *key)
{
    ratelimit_t *found = limit_find_locked(key);
    if (found) {
        return found;
    }

    uint32_t now = utils_millis();
    ratelimit_t *victim = NULL;
    uint32_t oldest = 0xFFFFFFFFu;

    for (size_t i = 0; i < LIMIT_SLOTS; ++i) {
        ratelimit_t *rl = &s_limits[i];
        if (!rl->used) {
            victim = rl;
            break;
        }
        /* Never evict a slot that is still serving a lockout. */
        if (rl->lockout_until_ms != 0 && now < rl->lockout_until_ms) {
            continue;
        }
        if (rl->window_start_ms <= oldest) {
            oldest = rl->window_start_ms;
            victim = rl;
        }
    }
    if (victim == NULL) {
        return NULL;
    }

    memset(victim, 0, sizeof(*victim));
    victim->used = true;
    utils_strlcpy(victim->ip, key, sizeof(victim->ip));
    victim->window_start_ms = now;
    return victim;
}

bool auth_is_rate_limited(const char *client_ip)
{
    const char *key = limit_key(client_ip);
    bool limited = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* Look up only. A read-only check must not consume a slot, or simply loading
     * the login page would churn the table. */
    ratelimit_t *rl = limit_find_locked(key);
    if (rl != NULL) {
        uint32_t now = utils_millis();
        if (rl->lockout_until_ms != 0 && now < rl->lockout_until_ms) {
            limited = true;
        }
    }
    xSemaphoreGive(s_lock);
    return limited;
}

/* Record an attempt outcome. Returns true if now locked out. */
static bool record_attempt_locked(const char *client_ip, bool success)
{
    ratelimit_t *rl = limit_acquire_locked(limit_key(client_ip));
    if (rl == NULL) {
        /* Table saturated with active lockouts. Fail closed. */
        return true;
    }
    uint32_t now = utils_millis();

    if (success) {
        rl->attempts = 0;
        rl->lockout_until_ms = 0;
        rl->window_start_ms = now;
        return false;
    }

    if ((now - rl->window_start_ms) > (uint32_t)(NEXUS_LOGIN_WINDOW_S * 1000U)) {
        rl->window_start_ms = now;
        rl->attempts = 0;
    }
    rl->attempts++;
    if (rl->attempts >= NEXUS_LOGIN_MAX_ATTEMPTS) {
        rl->lockout_until_ms = now + (uint32_t)(NEXUS_LOGIN_LOCKOUT_S * 1000U);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Credentials                                                        */
/* ------------------------------------------------------------------ */
static bool credentials_ok(const char *user, const char *pass)
{
    nexus_config_t cfg;
    config_get(&cfg);

    /* Hash unconditionally, even for a wrong username. Returning early on the
     * username would make a bad username measurably faster to reject than a bad
     * password, which leaks whether an account name is right. */
    bool user_ok = utils_consttime_equal(user, cfg.admin_user);

    char salted[192];
    snprintf(salted, sizeof(salted), "%s%s", cfg.admin_salt, pass);
    char hash[65];
    bool hash_ok = utils_sha256_hex(salted, strlen(salted), hash) &&
                   utils_consttime_equal(hash, cfg.admin_pass_hash);

    return user_ok && hash_ok;
}

/* ------------------------------------------------------------------ */
/* Sessions                                                           */
/* ------------------------------------------------------------------ */
static session_t *alloc_session_locked(void)
{
    uint32_t now = utils_millis();
    session_t *victim = NULL;
    uint32_t oldest = 0xFFFFFFFFu;

    for (size_t i = 0; i < NEXUS_MAX_SESSIONS; ++i) {
        session_t *s = &s_sessions[i];
        if (!s->used || session_expired(s, now)) {
            memset(s, 0, sizeof(*s));
            return s;
        }
        if (s->last_ms < oldest) {
            oldest = s->last_ms;
            victim = s;
        }
    }
    /* All slots active: evict the least-recently-used. */
    if (victim) {
        memset(victim, 0, sizeof(*victim));
    }
    return victim;
}

bool auth_login(const char *user, const char *pass, const char *client_ip,
                char *token_out, size_t token_sz,
                char *csrf_out, size_t csrf_sz)
{
    if (user == NULL || pass == NULL || token_out == NULL || csrf_out == NULL) {
        return false;
    }
    if (auth_is_rate_limited(client_ip)) {
        LOG_WARN("Login blocked (rate limit) from %s", client_ip ? client_ip : "?");
        return false;
    }

    bool ok = credentials_ok(user, pass);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool locked = record_attempt_locked(client_ip, ok);
    session_t *s = NULL;
    if (ok) {
        s = alloc_session_locked();
        if (s) {
            utils_random_hex(s->token, NEXUS_SESSION_TOKEN_HEX_LEN);
            utils_random_hex(s->csrf, NEXUS_SESSION_TOKEN_HEX_LEN);
            s->created_ms = utils_millis();
            s->last_ms = s->created_ms;
            s->used = true;
            utils_strlcpy(token_out, s->token, token_sz);
            utils_strlcpy(csrf_out, s->csrf, csrf_sz);
        }
    }
    xSemaphoreGive(s_lock);

    if (ok && s) {
        LOG_EVENT("Login OK for '%s' from %s", user, client_ip ? client_ip : "?");
        return true;
    }
    if (locked) {
        LOG_WARN("Login lockout engaged for %s", client_ip ? client_ip : "?");
    } else {
        LOG_WARN("Login FAILED for '%s' from %s", user, client_ip ? client_ip : "?");
    }
    return false;
}

bool auth_validate_session(const char *token, char *csrf_out, size_t csrf_sz)
{
    if (token == NULL || token[0] == '\0') {
        return false;
    }
    bool valid = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t now = utils_millis();
    for (size_t i = 0; i < NEXUS_MAX_SESSIONS; ++i) {
        session_t *s = &s_sessions[i];
        if (s->used && !session_expired(s, now) &&
            utils_consttime_equal(s->token, token)) {
            s->last_ms = now;         /* sliding expiry */
            if (csrf_out) {
                utils_strlcpy(csrf_out, s->csrf, csrf_sz);
            }
            valid = true;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return valid;
}

bool auth_peek_session(const char *token)
{
    if (token == NULL || token[0] == '\0') {
        return false;
    }
    bool valid = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t now = utils_millis();
    for (size_t i = 0; i < NEXUS_MAX_SESSIONS; ++i) {
        session_t *s = &s_sessions[i];
        if (s->used && !session_expired(s, now) &&
            utils_consttime_equal(s->token, token)) {
            /* Deliberately not touching s->last_ms. */
            valid = true;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return valid;
}

bool auth_validate_csrf(const char *token, const char *csrf)
{
    if (token == NULL || csrf == NULL) {
        return false;
    }
    bool ok = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t now = utils_millis();
    for (size_t i = 0; i < NEXUS_MAX_SESSIONS; ++i) {
        session_t *s = &s_sessions[i];
        if (s->used && !session_expired(s, now) &&
            utils_consttime_equal(s->token, token)) {
            ok = utils_consttime_equal(s->csrf, csrf);
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return ok;
}

void auth_logout(const char *token)
{
    if (token == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < NEXUS_MAX_SESSIONS; ++i) {
        session_t *s = &s_sessions[i];
        if (s->used && utils_consttime_equal(s->token, token)) {
            memset(s, 0, sizeof(*s));
            break;
        }
    }
    xSemaphoreGive(s_lock);
}

int auth_active_sessions(void)
{
    int n = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t now = utils_millis();
    for (size_t i = 0; i < NEXUS_MAX_SESSIONS; ++i) {
        if (s_sessions[i].used && !session_expired(&s_sessions[i], now)) {
            n++;
        }
    }
    xSemaphoreGive(s_lock);
    return n;
}
