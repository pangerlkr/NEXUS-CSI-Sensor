/**
 * @file app_config.h
 * @brief Central compile-time configuration for the NEXUS CSI Sensor.
 *
 * This header is the single source of truth for every tunable constant,
 * default value and threshold used across the firmware. There are no magic
 * numbers scattered through the codebase: every module includes this file and
 * references the named constants below. Runtime-tunable values (thresholds,
 * sampling rate, credentials, ...) additionally have persisted counterparts in
 * @ref nexus_config_t (see config.h) that override these defaults from NVS.
 *
 * @copyright MIT License. See LICENSE at the repository root.
 */
#ifndef NEXUS_APP_CONFIG_H
#define NEXUS_APP_CONFIG_H

/* =========================================================================
 * Firmware identity
 * ========================================================================= */
#define NEXUS_FW_NAME                "NEXUS-CSI-Sensor"
#define NEXUS_FW_VERSION             "1.0.0"
#define NEXUS_FW_AUTHOR              "NEXUS Sensing Project"

/* =========================================================================
 * Feature toggles
 * ========================================================================= */
/** Set to 1 to build the ST7735 TFT display driver and display task. */
#ifndef NEXUS_ENABLE_DISPLAY
#define NEXUS_ENABLE_DISPLAY         1
#endif

/** Set to 1 to generate ICMP traffic to the gateway so that CSI keeps
 *  flowing even when the network is otherwise idle. */
#ifndef NEXUS_ENABLE_TRAFFIC_GEN
#define NEXUS_ENABLE_TRAFFIC_GEN     1
#endif

/** Set to 1 to require login for the dashboard and all mutating endpoints. */
#ifndef NEXUS_ENABLE_AUTH
#define NEXUS_ENABLE_AUTH            1
#endif

/* =========================================================================
 * Networking defaults
 * ========================================================================= */
#define NEXUS_DEFAULT_DEVICE_NAME    "nexus-csi"
#define NEXUS_DEFAULT_WIFI_SSID      ""
#define NEXUS_DEFAULT_WIFI_PASS      ""

/** SoftAP fallback used when no STA credentials are stored or STA fails. */
#define NEXUS_AP_SSID_PREFIX         "NEXUS-CSI-Setup"
#define NEXUS_AP_PASSWORD            "nexus1234"   /* >= 8 chars for WPA2 */
#define NEXUS_AP_CHANNEL             6
#define NEXUS_AP_MAX_CONN            4

#define NEXUS_WIFI_CONNECT_RETRY_MAX 8
#define NEXUS_WIFI_RETRY_BACKOFF_MS  2000

/* =========================================================================
 * CSI acquisition
 * ========================================================================= */
/** Maximum number of CSI complex pairs we will read from a single packet.
 *  HT-LTF for a 20 MHz channel yields up to 64 subcarriers (128 bytes). */
#define NEXUS_CSI_MAX_SUBCARRIERS    64

/** Depth of the circular buffer holding per-packet CSI feature samples. */
#define NEXUS_CSI_RING_CAPACITY      256

/** Sliding window (samples) used by the motion engine for statistics. */
#define NEXUS_CSI_WINDOW_SIZE        64

/** How long the CSI receive callback will wait for the sample buffer's mutex
 *  before dropping the sample. The callback runs in the WiFi task, so blocking
 *  there stalls packet reception itself; losing an occasional sample out of 20
 *  per second is the cheaper failure. */
#define NEXUS_CSI_PUSH_TIMEOUT_MS    2

/** Default / min / max packet sampling rate (Hz) for the traffic generator. */
#define NEXUS_SAMPLING_RATE_DEFAULT  20
#define NEXUS_SAMPLING_RATE_MIN      5
#define NEXUS_SAMPLING_RATE_MAX      100

/* =========================================================================
 * Signal processing
 * ========================================================================= */
/** Median filter kernel size (odd). */
#define NEXUS_MEDIAN_KERNEL          5

/** Moving-average length for amplitude smoothing. */
#define NEXUS_MOVAVG_LEN             8

/** Low-pass (EMA) smoothing factor for the motion score, 0..1.
 *  Higher = more responsive, lower = smoother. */
#define NEXUS_LPF_ALPHA              0.30f

/** EMA factor used to track the adaptive noise baseline (very slow). */
#define NEXUS_BASELINE_ALPHA         0.01f

/** Baseline is only updated while the system believes the room is empty,
 *  to avoid learning a moving person as "background". */
#define NEXUS_BASELINE_FREEZE_ON_MOTION  1

/** Self-scaling normalisation of the movement feature into a 0..1 score:
 *  score = delta / (delta + baseline * SENSITIVITY + FLOOR).
 *  Higher SENSITIVITY => less sensitive (needs more movement to saturate). */
#define NEXUS_NORM_SENSITIVITY       4.0f
#define NEXUS_NORM_FLOOR             1.0f

/** EMA factor for the slow "activity level" indicator (sustained motion). */
#define NEXUS_ACTIVITY_ALPHA         0.05f

