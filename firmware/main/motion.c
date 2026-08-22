/**
 * @file motion.c
 * @brief Signal processing + detection state machine implementation.
 */
#include "motion.h"
#include "app_config.h"
#include "config.h"
#include "csi.h"
#include "wifi.h"
#include "logger.h"
#include "ringbuffer.h"
#include "utils.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"
#include "esp_log.h"

static const char *TAG = "motion";

static motion_result_t   s_result;
static SemaphoreHandle_t s_result_lock;
static ringbuffer_t      s_history;

/* State-machine working state (owned by the motion task). */
static float    s_baseline;          /* adaptive noise floor (variance units) */
static bool     s_baseline_seeded;
static float    s_score_lpf;         /* low-pass filtered score */
static float    s_activity_ema;      /* slow activity EMA (0..100) */
static int      s_level;             /* current level 0..3 */
static int      s_rise_ctr;
static int      s_fall_ctr;
static volatile bool s_force_calibrate;

/* ================================================================== */
/* Signal processing primitives                                        */
/* ================================================================== */

/** In-place-safe median filter with an odd @p kernel window. */
static void sp_median_filter(const float *in, float *out, size_t n, size_t kernel)
{
    if (kernel < 3) {
        memcpy(out, in, n * sizeof(float));
        return;
    }
    size_t half = kernel / 2;
    float window[NEXUS_MEDIAN_KERNEL];
    if (kernel > NEXUS_MEDIAN_KERNEL) {
        kernel = NEXUS_MEDIAN_KERNEL;
        half = kernel / 2;
    }
    for (size_t i = 0; i < n; ++i) {
        size_t w = 0;
        for (size_t k = 0; k < kernel; ++k) {
            long idx = (long)i + (long)k - (long)half;
            if (idx < 0) idx = 0;
            if (idx >= (long)n) idx = (long)n - 1;
            window[w++] = in[idx];
        }
        /* insertion sort of the small window */
        for (size_t a = 1; a < w; ++a) {
            float key = window[a];
            long b = (long)a - 1;
            while (b >= 0 && window[b] > key) {
                window[b + 1] = window[b];
                b--;
            }
            window[b + 1] = key;
        }
        out[i] = window[w / 2];
    }
}

/** Trailing moving average with window @p win. */
static void sp_moving_average(const float *in, float *out, size_t n, size_t win)
{
    if (win < 1) win = 1;
    float acc = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        acc += in[i];
        if (i >= win) {
            acc -= in[i - win];
        }
        size_t denom = (i + 1 < win) ? (i + 1) : win;
        out[i] = acc / (float)denom;
    }
}

/** Arithmetic mean. */
static float sp_mean(const float *x, size_t n)
{
    if (n == 0) return 0.0f;
    float s = 0.0f;
    for (size_t i = 0; i < n; ++i) s += x[i];
    return s / (float)n;
}

/** Population variance. */
static float sp_variance(const float *x, size_t n)
{
    if (n < 2) return 0.0f;
    float m = sp_mean(x, n);
    float s = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        float d = x[i] - m;
        s += d * d;
    }
    return s / (float)n;
}

/** First-order IIR low-pass (EMA). */
static float sp_lowpass(float prev, float sample, float alpha)
{
    return prev + alpha * (sample - prev);
}

/**
 * Self-scaling normalisation of the movement feature into 0..1.
 * Divides the above-baseline excess by a scale proportional to the noise
 * floor, giving a soft-saturating response that adapts to the environment.
 */
static float sp_normalise(float feature, float baseline)
{
    float delta = feature - baseline;
    if (delta <= 0.0f) {
        return 0.0f;
    }
    float scale = baseline * NEXUS_NORM_SENSITIVITY + NEXUS_NORM_FLOOR;
    return utils_clampf(delta / (delta + scale), 0.0f, 1.0f);
}

/* ================================================================== */
/* State machine                                                       */
/* ================================================================== */

static int classify(float score, const nexus_config_t *cfg, float hyst)
{
    if (score >= (cfg->high_motion_threshold - hyst)) return MOTION_STATE_HIGH_MOTION;
    if (score >= (cfg->motion_threshold      - hyst)) return MOTION_STATE_MOTION;
    if (score >= (cfg->presence_threshold     - hyst)) return MOTION_STATE_PRESENCE;
    return MOTION_STATE_IDLE;
}

