/**
 * @file motion.h
 * @brief CSI-based motion / presence detection engine.
 *
 * Consumes CSI feature samples, applies a signal-processing pipeline
 * (median filter -> moving average -> temporal variance -> adaptive-baseline
 * normalisation -> low-pass smoothing) and drives a debounced, hysteretic
 * state machine over Idle / Presence / Motion / High-Motion. Results are
 * published for the web, display and logging layers, and a rolling history is
 * retained for charting.
 */
#ifndef NEXUS_MOTION_H
#define NEXUS_MOTION_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Detection state, ordered by increasing activity. */
typedef enum {
    MOTION_STATE_IDLE = 0,
    MOTION_STATE_PRESENCE,
    MOTION_STATE_MOTION,
    MOTION_STATE_HIGH_MOTION,
} motion_state_t;

/** Published detection result. */
typedef struct {
    motion_state_t state;
    bool           presence;         /**< true when state >= PRESENCE */
    float          motion_score;     /**< smoothed, normalised 0..1 */
    float          motion_intensity; /**< 0..100 %, instantaneous */
    float          activity_level;   /**< 0..100 %, slow-EMA sustained */
    float          variance;         /**< raw temporal-variance feature */
    float          baseline;         /**< adaptive noise baseline */
    float          signal_quality;   /**< 0..100 % (RSSI + packet rate) */
    uint32_t       last_change_ms;   /**< uptime of last state change */
    uint32_t       state_since_ms;   /**< uptime the current state began */
} motion_result_t;

/** One point in the rolling history (for charts / /api/history). */
typedef struct {
    uint32_t t_ms;      /**< uptime in ms */
    float    score;     /**< motion score 0..1 */
    float    variance;  /**< raw variance feature */
    int8_t   rssi;      /**< RSSI at the time */
    uint8_t  state;     /**< ::motion_state_t */
} history_point_t;

/** Initialise the motion engine. Call after csi_init(). */
esp_err_t motion_init(void);

/** Spawn the motion-processing task. */
esp_err_t motion_start_task(void);

/** Copy the latest result into @p out (thread-safe). */
void motion_get_result(motion_result_t *out);

/**
 * @brief Force a baseline recalibration from the current sample window.
 *        Resets the state machine to Idle.
 */
void motion_calibrate(void);

/** @return human-readable state name. */
const char *motion_state_str(motion_state_t s);

/** Copy up to @p max history points (oldest first) into @p out. */
size_t motion_history_snapshot(history_point_t *out, size_t max);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_MOTION_H */
