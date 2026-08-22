# Firmware Code Review, 2026-08-21

A static review of every C module in `firmware/main/`, performed by reading the
sources. **The firmware has not been compiled for the target or run on hardware
at the time of this review**, so this document is most of the correctness
evidence the project currently has. It is not a substitute for a green build and
a bench test.

The findings below are kept as written, with a **Status** line added to each one
after the fixes landed. Deleting a fixed finding would throw away the useful
half: what was wrong, and why the current code is shaped the way it is. Every fix
was verified the same way the bug was found, by reading the code, so "addressed"
here means reviewed rather than compiled for the target. Three modules are the
exception: `ringbuffer.c`, `logger.c` and `json.c` are compiled and executed by
the host suites in [tests/host](../tests/host/README.md), which cover findings 3,
12 and 16 with something better than a reading pass.

Severity key:

| Level | Meaning |
|-------|---------|
| BLOCKER | Will not compile |
| HIGH | Crash, memory corruption, or exploitable |
| MEDIUM | Wrong behaviour, latent corruption, or a real robustness gap |
| LOW | Nit, style, or hardening worth doing |

**No BLOCKER was found in any module.** Every ESP-IDF, mbedTLS, and cJSON API
signature used matches v5.2, header and source declarations agree, and the
CMake `REQUIRES` lists cover the components actually used.

---

## Where this stands

| Item | Severity | Status |
|------|----------|--------|
| 1. WebSocket push writes to stale sockets | HIGH | Addressed |
| 2. WebSocket sessions never revalidated | HIGH | Addressed |
| 3. Latent heap overflow restoring logs | MEDIUM | Addressed, host test |
| 4. Constant-time compare truncation | MEDIUM | Addressed |
| 5. Rate limiting bypassed for unknown IP | MEDIUM | Addressed |
| 6. Rate-limit table evicts slot 0 | MEDIUM | Addressed |
| 7. Cookie lookup unanchored | MEDIUM | Addressed |
| 8. Blocking calls in the WiFi event handler | MEDIUM | Addressed |
| 9. `wifi_reconfigure()` races auto-reconnect | MEDIUM | Addressed |
| 10. WiFi status strings unlocked | MEDIUM | Addressed |
| 11. CSI counter undercounts | MEDIUM | Addressed |
| 12. One NaN breaks the whole response | MEDIUM | Addressed, host test |
| 13. Zero-width fill on idle frames | MEDIUM | Addressed |
| 14. Display degradation claimed, not implemented | MEDIUM | Docs corrected, probe not added |
| 15. `config.c` missing `<stdio.h>` | MEDIUM | Addressed |
| 16. Log export allocates about 7 KB | MEDIUM | Addressed, host test |
| 17. OTA erases before the size check | MEDIUM | Addressed |
| 18. Plaintext HTTP undercuts the auth model | MEDIUM | Accepted, in the threat model |

Of the LOW list, all but three are done. The three left standing are noted
inline: `utils_random_hex` call efficiency, the `ringbuffer_push` return
convention, and `ringbuffer_free` versus waiters. Each is cosmetic or
unreachable in this codebase.

**What has not happened: a target build.** Nothing here has been through the
ESP-IDF compiler, and that remains the single most valuable next step. What has
happened since the review is a smaller thing worth naming honestly: the three
modules that do not touch the radio, the panel, or the HTTP stack are now compiled
and executed on the host by [tests/host](../tests/host/README.md), under the
address and undefined-behaviour sanitizers, with the fixes for findings 3, 12 and
16 each verified by putting the bug back and watching the suite go red. That
covers three of the eighteen numbered findings. The other fifteen are still one
person reading code carefully.

---

## Summary by module