/** RSSI range (dBm) mapped to 0..100 % for the signal-quality indicator. */
#define NEXUS_RSSI_MIN_DBM           (-90)
#define NEXUS_RSSI_MAX_DBM           (-30)

/* =========================================================================
 * Motion / presence thresholds (defaults; overridable at runtime)
 * ========================================================================= */
/** Normalised motion score (0..1) above which presence is asserted. */
#define NEXUS_PRESENCE_THRESHOLD     0.12f

/** Motion score above which "motion" is asserted. */
#define NEXUS_MOTION_THRESHOLD       0.35f

/** Motion score above which "high motion" is asserted. */
#define NEXUS_HIGH_MOTION_THRESHOLD  0.70f

/** Consecutive windows required to confirm a state (debounce, rising edge). */
#define NEXUS_DEBOUNCE_RISE          3

/** Consecutive windows required to drop a state (debounce, falling edge). */
#define NEXUS_DEBOUNCE_FALL          8

/** Hysteresis subtracted from a threshold when deciding to drop below it. */
#define NEXUS_THRESHOLD_HYSTERESIS   0.05f

/* =========================================================================
 * History / logging
 * ========================================================================= */
/** Number of history points kept for the dashboard chart / /api/history. */
#define NEXUS_HISTORY_CAPACITY       120

/** Number of log entries retained in the RAM ring buffer. */
#define NEXUS_LOG_CAPACITY           64
#define NEXUS_LOG_MSG_LEN            96

/* =========================================================================
 * Security
 * ========================================================================= */
#define NEXUS_DEFAULT_ADMIN_USER     "admin"
#define NEXUS_DEFAULT_ADMIN_PASS     "nexus-admin"   /* change on first login */
#define NEXUS_SESSION_TOKEN_HEX_LEN  32               /* 16 random bytes */
#define NEXUS_SALT_HEX_LEN           16               /* 8 random bytes */
#define NEXUS_SESSION_TTL_S          3600             /* 1 hour */
#define NEXUS_MAX_SESSIONS           4
#define NEXUS_LOGIN_MAX_ATTEMPTS     5                /* per window */
#define NEXUS_LOGIN_WINDOW_S         60
#define NEXUS_LOGIN_LOCKOUT_S        120

/* =========================================================================
 * Display (ST7735, 128x160)
 * ========================================================================= */
#define NEXUS_TFT_WIDTH              128
#define NEXUS_TFT_HEIGHT             160
#define NEXUS_TFT_SPI_HOST           SPI2_HOST
#define NEXUS_TFT_PIN_MOSI           23
#define NEXUS_TFT_PIN_SCLK           18
#define NEXUS_TFT_PIN_CS             5
#define NEXUS_TFT_PIN_DC             2
#define NEXUS_TFT_PIN_RST            4
#define NEXUS_TFT_PIN_BLK            15   /* backlight, PWM-capable */
#define NEXUS_TFT_SPI_CLOCK_HZ       (26 * 1000 * 1000)
#define NEXUS_DEFAULT_BRIGHTNESS     80   /* 0..100 % */
#define NEXUS_DISPLAY_REFRESH_MS     500

/* =========================================================================
 * Task configuration
 *
 * These are stack sizes in bytes, passed unchanged to xTaskCreate by the
 * module that owns each task.
 * ========================================================================= */
#define NEXUS_TASK_STACK_MOTION      4096
#define NEXUS_TASK_STACK_CSI         3072
#define NEXUS_TASK_STACK_DISPLAY     3072
#define NEXUS_TASK_STACK_LOGGER      3072
#define NEXUS_TASK_STACK_WIFI        3072

#define NEXUS_TASK_PRIO_CSI          6
#define NEXUS_TASK_PRIO_MOTION       5
#define NEXUS_TASK_PRIO_WIFI         4
#define NEXUS_TASK_PRIO_DISPLAY      3
#define NEXUS_TASK_PRIO_LOGGER       2

/** Task watchdog feed interval margin: tasks feed the WDT at least this
 *  often (must be < CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000). */
#define NEXUS_WDT_FEED_MS            2000

/* =========================================================================
 * Web server
 * ========================================================================= */
#define NEXUS_HTTP_PORT              80
#define NEXUS_WS_MAX_CLIENTS         4
#define NEXUS_WS_PUSH_INTERVAL_MS    500

/** Largest inbound WebSocket frame we will buffer. The dashboard only ever
 *  sends short control messages, so anything bigger is treated as a client to
 *  drop rather than a payload to allocate for. */
#define NEXUS_WS_MAX_FRAME_LEN       512
#define NEXUS_HTTP_MAX_BODY          2048

/* =========================================================================
 * NVS namespaces / keys
 * ========================================================================= */
#define NEXUS_NVS_NAMESPACE          "nexus"
#define NEXUS_NVS_KEY_CONFIG         "cfg"
#define NEXUS_NVS_KEY_LOGS           "logs"

#endif /* NEXUS_APP_CONFIG_H */
