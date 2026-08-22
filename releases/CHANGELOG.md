# Changelog

All notable changes to the NEXUS CSI Sensor are documented here. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the
project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

A correctness and hardening pass over the whole firmware, plus the project files
a public repository needs. Every finding is written up with a severity in
[docs/CODE_REVIEW.md](../docs/CODE_REVIEW.md); this is the summary of what got
fixed.

> Most of this has not been through a compiler. The findings come from a
> module-by-module static review, not from a green build, and `idf.py build` will
> almost certainly surface things a reading pass cannot. The exceptions are
> `ringbuffer.c`, `logger.c` and `json.c`, which the new host suites in
> [tests/host](../tests/host/README.md) do compile and run. Everything else is
> reviewed, not verified.

### Fixed

**WebSocket lifetime (both high severity)**
- The push task tracked clients by raw socket descriptor. lwIP reuses fd numbers,
  so a descriptor abandoned by an abrupt disconnect could be handed to an
  unrelated connection and then written to with a raw WebSocket frame. Clients
  are now tracked as `{fd, session token}` pairs, and every push re-checks with
  `httpd_ws_get_fd_info()` that the fd is still a WebSocket before using it.
- An open WebSocket was never re-authenticated after the handshake, so logging
  out or letting a session lapse did not stop the telemetry stream. Each push now
  revalidates the session that opened the socket and closes it if the session is
  gone.

**Memory safety**
- `logger_init()` passed the stored blob size to `nvs_get_blob()` as the capacity
  of a smaller heap buffer. Because that length argument is in/out and describes
  the caller's buffer on input, a stored blob larger than
  `NEXUS_LOG_CAPACITY` could be written past the end of the allocation.
- `json_reserve()` could overflow its capacity doubling on a very large document.
- Non-finite floats are no longer formatted into JSON, where they would have
  produced `nan` or `inf` and made the response unparseable.
- The two static SPI buffers in the display driver are now word-aligned, which is
  what ESP-IDF asks for on DMA-capable transmit buffers.
- Mutexes are no longer leaked when a later step of `logger_init()`,
  `csi_init()`, or `motion_init()` fails.

**Security**
- `utils_consttime_equal()` compared only up to 256 bytes and returned early on a
  length mismatch. It now compares full width and folds the length difference into
  the same accumulator.
- Login no longer skips password hashing when the username is wrong, which made a
  bad username measurably faster to reject than a bad password and leaked whether
  an account name was right.
- The login rate limiter could be defeated by filling the slot table: an
  overflowing key reused slot 0 unconditionally, clearing somebody else's lockout.
  Slots serving an active lockout are now never evicted, a saturated table fails
  closed, a read-only check no longer consumes a slot, and requests with no
  resolvable peer address share one bucket instead of being exempt.
- Session cookie parsing matched the name anywhere in the header, so a cookie
  called `notsession` satisfied a lookup for `session`. The match is now anchored
  to a cookie boundary.
- An oversized inbound WebSocket frame dropped the client instead of leaving the
  socket desynchronised.

**Correctness and robustness**
- OTA now rejects an image larger than the target partition **before** erasing
  anything, and returns HTTP 413. Previously the partition was wiped first and the
  write failed part-way through, destroying the rollback copy for nothing.
- A rate-limited login returns HTTP 429 with a message saying so, rather than a
  flat 401 that told a locked-out user to check their password for two minutes.
- WiFi disconnect handling no longer sleeps for the retry backoff inside the
  event loop. The backoff and the soft-AP fallback moved to a dedicated worker
  task fed by a queue, so the event loop is never blocked and reconfiguration
  cannot race a reconnect.
- All shared WiFi state (SSID, IP, gateway, role) is now written and read under a
  mutex.
- `packets_total` is counted unconditionally instead of only when a try-lock
  succeeded, so the counter no longer silently undercounts under load.
