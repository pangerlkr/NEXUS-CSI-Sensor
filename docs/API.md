# API Reference

The firmware serves the dashboard and a JSON REST API from an embedded HTTP
server on **port 80**, plus a WebSocket for live telemetry. This document is
the exact contract for every endpoint.

- Base URL: `http://<device-ip>/`
- Request bodies (where applicable) are JSON; responses are JSON unless noted.
- All responses include `X-Content-Type-Options: nosniff` and
  `X-Frame-Options: DENY`.

---

## Authentication model

Authentication is enabled by default (`NEXUS_ENABLE_AUTH`). The flow is:

1. `POST /api/login` with credentials. On success the server sets an **HttpOnly
   session cookie** and returns a **CSRF token** in the JSON body.

   ```
   Set-Cookie: session=<token>; Path=/; HttpOnly; SameSite=Strict; Max-Age=3600
   ```

2. The browser automatically sends the cookie on subsequent requests.
   Because the cookie is HttpOnly, JavaScript cannot read it; the dashboard
   obtains its CSRF token from `GET /api/session` instead.

3. **Mutating requests** (`POST`) must include the CSRF token in a header:

   ```
   X-CSRF-Token: <csrf>
   ```

Sessions expire after `NEXUS_SESSION_TTL_S` (3600 s). Up to
`NEXUS_MAX_SESSIONS` (4) sessions may be active. Passwords are stored only as a
salted SHA-256 hash and compared in constant time. Repeated failed logins from
one client are rate-limited (`NEXUS_LOGIN_MAX_ATTEMPTS` per
`NEXUS_LOGIN_WINDOW_S`, then a `NEXUS_LOGIN_LOCKOUT_S` lockout).

> When the firmware is built with `NEXUS_ENABLE_AUTH = 0`, `GET /api/session`
> returns `auth_required: false` with an empty CSRF token and the mutating
> endpoints do not enforce authentication. The default build **does** enforce it.

### Status codes

| Code | Meaning |
|------|---------|
| `200` | Success. |
| `400` | Malformed request (bad/missing JSON, empty body where one is required). |
| `401` | Not authenticated (missing/expired session) - client should redirect to `/login`. |
| `403` | Authenticated but CSRF token missing/mismatched, or login temporarily locked out. |
| `500` | Internal error. |

Error responses have the shape:

```json
{ "ok": false, "message": "human-readable reason" }
```

---

## Static / page routes

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| `GET` | `/` | session | Dashboard SPA (`index.html`). Unauthenticated requests are redirected to `/login`. |
| `GET` | `/login` | none | Login page. |
| `GET` | `/css/style.css` | none | Stylesheet. |
| `GET` | `/js/app.js` | none | Dashboard controller. |

Unknown `GET` routes return `index.html` (SPA fallback); unknown non-`GET`
routes return `400`.

---

## Auth endpoints

### `POST /api/login`

Authenticate and start a session.

**Request**

```json
{ "user": "admin", "pass": "your-password" }
```

**Response `200`** - also sets the `session` cookie.

```json
{ "ok": true, "csrf": "b3f1c2…" }
```

**Errors:** `401` invalid credentials; `403` locked out (too many attempts);
`400` malformed body.

---

### `POST /api/logout`

Invalidate the current session. Requires a valid session cookie.

**Response `200`**

```json
{ "ok": true }
```

---

### `GET /api/session`

Report whether the caller is authenticated and hand back the CSRF token bound
to the session. The dashboard calls this on load.

**Response `200`**

```json
{ "authenticated": true, "auth_required": true, "csrf": "b3f1c2…" }
```

**Response `401`** when there is no valid session:

```json
{ "ok": false, "message": "Not authenticated" }
```

---

## Telemetry & data

### `GET /api/status`

