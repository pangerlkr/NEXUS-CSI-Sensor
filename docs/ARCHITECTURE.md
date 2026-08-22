# Architecture

This document describes how the NEXUS CSI Sensor firmware is structured: its
modules, the FreeRTOS task model, the flow of data from Wi-Fi packets to a
detection result, the signal-processing pipeline, and the cross-cutting
concerns (persistence, security, resilience). It reflects the 1.0.0 firmware in
[`firmware/main`](../firmware/main).

---

## 1. Design goals

- **CSI-only.** Presence and motion are derived exclusively from Wi-Fi Channel
  State Information - no PIR, camera or ultrasonic input anywhere in the code.
- **No magic numbers.** Every tunable lives in
  [`app_config.h`](../firmware/main/app_config.h); modules reference named
  constants. Runtime-editable values additionally persist in NVS.
- **Modular & layered.** Each concern is one `.c`/`.h` pair with a narrow
  public interface. Layers depend downward only (see the map below).
- **Resilient.** Worker tasks feed the Task Watchdog; the OTA path uses
  bootloader rollback; the network layer falls back to a provisioning AP.
- **Deterministic control flow.** Tasks own their data behind mutexes and
  expose thread-safe snapshot getters; no shared mutable globals across tasks.

---

## 2. Module map

```
        ┌──────────────────────────────────────────────────────────┐
        │                        main.c                             │  bring-up / orchestration
        └───┬───────────┬───────────┬───────────┬──────────┬────────┘
            │           │           │           │          │
    ┌───────▼──┐  ┌─────▼────┐  ┌───▼────┐  ┌───▼─────┐  ┌─▼────────┐
    │  wifi    │  │   csi    │  │ motion │  │ display │  │ webserver│   feature modules
    └───┬──────┘  └────┬─────┘  └───┬────┘  └────┬────┘  └────┬─────┘
        │              │            │            │            │
        │         ┌────▼────────────▼────────────▼────────────▼───┐
        │         │        auth · ota · logger · config           │   services
        │         └────────────────────┬──────────────────────────┘
        │                              │
   ┌────▼──────────────────────────────▼─────────────────────────────┐
   │        utils · ringbuffer · json · storage (NVS)                 │   primitives
   └──────────────────────────────────────────────────────────────────┘
```

| Module | Responsibility |
|--------|----------------|
| `main` | Boot sequence, task creation order, health gate, watchdog. |
| `wifi` | STA join / soft-AP fallback, IP/event handling, status getters. |
| `csi` | Enable CSI capture, per-packet feature extraction, traffic generator. |
| `motion` | Signal-processing pipeline + detection state machine + history. |
| `display` | Optional ST7735 status screen (compiled out if disabled). |
| `webserver` | HTTP server, REST API, `/live` WebSocket, OTA upload. |
| `auth` | Sessions, CSRF, login rate-limiting, credential verification. |
| `ota` | Streamed image write, verification, rollback bookkeeping. |
| `logger` | Event ring buffer with NVS persistence; JSON/CSV export. |
| `config` | Versioned, CRC-checked NVS config blob; validation; JSON apply. |
| `storage` | Thin error-checked NVS wrapper. |
| `utils` | Time, clamping/mapping, hex, SHA-256, RNG, constant-time compare. |
| `ringbuffer` | Generic thread-safe circular buffer. |
| `json` | Streaming JSON builder + cJSON-backed parse helpers. |

---

## 3. Task model (FreeRTOS)

The firmware runs a small set of long-lived tasks plus event-driven callbacks.
Stack sizes and priorities are named constants in
[`app_config.h`](../firmware/main/app_config.h).

| Task | Priority | Stack (bytes) | Role |
|------|----------|---------------|------|
| CSI housekeeping | 6 (`NEXUS_TASK_PRIO_CSI`) | 3072 | Packet-rate stats + ICMP traffic generator. CSI samples themselves are captured in the driver's RX callback. |
| Motion engine | 5 (`NEXUS_TASK_PRIO_MOTION`) | 4096 | Runs the pipeline + state machine, publishes results, appends history. |
| WebSocket push | 4 | 4096 | Serialises status and pushes to WS clients every 500 ms. |
| Display | 3 (`NEXUS_TASK_PRIO_DISPLAY`) | 3072 | Redraws the TFT every `NEXUS_DISPLAY_REFRESH_MS`. |
| Logger | 2 (`NEXUS_TASK_PRIO_LOGGER`) | 3072 | Periodically persists the event ring to NVS. |
| HTTP server | (esp_http_server) | 8192 | Accepts connections, dispatches REST/OTA handlers. |

Additional execution contexts:

- **Wi-Fi / IP events** are handled in the system event task via callbacks in
  `wifi.c` - there is no dedicated Wi-Fi worker task.