- A zero-width or zero-height fill no longer programs an inverted window on the
  display panel, which happened on every idle frame.
- `/api/logs.csv` sizes its line buffer from `NEXUS_LOG_MSG_LEN` rather than a
  fixed 160 bytes, so a long message with escaping cannot be truncated.
- Reading the event log no longer allocates a copy of the whole ring. Both the
  JSON and CSV exports walk it one entry at a time, which takes the transient cost
  of a log request from roughly 6.9 KB of heap to about 108 bytes of stack and
  removes a path where an allocation failure returned a silently empty log list.
  The remaining copy is in `logger_persist()`, because `nvs_set_blob()` needs the
  entries contiguous, and it now sizes itself to the entries actually held.
- The CSI receive callback no longer blocks on the sample buffer's mutex. It runs
  in the WiFi task, where waiting on a slow reader stalls packet reception itself,
  so it now waits 2 ms and drops the sample instead. Drops are counted and
  reported as `csi.packets_dropped`, so contention is visible rather than silent.
- `json_get_int()` reads cJSON's `valuedouble` and rejects values outside `int`
  range. It previously read `valueint`, which saturates at `INT_MAX` and truncates
  fractions, so a range check could pass on a number the client never sent.
- The first-boot health check no longer waits 15 seconds for a station connection
  that is not coming. In soft-AP fallback there is no join in flight, and that is
  exactly the boot where somebody is waiting for the setup network to appear.
- The unknown-route handler returns 404 instead of 400, and `send_json()` can now
  emit 404, 413, and 429.
- `webserver_start()` fails cleanly if its mutex or push task cannot be created,
  and `webserver_stop()` stops the push task before tearing down the server.
- Missing includes added (`stddef.h`, `stdio.h`, `stdint.h`, `math.h`,
  `esp_attr.h`, the FreeRTOS task/queue/semaphore headers), and the IPv6 branch of
  client-address lookup is guarded by `CONFIG_LWIP_IPV6`.

### Added