Full live status snapshot. This is exactly the payload pushed over the
[`/live` WebSocket](#websocket-live).

**Response `200`**

```json
{
  "device": "nexus-csi",
  "fw": "1.0.0",
  "uptime_s": 12345,
  "uptime": "0d 03:25:45",
  "heap_free": 210344,
  "heap_min": 183920,
  "partition": "ota_0",
  "sessions": 1,
  "wifi": {
    "role": "STA",
    "connected": true,
    "ssid": "lab-ap",
    "ip": "192.168.1.42",
    "gateway": "192.168.1.1",
    "rssi": -57,
    "channel": 6
  },
  "csi": {
    "rssi": -57,
    "amp_mean": 22.4,
    "amp_std": 6.1,
    "pps": 19.8,
    "packets_total": 402113,
    "packets_dropped": 0,
    "active": true
  },
  "motion": {
    "state": 2,
    "state_str": "Motion",
    "presence": true,
    "score": 0.42,
    "intensity": 48.0,
    "activity": 31.0,
    "variance": 0.017,
    "baseline": 0.004,
    "signal_quality": 72.0,
    "state_since_s": 8
  }
}
```

**Field notes**

| Field | Meaning |
|-------|---------|
| `uptime_s` / `uptime` | Seconds since boot / formatted `Dd HH:MM:SS`. |
| `partition` | Running OTA partition (`ota_0` / `ota_1`). |
| `sessions` | Active authenticated sessions. |
| `wifi.role` | `STA`, `AP`, or `-`. |
| `csi.pps` | Packets per second feeding CSI. |
| `csi.packets_dropped` | Samples the receive callback could not hand to the ring buffer because a reader held its mutex past the timeout. Normally `0`. A climbing value means something is holding the sample buffer too long, not that packets were lost on the air. |
| `csi.active` | `true` if packets arrived recently (link live). |
| `motion.state` | `0` Idle · `1` Presence · `2` Motion · `3` High Motion. |
| `motion.state_str` | Human name of `state`. |
| `motion.score` | Smoothed, normalised motion score `0.00-1.00`. |
| `motion.intensity` / `activity` / `signal_quality` | Percentages `0-100`. |
| `motion.variance` / `baseline` | Raw CSI-variance feature and adaptive baseline. |
| `motion.state_since_s` | Seconds the current state has held. |

---

### `GET /api/history`

Rolling history for the dashboard charts. Up to `NEXUS_HISTORY_CAPACITY`
(120) points, oldest first. Parallel arrays share the same index.

**Response `200`**

```json
{
  "count": 120,
  "t":        [12000, 13000, 14000, "…"],
  "score":    [0.03, 0.05, 0.41, "…"],
  "variance": [0.004, 0.004, 0.019, "…"],
  "rssi":     [-57, -58, -57, "…"],
  "state":    [0, 0, 2, "…"]
}
```

`t` is uptime in milliseconds; `state` uses the same enum as
`motion.state`.

---

### `GET /api/logs`

Recent event-log entries, **newest first**, up to `NEXUS_LOG_CAPACITY` (64).

**Response `200`**

```json
{
  "logs": [
    { "seq": 128, "t": 12441000, "level": "EVENT", "msg": "Motion detected" },
    { "seq": 127, "t": 12250000, "level": "INFO",  "msg": "Recalibrated baseline" }
  ]
}
```

| Field | Meaning |
|-------|---------|
| `seq` | Monotonic sequence number. |
| `t` | Uptime in milliseconds when the event was logged. |
| `level` | `INFO`, `EVENT`, `WARN` or `ERROR`. |
| `msg` | Message text (≤ 95 chars). |

---

### `GET /api/logs.csv`

The same events as a downloadable CSV (chunked). Fields are quoted and
quote-escaped.

```
Content-Type: text/csv
Content-Disposition: attachment; filename="nexus-logs.csv"
```

```csv
seq,uptime_ms,level,message
128,12441000,EVENT,"Motion detected"
127,12250000,INFO,"Recalibrated baseline"
```

---

## Configuration

### `GET /api/config`

Current configuration. **Secrets are never returned** - the Wi-Fi and admin
passwords are represented only by presence flags/usernames.

**Response `200`**

```json
{
  "device_name": "nexus-csi",
  "wifi_ssid": "lab-ap",
  "wifi_pass_set": true,
  "presence_threshold": 0.12,
  "motion_threshold": 0.35,
  "high_motion_threshold": 0.70,
  "sampling_rate_hz": 20,
  "auto_calibration": true,
  "display_brightness": 80,
  "admin_user": "admin"
}
```

---

### `POST /api/config`

Apply a **partial** configuration update. Only the keys present are changed;
unknown keys are ignored. Values are clamped to valid ranges server-side.
Requires session + CSRF.

**Accepted keys**

| Key | Type | Range / notes |
|-----|------|---------------|
| `device_name` | string | ≤ 31 chars |
| `wifi_ssid` | string | ≤ 32 chars |
| `wifi_pass` | string | ≤ 64 chars. Omit to keep the stored password. |
| `presence_threshold` | number | 0.0-1.0 |
| `motion_threshold` | number | 0.0-1.0 |
| `high_motion_threshold` | number | 0.0-1.0 |
| `sampling_rate_hz` | integer | 5-100 |
| `auto_calibration` | boolean | |
| `display_brightness` | integer | 0-100 |
| `admin_user` | string | ≤ 31 chars |
| `admin_pass` | string | new admin password; re-salted and hashed |

Changing `wifi_ssid`/`wifi_pass` triggers a live Wi-Fi reconnect (no reboot).

**Response `200`**

```json
{ "ok": true, "message": "Configuration saved" }
```

---

## Actions

All require session + CSRF and take no body (send `{}`).

### `POST /api/calibrate`

Force a baseline recalibration and reset the state machine to Idle.

```json
{ "ok": true, "message": "Calibration started" }
```

### `POST /api/reboot`

Persist state and reboot after a short delay (≈ 0.8 s) so the response can be
delivered first.

```json
{ "ok": true, "message": "Rebooting" }
```

### `POST /api/factory-reset`

Erase all stored configuration and the event log, then reboot into the setup
AP.

```json
{ "ok": true, "message": "Factory reset - rebooting" }
```

---

## Over-the-air update

### `POST /api/ota`

Stream a firmware image to the inactive OTA partition. Requires session + CSRF.

- **Body:** raw binary firmware (`application/octet-stream`), sent as the
  request body. The server streams it straight to flash - do **not** wrap it in
  multipart or JSON.
- Send `Content-Length` so the server can report a percentage.

**Response `200`** (image written and validated; device reboots into it):

```json
{ "ok": true, "message": "Update applied, rebooting" }
```

**Errors:** `400`/`500` with a `message` (e.g. bad magic, verification failed,
write error). On any failure the running firmware is untouched.

Example with `curl` (obtain `<csrf>` from `/api/session` and reuse the login
cookie jar):

```bash
curl -b cookies.txt -H "X-CSRF-Token: <csrf>" \
     --data-binary @build/nexus_csi_sensor.bin \
     http://<device-ip>/api/ota
```

### `GET /api/ota/status`

Poll OTA progress (also used to render the dashboard progress bar).

```json
{ "in_progress": true, "percent": 63, "received": 812000, "total": 1287664, "message": "writing" }
```

`total` is `0` when the client did not send a `Content-Length`, in which case
`percent` stays `0`.

---

## WebSocket `/live`

Real-time telemetry stream.

- **URL:** `ws://<device-ip>/live` (`wss://` if served over TLS).
- **Auth:** the session cookie is validated during the HTTP upgrade handshake;
  an unauthenticated upgrade is rejected.
- **Direction:** server → client only. Every `NEXUS_WS_PUSH_INTERVAL_MS`
  (500 ms) the server sends one text frame containing the exact
  [`GET /api/status`](#get-apistatus) JSON. Client frames are ignored.
- Up to `NEXUS_WS_MAX_CLIENTS` (4) simultaneous clients; dead sockets are
  reaped automatically.

The dashboard opens this socket on load and falls back to polling
`/api/status` every few seconds whenever the socket is not open.

---

## Rate limits & sizes

| Limit | Value | Constant |
|-------|-------|----------|
| Max request body | 2048 bytes | `NEXUS_HTTP_MAX_BODY` |
| Login attempts | 5 / 60 s, then 120 s lockout | `NEXUS_LOGIN_*` |
| Session TTL | 3600 s | `NEXUS_SESSION_TTL_S` |
| Concurrent sessions | 4 | `NEXUS_MAX_SESSIONS` |
| WebSocket clients | 4 | `NEXUS_WS_MAX_CLIENTS` |

All are defined in [`app_config.h`](../firmware/main/app_config.h).