- **CSI RX** runs in the Wi-Fi driver's callback (`csi_rx_cb`); it does the
  minimum work - extract a feature, push to the ring - then returns.
- **OTA** executes inside the HTTP request handler that receives the upload;
  progress is exposed via a shared, mutex-guarded status struct.

Every long-lived worker subscribes to the **Task Watchdog** and feeds it at
least every `NEXUS_WDT_FEED_MS` (2 s), well inside the 10 s timeout. See
[§8 Resilience](#8-resilience--recovery).

---

## 4. Data flow

```
 Wi-Fi packets ──▶ csi_rx_cb (driver ctx)
                     │  extract: amp_mean, amp_std, rssi, timestamp
                     ▼
              CSI ring buffer  (csi_sample_t × 256)          ← ringbuffer.c
                     │  snapshot of the newest window
                     ▼
              Motion task  ──▶ signal pipeline ──▶ state machine
                     │                                  │
       motion_result_t (mutex-guarded)        history ring (120 pts)
                     │                                  │
        ┌────────────┼───────────────┬──────────────────┤
        ▼            ▼               ▼                  ▼
   display task   WS push task   /api/status        /api/history
                                 REST handler        REST handler
```

Producers never block on consumers: the motion task copies a snapshot out of
the CSI ring, and all readers (web, display) take a lock only to memcpy a small
result struct.

---

## 5. The signal-processing pipeline

Implemented in `motion.c`, consuming `csi_sample_t` amplitude features. Each
stage's constant is named in `app_config.h`.

```
 raw amp_mean stream
   │
   ├─▶ 1. Median filter        kernel NEXUS_MEDIAN_KERNEL (5)      → spike removal
   │
   ├─▶ 2. Moving average       length NEXUS_MOVAVG_LEN (8)         → smoothing
   │
   ├─▶ 3. Temporal variance    window NEXUS_CSI_WINDOW_SIZE (64)   → "movement energy"
   │
   ├─▶ 4. Adaptive baseline    EMA NEXUS_BASELINE_ALPHA (0.01)     → learned empty-room level
   │        (frozen while motion is asserted - NEXUS_BASELINE_FREEZE_ON_MOTION)
   │
   ├─▶ 5. Normalisation        score = Δ / (Δ + baseline·SENS + FLOOR)
   │        SENS = NEXUS_NORM_SENSITIVITY (4.0), FLOOR = NEXUS_NORM_FLOOR (1.0)
   │        → self-scaling 0..1, independent of absolute magnitude
   │
   ├─▶ 6. Low-pass (EMA)       alpha NEXUS_LPF_ALPHA (0.30)        → final motion score
   │
   └─▶ 7. State machine        thresholds + debounce + hysteresis  → detection state
```

Auxiliary indicators derived alongside:

- **Motion intensity** (`0-100 %`): the instantaneous score scaled to a percent.
- **Activity level** (`0-100 %`): a slow EMA (`NEXUS_ACTIVITY_ALPHA`, 0.05) of
  the score - reflects *sustained* activity rather than an instant.
- **Signal quality** (`0-100 %`): RSSI mapped from
  `[NEXUS_RSSI_MIN_DBM, NEXUS_RSSI_MAX_DBM]` (`-90…-30 dBm`) combined with
  packet-rate health.

Because step 5 normalises against the learned baseline, the same thresholds
work across rooms with very different absolute CSI magnitudes - which is why the
[calibration step](CALIBRATION.md) matters and why the baseline is frozen during
motion (so a moving person is never absorbed into "background").

---

## 6. Detection state machine

Four states, ordered by activity:

```
        score ≥ presence (rise-debounced)
  IDLE ─────────────────────────────────▶ PRESENCE
    ▲                                        │  score ≥ motion
    │ score < presence-hysteresis            ▼
    │ (fall-debounced)                     MOTION
    │                                        │  score ≥ high_motion
    └──────────────── … ◀────────────────    ▼
                                          HIGH_MOTION
```

- **Debounce:** a rising transition needs `NEXUS_DEBOUNCE_RISE` (3) consecutive
  qualifying windows; a falling transition needs `NEXUS_DEBOUNCE_FALL` (8).
  Falling is slower so brief stillness doesn't drop presence.
- **Hysteresis:** `NEXUS_THRESHOLD_HYSTERESIS` (0.05) is subtracted from a
  threshold before the machine will drop below it, preventing chatter at the
  boundary.
- **False-positive suppression:** the combination of median filtering, the
  fall-debounce, hysteresis, and the frozen baseline suppresses transient RF
  glitches.

State enum (also the `motion.state` value in the API):
`0` Idle · `1` Presence · `2` Motion · `3` High Motion.

Transitions are logged as `EVENT`s and reflected on the dashboard, the TFT, and
the `/live` stream.

---

## 7. Persistence & memory

### Flash partition table

From [`partitions.csv`](../firmware/partitions.csv) (4 MB flash):

| Partition | Type | Offset | Size | Purpose |
|-----------|------|--------|------|---------|
| `nvs` | data/nvs | `0x9000` | 24 KB | Config blob + log snapshot. |
| `otadata` | data/ota | `0xF000` | 8 KB | Which OTA slot is active. |
| `phy_init` | data/phy | `0x11000` | 4 KB | RF calibration data. |
| `ota_0` | app | `0x20000` | 1.5 MB | Firmware slot A. |
| `ota_1` | app | `0x1A0000` | 1.5 MB | Firmware slot B. |
| `storage` | data/spiffs | `0x320000` | 768 KB | Reserved for future file storage. |

### NVS layout

A single namespace `nexus` (`NEXUS_NVS_NAMESPACE`) holds:

- `cfg` - the versioned `nexus_config_t` blob, guarded by a **magic**
  (`NEX1`), a **version**, and a trailing **CRC32**. On boot a mismatch (first
  boot, corruption, or an older layout) falls back cleanly to defaults.
- `logs` - a periodically-persisted snapshot of the event ring, so recent
  history survives reboots.

Secrets in the config blob are stored as `admin_pass_hash` = salted SHA-256 and
a per-device `admin_salt`; the plaintext password is never persisted.

---

## 8. Resilience & recovery

- **Task Watchdog.** Enabled with `CONFIG_ESP_TASK_WDT_PANIC=y`: if a
  subscribed worker (or an idle task) stalls past the 10 s timeout the system
  **panics and reboots** rather than hanging. Combined with
  `CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y`, any panic auto-reboots.
- **OTA rollback.** With `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, a
  freshly-flashed image is *pending* until `main` reaches a known-good state and
  calls `ota_mark_valid()`. If the new image crash-loops before that, the
  bootloader reverts to the previous slot.
- **Network fallback.** If STA credentials are missing or the join fails after
  `NEXUS_WIFI_CONNECT_RETRY_MAX` attempts, `wifi` starts the `NEXUS-CSI-Setup`
  soft-AP so the dashboard stays reachable for reconfiguration.
- **Graceful degradation.** A missing/failed TFT, or a failure to start CSI,
  is logged and the rest of the system continues; only truly fatal bring-up
  failures (Wi-Fi init, web server) trigger a reboot.

### Boot sequence (`app_main`)

```
banner → storage_init → config_init → logger_init → auth_init
       → wifi_start → csi_init → motion_init → display_init
       → ota_init → webserver_start
       → wait for IP (≤ 15 s) → ota_mark_valid()  → "System ready"
```

Each step checks `esp_err_t`, logs a meaningful message, and either continues
(non-fatal) or reboots after a short delay (fatal).

---

## 9. Security architecture

See the [API reference](API.md#authentication-model) for the wire-level flow.
In the firmware:

- Passwords: **salted SHA-256** (hardware-accelerated mbedTLS), compared with a
  **constant-time** routine (`utils_consttime_equal`).
- Sessions: random 16-byte tokens (hex) in an **HttpOnly, SameSite=Strict**
  cookie; server-side table with TTL and a cap.
- **CSRF:** each session is bound to a synchronizer token, required in
  `X-CSRF-Token` on every mutating request.
- **Rate limiting:** failed logins per client IP are throttled with a lockout.
- Response hardening headers (`nosniff`, `DENY`) on every response.

---

## 10. Web asset embedding

The dashboard (`web/index.html`, `login.html`, `css/style.css`, `js/app.js`) is
compiled **into the firmware binary** via `EMBED_FILES` in
[`main/CMakeLists.txt`](../firmware/main/CMakeLists.txt) and served from flash.
There is no filesystem dependency, so the UI works identically in soft-AP mode
and offline. Chart.js is loaded from a CDN when reachable; `app.js` includes a
built-in canvas renderer as a fallback so charts still work without internet.

---

## 11. Future-ready architecture

The module boundaries were chosen so the following can be added without
reworking the core. Each would live in [`firmware/components/`](../firmware/components/README.md):

- **MQTT / Home Assistant** - subscribe to `motion_result_t` snapshots and
  publish state + HA MQTT discovery.
- **BLE provisioning** - an alternative to the soft-AP for entering Wi-Fi
  credentials.
- **TinyML** - a classifier consuming the same CSI feature window to label
  activity types, running as an extra pipeline stage.
- **ESP-NOW multi-node** - fuse detections from several sensors for
  whole-home coverage.
- **Cloud bridge** - optional off-device telemetry/history.

The sensing core (`csi` + `motion`) publishes a clean `motion_result_t` and a
history ring; new consumers attach to those without touching the pipeline.