- `tests/host/`: four test suites that build `ringbuffer.c`, `logger.c` and
  `json.c` with the compiler already on your machine and run them, in a couple of
  seconds, with no ESP-IDF and no board. They compile the production sources
  directly rather than copies, and `make SAN=1` reruns them under the address and
  undefined-behaviour sanitizers. Two of them were checked against the bugs they
  exist for by reintroducing those bugs and confirming a red run:
  `test_logger` calls `logger_log()` from inside the emit loop of
  `logger_to_json()` to reproduce the index shift the dedupe guard handles, and
  `test_logger_restore` models the awkward half of `nvs_get_blob()` so the
  boot-time heap overflow in finding 3 is reachable rather than papered over. CI
  runs the lot as a separate job, which makes it the only job that executes code
  instead of only compiling it. The
  [README](../tests/host/README.md#what-this-does-not-prove) is explicit about
  the limits: the FreeRTOS stubs are a counter and not a scheduler, so real
  concurrency is out of scope, the cJSON parse half of `json.c` cannot run on the
  host, NVS itself is a stand-in, and none of it substitutes for `idf.py build`.
- `docs/CODE_REVIEW.md`: the full module-by-module review behind this entry, with
  severities, a verified-correct section, and a recommended order of work. Several
  of the remaining items are small and self-contained, which makes them reasonable
  first contributions.
- `docs/FAQ.md`: the questions people actually ask, including honest answers about
  what CSI sensing cannot do (no person counting, no identification, pets look a
  lot like people).
- `CODE_OF_CONDUCT.md`, with project-specific sections on sensing ethics, security
  reporting, and what is expected of maintainers.
- Issue templates for bug reports, feature requests, and detection tuning, plus a
  pull request template. Detection tuning gets its own form because "false
  presence in an empty room" is usually placement and thresholds rather than a
  defect, and answering it needs the room layout rather than a stack trace.
- `auth_peek_session()`: validates a session **without** sliding its expiry, so
  the twice-a-second WebSocket revalidation cannot keep a session alive forever
  while a browser tab sits open.
- `ringbuffer_peek()` and `ringbuffer_push_timeout()`, wrapped for the event log as
  `logger_count()` and `logger_peek()`. One reads a single element out by position
  so a reader does not need an array the size of the ring; the other gives up on
  the mutex instead of blocking a driver callback.
- `NEXUS_WS_MAX_FRAME_LEN`, `NEXUS_CSI_PUSH_TIMEOUT_MS`, `NEXUS_TASK_STACK_WIFI`,
  and `NEXUS_TASK_PRIO_WIFI` in `app_config.h`, so the new limits are named
  constants like everything else.

### Changed

- No em-dashes anywhere in the repository, source included. This is a house style
  rule and it is on the PR checklist.
- The documentation no longer claims the firmware detects a missing display. It
  does not: the ST7735 driver is write-only, so it cannot know. The outcome is the
  same (the device runs fine with no panel), but the mechanism described was
  wrong.
- The reboot-scheduling task's stack was raised from 2560 to 3072 bytes.

## [1.0.0] - 2026-08-21

Initial public release. A complete, self-contained WiFi-CSI human presence and
motion detection system for the ESP32.

### Added

**Sensing**
- WiFi Channel State Information (CSI) capture with per-packet feature
  extraction (subcarrier amplitude mean/spread, RSSI, timestamp) into a
  circular buffer.
- Signal-processing pipeline: median filter → moving average → temporal
  variance → adaptive (motion-frozen) baseline → self-scaling normalisation →
  low-pass smoothing.
- Debounced, hysteretic four-state detection machine:
  **Idle → Presence → Motion → High Motion**, with false-positive suppression.
- Derived indicators: motion intensity, activity level, and signal quality.
- ICMP traffic generator to keep CSI flowing on idle networks.
- All thresholds and tunables centralised in `app_config.h` (no magic numbers).

**Connectivity & web**
- STA join with automatic **`NEXUS-CSI-Setup`** soft-AP provisioning fallback.
- Embedded HTTP server with a REST API, a `/live` WebSocket telemetry stream,
  and the dashboard served from flash (no filesystem dependency).
- Responsive dark "glass" dashboard: live status cards, charts (Chart.js with a
  built-in offline canvas fallback), event log, settings, and OTA upload.

**Security**
- Login with salted **SHA-256** password hashing (hardware-accelerated),
  constant-time comparison, HttpOnly/SameSite session cookies, CSRF
  synchronizer tokens, and login rate-limiting with lockout.

**Storage & updates**
- Versioned, CRC-checked configuration blob in NVS with validation and clean
  fallback to defaults; factory reset.
- Event logger with NVS persistence and CSV export.
- Dual-partition **OTA** via the browser with image verification and bootloader
  rollback.

**Platform & resilience**
- FreeRTOS task model (CSI, motion, WebSocket push, display, logger) with a
  **Task Watchdog** and panic-reboot self-recovery.
- Optional **ST7735** 1.8" TFT status screen, or none at all: the driver is
  write-only and cannot detect a missing panel, so with nothing wired up the SPI
  writes go nowhere and the device runs headless. `NEXUS_ENABLE_DISPLAY=0` leaves
  the driver out of the build entirely.

**Project**
- On-target Unity unit & stress tests reusing production sources.
- GitHub Actions CI building the firmware and test app.
- Full documentation set: README, Installation, Calibration, API, Architecture,
  Troubleshooting, Developer, and Structure guides, plus hardware wiring.
- Module boundaries prepared for future components (MQTT / Home Assistant, BLE
  provisioning, TinyML, ESP-NOW multi-node, cloud bridge).

[Unreleased]: https://github.com/nexus-sensing/NEXUS-CSI-Sensor/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/nexus-sensing/NEXUS-CSI-Sensor/releases/tag/v1.0.0