const char *motion_state_str(motion_state_t s)
{
    switch (s) {
        case MOTION_STATE_IDLE:        return "Idle";
        case MOTION_STATE_PRESENCE:    return "Presence";
        case MOTION_STATE_MOTION:      return "Motion";
        case MOTION_STATE_HIGH_MOTION: return "High Motion";
        default:                       return "?";
    }
}

/* ================================================================== */
/* Signal quality                                                      */
/* ================================================================== */
static float compute_signal_quality(const csi_metrics_t *m, const nexus_config_t *cfg)
{
    float rssi_q = utils_mapf((float)m->rssi,
                              (float)NEXUS_RSSI_MIN_DBM, (float)NEXUS_RSSI_MAX_DBM,
                              0.0f, 100.0f);
    rssi_q = utils_clampf(rssi_q, 0.0f, 100.0f);

    float rate_q = utils_mapf(m->packets_per_sec,
                              0.0f, (float)cfg->sampling_rate_hz,
                              0.0f, 100.0f);
    rate_q = utils_clampf(rate_q, 0.0f, 100.0f);

    /* Weight link strength and data rate equally. */
    return 0.5f * rssi_q + 0.5f * rate_q;
}

/* ================================================================== */
/* Core processing cycle                                               */
/* ================================================================== */
static void motion_process(void)
{
    static csi_sample_t   raw[NEXUS_CSI_WINDOW_SIZE];
    static float          amp[NEXUS_CSI_WINDOW_SIZE];
    static float          med[NEXUS_CSI_WINDOW_SIZE];
    static float          smo[NEXUS_CSI_WINDOW_SIZE];

    size_t n = csi_snapshot_samples(raw, NEXUS_CSI_WINDOW_SIZE);
    if (n < (NEXUS_CSI_WINDOW_SIZE / 2)) {
        return;   /* not enough data yet */
    }

    for (size_t i = 0; i < n; ++i) {
        amp[i] = raw[i].amp_mean;
    }

    /* 1) Median filter removes impulsive spikes (noise reduction). */
    sp_median_filter(amp, med, n, NEXUS_MEDIAN_KERNEL);
    /* 2) Moving average further smooths the amplitude track. */
    sp_moving_average(med, smo, n, NEXUS_MOVAVG_LEN);
    /* 3) Temporal variance of the smoothed track is our movement feature. */
    float feature = sp_variance(smo, n);

    nexus_config_t cfg;
    config_get(&cfg);

    /* 4) Adaptive baseline: seed once, then track slowly while idle. */
    if (!s_baseline_seeded || s_force_calibrate) {
        s_baseline = feature;
        s_baseline_seeded = true;
        s_force_calibrate = false;
        s_level = MOTION_STATE_IDLE;
        s_rise_ctr = s_fall_ctr = 0;
        s_score_lpf = 0.0f;
        s_activity_ema = 0.0f;
        LOG_EVENT("Baseline calibrated (var=%.2f)", (double)s_baseline);
    } else {
        bool idle = (s_level == MOTION_STATE_IDLE);
        if (cfg.auto_calibration && (idle || !NEXUS_BASELINE_FREEZE_ON_MOTION)) {
            s_baseline = sp_lowpass(s_baseline, feature, NEXUS_BASELINE_ALPHA);
        }
    }

    /* 5) Normalise into 0..1 and 6) low-pass smooth the score. */
    float raw_score = sp_normalise(feature, s_baseline);
    s_score_lpf = sp_lowpass(s_score_lpf, raw_score, NEXUS_LPF_ALPHA);
    float score = utils_clampf(s_score_lpf, 0.0f, 1.0f);

    /* 7) Debounced, hysteretic state machine. */
    int level_up = classify(score, &cfg, 0.0f);
    int level_dn = classify(score, &cfg, NEXUS_THRESHOLD_HYSTERESIS);
    int prev = s_level;

    if (level_up > s_level) {
        s_rise_ctr++;
        s_fall_ctr = 0;
        if (s_rise_ctr >= NEXUS_DEBOUNCE_RISE) {
            s_level = level_up;
            s_rise_ctr = 0;
        }
    } else if (level_dn < s_level) {
        s_fall_ctr++;
        s_rise_ctr = 0;
        if (s_fall_ctr >= NEXUS_DEBOUNCE_FALL) {
            s_level = level_dn;
            s_fall_ctr = 0;
        }
    } else {
        s_rise_ctr = 0;
        s_fall_ctr = 0;
    }

    /* Derived indicators. */
    float intensity = score * 100.0f;
    s_activity_ema = sp_lowpass(s_activity_ema, intensity, NEXUS_ACTIVITY_ALPHA);

    csi_metrics_t m;
    csi_get_metrics(&m);
    float quality = compute_signal_quality(&m, &cfg);

    uint32_t now = utils_millis();
    bool changed = (s_level != prev);

    /* Publish the result. */
    xSemaphoreTake(s_result_lock, portMAX_DELAY);
    if (changed) {
        s_result.last_change_ms = now;
        s_result.state_since_ms = now;
    }
    s_result.state            = (motion_state_t)s_level;
    s_result.presence         = (s_level >= MOTION_STATE_PRESENCE);
    s_result.motion_score     = score;
    s_result.motion_intensity = intensity;
    s_result.activity_level   = s_activity_ema;
    s_result.variance         = feature;
    s_result.baseline         = s_baseline;
    s_result.signal_quality   = quality;
    motion_state_t published_state = s_result.state;
    xSemaphoreGive(s_result_lock);

    if (changed) {
        LOG_EVENT("State -> %s (score=%.2f)",
                  motion_state_str(published_state), (double)score);
    }

    /* Append to rolling history at ~1 Hz. */
    static uint32_t last_hist_ms;
    if (now - last_hist_ms >= 1000U) {
        last_hist_ms = now;
        history_point_t hp = {
            .t_ms     = now,
            .score    = score,
            .variance = feature,
            .rssi     = m.rssi,
            .state    = (uint8_t)s_level,
        };
        ringbuffer_push(&s_history, &hp);
    }
}

