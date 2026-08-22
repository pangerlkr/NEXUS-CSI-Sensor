/**
 * @file auth.h
 * @brief Authentication, session management, CSRF and login rate limiting.
 *
 * Credentials are verified against the salted SHA-256 hash held in the device
 * configuration. A successful login mints a random session token plus a CSRF
 * token; the token is returned to the browser as an HttpOnly cookie and the
 * CSRF token is echoed in a header for mutating requests. Sessions expire after
 * a configurable TTL. Repeated failed logins from one client are throttled.
 */
#ifndef NEXUS_AUTH_H
#define NEXUS_AUTH_H

#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise session table and rate-limit state. */
esp_err_t auth_init(void);

/**
 * @brief Attempt a login.
 * @param user       Supplied username.
 * @param pass       Supplied plaintext password.
 * @param client_ip  Client address (for rate limiting), may be "".
 * @param token_out  Receives the session token (>= NEXUS_SESSION_TOKEN_HEX_LEN+1).
 * @param token_sz   Size of @p token_out.
 * @param csrf_out   Receives the CSRF token (>= NEXUS_SESSION_TOKEN_HEX_LEN+1).
 * @param csrf_sz    Size of @p csrf_out.
 * @return true on success, false on bad credentials or if rate-limited.
 */
bool auth_login(const char *user, const char *pass, const char *client_ip,
                char *token_out, size_t token_sz,
                char *csrf_out, size_t csrf_sz);

/**
 * @brief Validate a session token and refresh its last-seen time.
 * @param csrf_out If non-NULL and the session is valid, receives the CSRF token.
 * @return true if the session exists and has not expired.
 */
bool auth_validate_session(const char *token, char *csrf_out, size_t csrf_sz);

/**
 * @brief Validate a session token WITHOUT refreshing its last-seen time.
 *
 * Use this for background checks that are not driven by user activity, such as
 * revalidating an open WebSocket before each telemetry push. Calling
 * auth_validate_session() there would slide the expiry on every push and the
 * session would never time out while a browser tab stayed open.
 *
 * @return true if the session exists and has not expired.
 */
bool auth_peek_session(const char *token);

/** @return true if @p csrf matches the CSRF token bound to @p token. */
bool auth_validate_csrf(const char *token, const char *csrf);

/** Invalidate the session identified by @p token (idempotent). */
void auth_logout(const char *token);

/** @return true if @p client_ip is currently locked out from logging in. */
bool auth_is_rate_limited(const char *client_ip);

/** @return number of currently active (non-expired) sessions. */
int auth_active_sessions(void);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_AUTH_H */