Verdicts as found at review time. See [Where this stands](#where-this-stands)
for what has been addressed since.

| Module | Verdict |
|--------|---------|
| `main.c` | Clean. Two LOW items. |
| `app_config.h` | Clean. One stale comment. |
| `wifi.c` / `.h` | Four MEDIUM concurrency items, all in the event handler. |
| `csi.c` / `.h` | One MEDIUM (counter undercount), one LOW. |
| `motion.c` / `.h` | Maths and hysteresis verified correct. Two LOW. |
| `config.c` / `.h` | One MEDIUM (missing include), CRC and salt sizing verified correct. |
| `storage.c` / `.h` | Clean. Thin, correct NVS wrapper. |
| `display.c` / `.h` | One MEDIUM (zero-width fill), one MEDIUM (doc mismatch). |
| `ringbuffer.c` / `.h` | Correct and genuinely thread-safe. One MEDIUM (blocking in callback). |
| `json.c` / `.h` | One MEDIUM (NaN emits invalid JSON). |
| `utils.c` / `.h` | One MEDIUM (constant-time compare truncation). |
| `logger.c` / `.h` | One MEDIUM (latent heap overflow), one MEDIUM (heap pressure). |
| `webserver.c` / `.h` | Two HIGH (WebSocket lifecycle), four MEDIUM. |
| `auth.c` / `.h` | Two MEDIUM (rate-limit bypass and eviction). |
| `ota.c` / `.h` | Clean. No handle leak on any error path. |

---

## HIGH

### 1. WebSocket push writes to stale and reused sockets

`webserver.c`, push loop. Descriptors are removed from `s_ws_fds` only on an
explicit CLOSE frame or a send error. An abrupt TCP disconnect, which is the
common case, leaves a stale fd in the list. lwIP then reuses that fd number for
a new and possibly plain-HTTP connection, and the push task writes a raw
WebSocket frame onto it.

Fix: confirm the descriptor is still a WebSocket client before every send.

```c
if (httpd_ws_get_fd_info(s_server, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
    s_ws_fds[i] = -1;
    continue;
}
```

**Status: addressed.** The push loop checks `httpd_ws_get_fd_info` before every
send and drops the slot when the descriptor is no longer a WebSocket client.

### 2. WebSocket sessions are authenticated once and never revalidated

`webserver.c`, handshake and push loop. `s_ws_fds` stores only the socket fd,
with no binding to a session token. `auth_logout()` clears the session slot but
leaves the fd in the push list, and the push task never rechecks. Live telemetry
keeps streaming after a client logs out and after the session TTL expires.

Fix: store the session token alongside each fd, revalidate it before each push,
and drop the client when validation fails.

**Status: addressed.** `s_ws_fds` became `s_ws_clients`, an fd plus the session
token that opened it, and the push loop calls `auth_peek_session()` per client.
That function exists precisely for this: `auth_validate_session()` refreshes the
session's activity timestamp, and calling it twice a second from the push task
would have kept every socket's session alive forever.

---

## MEDIUM

### 3. Latent heap overflow restoring persisted logs

`logger.c`, `logger_init()`. The entry count `n` is clamped to
`NEXUS_LOG_CAPACITY` and the temporary buffer is allocated for `n` entries, but
the size handed to NVS is the full, unclamped `blob_sz`:

```c
size_t n = blob_sz / sizeof(log_entry_t);
if (n > NEXUS_LOG_CAPACITY) n = NEXUS_LOG_CAPACITY;   /* n clamped */
log_entry_t *tmp = calloc(n, sizeof(log_entry_t));    /* n entries */
size_t rd = blob_sz;                                  /* full size, a lie */
storage_get_blob(NEXUS_NVS_KEY_LOGS, tmp, &rd);
```

`storage_get_blob` passes that value straight to `nvs_get_blob`, whose length
argument is the caller's buffer capacity. Because the claimed capacity equals
the stored size, NVS's own too-small check passes and it writes `blob_sz` bytes
into a smaller allocation, corrupting the heap at boot.

Not reachable with the shipped constants, because `logger_persist()` never
writes more than `NEXUS_LOG_CAPACITY` entries. It becomes reachable the moment
someone lowers `NEXUS_LOG_CAPACITY` while an older, larger blob is still in NVS,
which is an ordinary thing to do given that `app_config.h` is documented as the
place to tune constants.

Fix, one line:

```c
size_t rd = n * sizeof(log_entry_t);
```

**Status: addressed, and tested.** `logger_init()` now describes its own buffer to
NVS, with a comment saying why the stored size is the wrong number to pass. This
is the one finding in the document with real executable evidence behind it:
[`tests/host/test_logger_restore.c`](../tests/host/test_logger_restore.c) models
`nvs_get_blob()`'s length semantics faithfully, runs an empty key, a partial blob,
an exactly-full blob, an oversized blob and a ragged blob, and reverting the fix
makes it fail with `get_blob asked for 9504 bytes, more than the 6912 the ring can
hold`. Worth noting what the fixed behaviour actually is: with the count clamped,
the declared capacity is now smaller than an oversized stored blob, so NVS
returns `ESP_ERR_NVS_INVALID_LENGTH` and the blob is dropped whole rather than
partly restored. Dropping it is the right outcome, and it is the outcome the test
pins down.

### 4. Constant-time comparison discards high bits of the length difference

`utils.c`, `utils_consttime_equal()`:

```c
unsigned char diff = (unsigned char)(la ^ lb);
```

`la ^ lb` is a `size_t`. Casting to `unsigned char` keeps only the low 8 bits,
so any length difference that is a multiple of 256 is silently lost. Lengths 1
and 257 compare as equal-length, only the first byte is then examined, and the
function can return true for two different strings.

Not currently exploitable, because every buffer compared through this function
is a bounded fixed-size field, so a 256-byte length delta cannot occur. It is
still wrong in a security primitive and should not be left in place.

Fix: accumulate the difference at full width.

```c
size_t diff = la ^ lb;
size_t n = (la < lb) ? la : lb;
for (size_t i = 0; i < n; ++i) {
    diff |= (size_t)((unsigned char)a[i] ^ (unsigned char)b[i]);
}
return diff == 0;
```

**Status: addressed.** `utils_consttime_equal()` accumulates at `size_t` width.

### 5. Login rate limiting is bypassed for an unknown client IP

`auth.c`, `auth_is_rate_limited()` returns `false` when the IP string is empty
or NULL. If `get_client_ip()` ever fails to resolve a peer, that request path is
never throttled and never locked out, so brute force against it is unlimited.

Fix: map an unresolved IP to a single shared bucket that is still rate-limited,
rather than exempting it.

**Status: addressed.** Unresolved peers now map to a literal `"?unknown"` key, so
they share one throttled bucket instead of escaping the table. Sharing a bucket
means several such clients can lock each other out, which is the right way round:
the failure mode is a stricter limit, not a missing one.

### 6. Rate-limit table evicts slot 0 instead of the true oldest

`auth.c`. When every slot is occupied the code unconditionally reuses
`s_limits[0]` and memsets it, discarding whatever lockout lived there. An
attacker can fill the table and then clear an active lockout.

Fix: evict the slot with the oldest window, and never evict one whose lockout is
still in the future.

**Status: addressed.** Eviction now scans for the oldest window and skips any
slot still serving a lockout.

### 7. Cookie lookup is unanchored

`webserver.c`. `strstr(cookie, "session=")` also matches inside `mysession=` or
`xsession=`, so a cookie whose name merely ends with the target name shadows the
real one. Not a forgery path, since the value must still match a 128-bit random
token, but it is a genuine parsing defect.

Fix: require the match to sit at the start of the header or immediately after
`"; "`.

**Status: addressed.** The match must now begin the header or follow a `"; "`
separator.

### 8. Blocking calls inside the WiFi event handler

`wifi.c`. The disconnect path calls `vTaskDelay` for the retry backoff, and the
soft-AP fallback runs a full `esp_wifi_stop` and restart sequence, both from the
default event loop task. That stalls every other event, including
`IP_EVENT_STA_GOT_IP`, for up to several seconds per retry.

Fix: set the state bit in the handler and do the delay and the mode switch from
a normal task, or defer the reconnect to a one-shot timer.

**Status: addressed.** The retry and fallback work moved out of the event
handler, which now sets state and returns.

### 9. `wifi_reconfigure()` races the auto-reconnect logic

`wifi.c`. `esp_wifi_disconnect()` posts an asynchronous disconnect event whose
handler then retries a connection to the stale SSID while the reconfigure path
is stopping and restarting the interface.

Fix: add a `s_reconfiguring` flag, set it before disconnecting, and early-return
from the disconnect case while it is set.

**Status: addressed.** `s_reconfiguring` guards both the disconnect case and the
soft-AP fallback.

### 10. WiFi status strings are shared across tasks without a lock

`wifi.c`. `s_ssid` is written by the start paths and read by `wifi_get_ssid()`
from other tasks, so a concurrent write yields a torn string.

Fix: guard the status fields with a small mutex for the copy in and out.

**Status: addressed.**

### 11. CSI packet counter undercounts under lock contention

`csi.c`. `packets_total` is incremented only inside a non-blocking
`xSemaphoreTake(..., 0)` block, so whenever a reader holds the metrics lock the
packet is pushed but never counted. Reported throughput reads low under load.

Fix: keep the total in a separately-updated atomic counter outside the try-lock.

**Status: addressed.** `s_packets_total` lives outside the lock. The same pass
added `s_packets_dropped`, which counts samples the ring buffer would not take,
and surfaced it as `csi.packets_dropped` in the API so contention is visible
rather than merely survivable.

### 12. A single NaN makes the whole API response unparseable

`json.c`, `json_kv_float()` formats with `%.*f`, which yields `nan` or `-inf`
for a non-finite value. Those are not valid JSON, so one bad float anywhere in
the motion pipeline breaks the entire `/api/status` document and the dashboard
stops updating rather than degrading.

The pipeline looks safe today: `NEXUS_NORM_FLOOR` is `1.0f`, so
`sp_normalise()`'s divisor is always at least 1, and `utils_mapf` guards its own
division. This is defence in depth for a hot path.

Fix: emit `0` (or `null`) for any value failing `isfinite()`.

**Status: addressed, and tested.** A shared `json_fmt_float()` helper substitutes
`0` for any non-finite value, used by both `json_kv_float` and `json_elem_float`.
[`tests/host/test_json.c`](../tests/host/test_json.c) checks both entry points
with NaN, positive infinity and negative infinity.

### 13. Zero-width fill on every idle frame

`display.c`. The motion bar computes
`bw = (NEXUS_TFT_WIDTH - 8) * clampf(motion_score, 0, 1)`, which is exactly `0`
whenever the score is zero, that is, in the idle state, which is the normal
resting state. `tft_fill_rect` then calls `tft_set_window(x, y, x - 1, ...)`,
producing a window whose end column precedes its start, and issues SPI
transactions with a length of zero bits.

Fix: return early from `tft_fill_rect` when `w == 0 || h == 0`.

**Status: addressed.**

### 14. Graceful display degradation is claimed but not implemented

`display.c`, `display_init()` returns `ESP_OK` after the panel init sequence
regardless of whether a panel is attached, because nothing is read back from the
ST7735. The hardware guide and the changelog both say the firmware detects an
absent display and logs a warning. In practice it always reports success and
starts the display task.

The behaviour is harmless, writing to an absent SPI device does nothing, so this
is a documentation accuracy problem rather than a runtime fault. Either soften
the claim or probe the panel.

**Status: documentation corrected, probe not added.** `hardware/README.md`,
`docs/COMPONENTS.md` and the changelog now say what actually happens: the driver
is write-only, so it cannot tell, and the writes go nowhere. Probing would mean
reading the panel's ID register, and there is no path for the answer to come
back: the SPI bus is opened with `.miso_io_num = -1`, and the wiring guide never
asks for a MISO connection. Detection would therefore be a hardware change for
every existing build, to report something that is already visible by looking at
the device. `NEXUS_ENABLE_DISPLAY=0` remains the way to build without the driver.

### 15. `config.c` calls `snprintf` without including `<stdio.h>`

It compiles today only because the declaration arrives transitively. Add the
include explicitly.

**Status: addressed.**

### 16. Log export allocates about 7 KB per request

`logger.c`. Both `logger_to_json()` and `logger_persist()` allocate
`NEXUS_LOG_CAPACITY * sizeof(log_entry_t)`, roughly 6.9 KB at the shipped
constants, for the duration of the call. On a device already running WiFi and
the HTTP server this is meaningful transient pressure on every status request,
and it can fail under concurrent requests.

Fix: stream entries directly from the ring into the JSON builder instead of
snapshotting the whole ring into a temporary.

**Status: addressed.** A new `ringbuffer_peek()` copies one element out by
position, wrapped as `logger_peek()`. `logger_to_json()` and the CSV export both
walk the ring an entry at a time, so the transient cost drops from about 6.9 KB of
heap to roughly 108 bytes of stack, and the out-of-memory path that used to return
a silently empty log list is gone.

The one allocation left is in `logger_persist()`, because `nvs_set_blob` takes a
single pointer and length and there is nothing to stream into. It now allocates
for the entries actually held rather than the full capacity, and it runs in the
logger task on a 30 second cadence where a retry is free.

Walking the ring across many locks trades a small amount of consistency for the
memory: an entry logged mid-walk shifts every index by one. The JSON path is
newest-first, where that would show up as a duplicated row, so it drops anything
whose sequence number is not below the last one emitted. The CSV path is
oldest-first, where the same shift can only skip a row, so it does not need the
guard and says so in a comment.

That duplicate is the one part of this fix that is tested rather than only
reviewed. [`tests/host/test_logger.c`](../tests/host/test_logger.c) calls
`logger_log()` from inside the emit loop, at the first entry, the last, and the
middle, and asserts the sequence numbers come out strictly decreasing with nothing
repeated. Disabling the guard makes it fail with `seq 94 emitted twice`.
`ringbuffer_peek()` itself is checked element by element against
`ringbuffer_snapshot()` across the empty, partial, exactly-full and wrapped
states, since the two compute the oldest-element position independently.

### 17. OTA accepts an arbitrary Content-Length before any size check

`webserver.c` and `ota.c`. `ota_begin` passes `OTA_SIZE_UNKNOWN`, erasing the
whole target partition before the declared size is sanity-checked; an oversize
image is only caught later during the write. The upload is gated by session and
CSRF, so this is hardening rather than an open door.

Fix: reject `total > s_target->size` up front.

**Status: addressed.** `ota_begin()` rejects an oversized image with
`ESP_ERR_INVALID_SIZE` before `esp_ota_begin()` touches the flash, so a bad
upload no longer destroys the rollback copy on its way to failing. The handler
maps that to `413`, and a concurrent update attempt to `429`, instead of the flat
`500` both used to get.

### 18. Plaintext HTTP undercuts the auth model

`webserver.c`. The dashboard is served on port 80 with no TLS, so credentials
and the session cookie are visible to anyone on the same LAN. This is an
accepted design constraint for a headless sensor, and the cookie correctly omits
`Secure` because there is no TLS to require. It belongs in the threat model
rather than the bug list, and it is now documented in
[`SECURITY.md`](../SECURITY.md).

---

## LOW

Everything here has been done except the three marked **still open**, which are
each either cosmetic or unreachable in this codebase.

- `webserver.c`: CSV row buffer is `char line[160]` but a quoted message can
  reach roughly 190 bytes, so long messages truncate mid-field and can cut the
  closing quote. Use `char line[48 + NEXUS_LOG_MSG_LEN * 2]`.
- `auth.c`: the username is compared before the password hash is computed, so a
  wrong username answers measurably faster. Always hash, then AND the two
  constant-time results.
- `webserver.c`: WebSocket frames of 512 bytes or more are left unread, which
  desynchronises that socket. Drain or close instead. *Closes the client now.*
- `webserver.c`: the push loop holds `s_ws_lock` across a send that can block for
  the full send timeout. Snapshot the fd list, release, then send.
- `webserver.c`: the 404 handler returns 400, and `send_json` has no 404 branch.
- `webserver.c`: `reboot_task` has a 2560-byte stack and calls `logger_persist()`
  before restarting. Consider 3072.
- `webserver.c`: `webserver_stop()` clears `s_server` but never stops the push
  task, which then leaks and reads the handle unlocked. *Now asks the task to
  exit, waits for it, and only deletes it if it does not.*
- `webserver.c`: `struct sockaddr_in6` is used unconditionally, so the build
  depends on `CONFIG_LWIP_IPV6` staying enabled. It is not pinned in
  `sdkconfig.defaults`. *`get_client_ip()` uses `sockaddr_storage` and compiles
  the IPv6 branch only when the option is on, so nothing needs pinning.*
- `ringbuffer.c`: every operation takes the mutex with `portMAX_DELAY`, including
  `ringbuffer_push` from the CSI callback, which runs in the WiFi task. A
  `ringbuffer_snapshot` of a full window therefore blocks the WiFi task for the
  duration of the copy. Correct, but consider a bounded timeout on the push path.
  *`ringbuffer_push_timeout()` added and used by the CSI callback, with a 2 ms
  budget from `NEXUS_CSI_PUSH_TIMEOUT_MS` and a counter for what it drops.*
- **Still open.** `ringbuffer.c`: `ringbuffer_push` returns whether an element was
  overwritten, and also returns `false` on invalid arguments, so callers cannot
  distinguish success from failure. Nothing relies on this today, and the header
  now spells the convention out, including the fact that
  `ringbuffer_push_timeout` deliberately reports the opposite thing.
- **Still open.** `ringbuffer.c`: `ringbuffer_free` deletes the mutex without
  regard for waiters. Nothing calls it at runtime; every ring in this firmware
  lives for the lifetime of the device.
- `utils.c`: `utils_format_uptime` returns `buf_len - 1` on truncation, which
  underflows to `SIZE_MAX` if `buf_len` is 0.
- **Still open.** `utils.c`: `utils_random_hex` calls `esp_random()` once per two
  hex characters and uses only 8 of the 32 bits. Entropy is correct at 4 bits per
  character; it is simply four times more RNG calls than needed. Tokens are
  generated at most a handful of times per session, so this buys nothing worth the
  churn in a security primitive.
- `json.c`: the growth loop in `json_reserve` doubles without an overflow guard.
- `json.c`: `json_get_int` reads `valueint`, which cJSON truncates. *Reads
  `valuedouble` and rejects anything outside `int` range rather than clamping it.*
- `csi.c`: `info->len <= 0` on a `uint16_t` collapses to `== 0` and may warn
  under `-Wtype-limits`.
- `wifi.c`: uses `vTaskDelay` but includes `freertos/task.h` only transitively.
- `wifi.h`: uses `size_t` without including `<stddef.h>`.
- `config.c`: uses `offsetof` with `<stddef.h>` arriving only via `utils.h`.
- `motion.c` and `config.c`: both leak their mutex if the following
  `ringbuffer_init` or allocation fails. Boot-time only.
- `display.c`: the static SPI buffers are not declared `WORD_ALIGNED_ATTR`.
  ESP-IDF recommends 32-bit alignment for DMA transmit buffers.
- `app_config.h`: the task-configuration comment says stack sizes are derived in
  `main.c` in words. They are passed unchanged, in bytes, from each module.
- `main.c`: in soft-AP mode the 15 second health wait always expires, because the
  connected bit is only set on `IP_EVENT_STA_GOT_IP`. Short-circuit when the role
  is AP. *Now skipped entirely unless the role is STA, which is the boot where
  somebody is standing there waiting for the setup network.*
- `main.c`: `ota_mark_valid()` runs even when connectivity was never
  established. This appears deliberate; confirm the intent. *It is deliberate, and
  the reasoning is now a comment: rollback exists to escape an image that cannot
  run, a failed join is almost always credentials or a missing router, and rolling
  back would remove the soft-AP recovery path that is the only way to fix them.*

---

## Verified correct

Worth recording, because these were the things most likely to be wrong:

- **`ringbuffer.c` is genuinely thread-safe.** A FreeRTOS mutex wraps every
  operation including `push`, `pop`, `count`, `snapshot`, `peek`, and `clear`. The
  earlier concern that the CSI callback raced the reader tasks does not hold.
  Head, tail, and wraparound arithmetic are correct in the full, empty, and
  wrapped cases, and `peek` reuses the same oldest-element arithmetic as
  `snapshot` rather than inventing its own.
- **The detection maths is sound.** Median, moving average, population variance,
  normalisation, and the EMA low-pass are each implemented correctly, including
  the partial-window denominator in the moving average.
- **The hysteresis is correct**, though it reads backwards at first glance. The
  lenient classification, with thresholds lowered by the hysteresis band, gates
  falls, and the strict one gates rises, which is what makes states sticky.
- **CSRF is a proper synchronizer token**, bound to the session, delivered only
  to authenticated callers outside the cookie, and combined with
  `SameSite=Strict` and `HttpOnly`. Every state-changing POST goes through the
  authenticated path.
- **Session and CSRF tokens are not predictable.** They come from `esp_random()`,
  which is a true hardware RNG once the radio is running.
- **The config CRC covers the right bytes.** `crc` is genuinely the last field in
  `nexus_config_t`, so `offsetof(nexus_config_t, crc)` covers all preceding
  bytes, and the version and magic checks precede it.
- **The salt buffer is correctly sized.** `admin_salt[NEXUS_SALT_HEX_LEN + 1]` is
  17 bytes and `utils_random_hex(..., 16)` writes exactly 17 including the
  terminator.
- **`ota.c` leaks no handle on any error path.** `esp_ota_end` releases the handle
  even when validation fails, and the write-failure path goes through
  `ota_abort`.
- **`display.c` has real no-op stubs** in its `#else` branch, so calling
  `display_init()` unconditionally from `main.c` is safe with the feature
  disabled.
- **`storage.c` is a clean, correct NVS wrapper**, and `nvs_flash_init` handles
  the no-free-pages and version-mismatch cases.
- **No user-controlled data is ever passed as a `printf` format string.**
- `mbedtls_sha256_starts(&ctx, 0)` is the correct mbedTLS 3.x call for ESP-IDF
  v5.2. The 2.x `_ret` suffixed variants are not used.

---

## Recommended order of work

The first four steps are done. What is left is the part no amount of reading or
host testing can do.

1. ~~Fix the two HIGH WebSocket items. They are live security and stability bugs.~~
2. ~~Fix items 3, 4, 5, and 6. Each is small, and each sits in a security or
   memory-safety path.~~
3. ~~Fix 13 and 15, which are one-liners, then the remaining MEDIUM items.~~
4. ~~Get the portable modules under a real compiler and a real test runner.~~
   Done as [tests/host](../tests/host/README.md): `make -C tests/host` builds
   `ringbuffer.c`, `logger.c` and `json.c` natively and runs four suites in a
   couple of seconds. It covers three of the eighteen findings and it is cheap to
   extend, which makes adding a suite a good first contribution.
5. **Compile it for the target.** `idf.py build` will find things no reading pass
   can, and it is the one piece of evidence this document cannot supply. Expect
   the first build to fail on something small and dull; that is what first builds
   do.
6. Flash it and watch it detect a human walking past. Until that happens, every
   behavioural claim in the documentation is a hypothesis.

Two things are worth watching on that first run, because they are the fixes
whose behaviour is hardest to predict from the source:

- `csi.packets_dropped` in `/api/status`. It should sit at zero. A number that
  climbs means a reader is holding the sample ring past the 2 ms budget, and the
  place to look is whoever is calling `csi_snapshot_samples`.
- The log list in the dashboard and the CSV export. Both now walk the ring one
  entry at a time. The JSON path's duplicate case is covered by a host test, but
  the CSV path's skip case is not, and neither is what a browser does with the
  result, so both are worth a look by eye.