/* ================================================================== */
/* Task + API                                                          */
/* ================================================================== */
static void motion_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    for (;;) {
        esp_task_wdt_reset();

        nexus_config_t cfg;
        config_get(&cfg);
        uint32_t period_ms = 1000U / (cfg.sampling_rate_hz ? cfg.sampling_rate_hz
                                                           : NEXUS_SAMPLING_RATE_DEFAULT);
        if (period_ms < 10) period_ms = 10;      /* cap CPU usage */
        if (period_ms > NEXUS_WDT_FEED_MS) period_ms = NEXUS_WDT_FEED_MS;

        motion_process();
        vTaskDelay(pdMS_TO_TICKS(period_ms));
    }
}

esp_err_t motion_init(void)
{
    s_result_lock = xSemaphoreCreateMutex();
    if (s_result_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_result, 0, sizeof(s_result));
    s_result.state_since_ms = utils_millis();

    esp_err_t err = ringbuffer_init(&s_history, sizeof(history_point_t),
                                    NEXUS_HISTORY_CAPACITY);
    if (err != ESP_OK) {
        vSemaphoreDelete(s_result_lock);
        s_result_lock = NULL;
        return err;
    }
    LOG_INFO("Motion engine initialised");
    return ESP_OK;
}

esp_err_t motion_start_task(void)
{
    BaseType_t ok = xTaskCreate(motion_task, "nexus_motion",
                                NEXUS_TASK_STACK_MOTION, NULL,
                                NEXUS_TASK_PRIO_MOTION, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void motion_get_result(motion_result_t *out)
{
    if (out == NULL) return;
    xSemaphoreTake(s_result_lock, portMAX_DELAY);
    *out = s_result;
    xSemaphoreGive(s_result_lock);
}

void motion_calibrate(void)
{
    s_force_calibrate = true;
    LOG_EVENT("Calibration requested");
}

size_t motion_history_snapshot(history_point_t *out, size_t max)
{
    return ringbuffer_snapshot(&s_history, out, max);
}
