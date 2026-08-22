/**
 * @file logger.h
 * @brief Event logger with a RAM ring buffer and NVS persistence.
 *
 * Captures human-meaningful events (motion detected, calibration, reboot,
 * errors, ...) in a bounded ring. The ring is periodically snapshotted to NVS
 * so recent history survives a reboot, and can be exported as JSON (dashboard)
 * or CSV (download). This is distinct from ESP_LOGx console logging.
 */
#ifndef NEXUS_LOGGER_H
#define NEXUS_LOGGER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "app_config.h"
#include "json.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Severity / category of a logged event. */
typedef enum {
    LOG_LVL_INFO = 0,
    LOG_LVL_EVENT,     /**< notable sensing event (motion, presence) */
    LOG_LVL_WARN,
    LOG_LVL_ERROR,
} log_level_t;

/** A single persisted log record. POD so it can live in a ring buffer. */
typedef struct {
    uint32_t seq;                       /**< monotonic sequence number */
    uint32_t uptime_ms;                 /**< ms since boot when logged */
    uint8_t  level;                     /**< ::log_level_t */
    char     msg[NEXUS_LOG_MSG_LEN];    /**< message text */
} log_entry_t;

/** Initialise the logger and restore any persisted entries. */
esp_err_t logger_init(void);

/** Spawn the background task that persists the ring buffer periodically. */
esp_err_t logger_start_task(void);

/** Append a formatted entry at the given @p level. */
void logger_log(log_level_t level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/** Convenience wrappers. */
#define LOG_INFO(...)  logger_log(LOG_LVL_INFO,  __VA_ARGS__)
#define LOG_EVENT(...) logger_log(LOG_LVL_EVENT, __VA_ARGS__)
#define LOG_WARN(...)  logger_log(LOG_LVL_WARN,  __VA_ARGS__)
#define LOG_ERR(...)   logger_log(LOG_LVL_ERROR, __VA_ARGS__)

/** Copy up to @p max entries (oldest first) into @p out; returns the count. */
size_t logger_snapshot(log_entry_t *out, size_t max);

/** @return Number of entries currently held in the ring. */
size_t logger_count(void);

/**
 * @brief Copy one entry out by position, so a reader does not need an array the
 *        size of the whole ring.
 *
 * @param index 0 for the oldest held entry, logger_count()-1 for the newest.
 * @param out   Destination for one entry.
 * @return true if an entry was copied, false if @p index is out of range.
 *
 * Positions shift when new entries arrive, so a walk that spans many calls can
 * repeat or miss one entry. Compare @ref log_entry_t::seq against the last one
 * emitted if that would be visible in the output.
 */
bool logger_peek(size_t index, log_entry_t *out);

/** @return string name of a level, e.g. "EVENT". */
const char *logger_level_str(log_level_t level);

/** Serialise recent entries as a JSON array under key "logs" in @p b. */
void logger_to_json(json_builder_t *b);

/** Force an immediate persist of the ring buffer to NVS. */
esp_err_t logger_persist(void);

/** Drop all entries (RAM and persisted). */
void logger_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_LOGGER_H */
