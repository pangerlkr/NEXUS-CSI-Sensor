# Developer Guide

Everything a contributor needs to build, test, and extend the firmware. Read
the [Architecture guide](ARCHITECTURE.md) first for the big picture; this
document covers workflow and conventions.

---

## Repository layout

See [STRUCTURE.md](STRUCTURE.md) for a file-by-file map. The essentials:

```
firmware/main/        all firmware modules (one .c/.h per concern) + app_config.h
firmware/components/  reserved for future extractable components
firmware/test/        on-target Unity unit/stress tests
tests/host/           native tests, no ESP-IDF and no board (`make`)
web/                  dashboard assets, embedded into the binary at build time
docs/                 this documentation set
```

---

## Toolchain & build

- **ESP-IDF v5.1.x or v5.2.x** (CI pins **v5.2.1**). Newer 5.x will likely work.
- Export the environment in each shell: `. $IDF_PATH/export.sh`.

```bash
cd firmware
idf.py set-target esp32     # once per checkout
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
idf.py fullclean            # when in doubt
```

`idf.py menuconfig` edits `sdkconfig`. Note that **user-facing** options
(thresholds, sampling rate, credentials, brightness…) are runtime settings in
NVS, **not** Kconfig - only build/platform options belong in menuconfig.
Project defaults live in [`sdkconfig.defaults`](../firmware/sdkconfig.defaults);
never commit a generated `sdkconfig`.

### The single source of truth

All tunable constants live in
[`app_config.h`](../firmware/main/app_config.h). **Do not** introduce a literal
threshold, timeout, pin, or size elsewhere - add a named constant here and
reference it. Runtime-editable values also have a field in `nexus_config_t`
([`config.h`](../firmware/main/config.h)) that overrides the compile-time
default from NVS.

Build-time feature toggles (also in `app_config.h`):

| Macro | Default | Effect |
|-------|---------|--------|
| `NEXUS_ENABLE_DISPLAY` | 1 | Build the ST7735 driver + display task. |
| `NEXUS_ENABLE_TRAFFIC_GEN` | 1 | Generate ICMP traffic to keep CSI flowing. |
| `NEXUS_ENABLE_AUTH` | 1 | Require login + CSRF on the web layer. |

---

## Coding conventions

- **Language:** C (C11), ESP-IDF style. No Arduino APIs anywhere.
- **Documentation:** every public function and type is documented with
  **Doxygen** (`@brief`, `@param`, `@return`) in the header. Each file starts
  with a `@file`/`@brief` block and the MIT copyright line.
- **Naming:** module-prefixed snake_case for public symbols
  (`motion_get_result`, `csi_is_active`); `NEXUS_`-prefixed UPPER_SNAKE for
  constants; `s_`-prefixed statics for file-scope globals.
- **Error handling:** functions that can fail return `esp_err_t`; callers check
  it and log a meaningful message. Use the `ESP_ERROR_CHECK`/`ESP_RETURN_ON_*`
  helpers where a failure is fatal; degrade gracefully where it isn't
  (see `main.c`).
- **Concurrency:** shared state is owned by one module and guarded by a mutex;
  expose thread-safe *snapshot* getters (`*_get_*` copies out) rather than
  handing out pointers to live data.
- **Logging:** use `ESP_LOGx` for developer/console logs and the event
  [`logger`](../firmware/main/logger.h) (`LOG_EVENT`, `LOG_WARN`, …) for
  user-visible events surfaced in the dashboard.
- **No duplicate code:** shared helpers go in `utils`, `json`, or `ringbuffer`.
- **Warnings:** the build enables `-Werror`-adjacent options; keep it clean - 
  no warnings, no deprecated APIs.

---

## Tests

There are two suites, and they answer different questions.

### Host tests, no hardware and no ESP-IDF

[`tests/host`](../tests/host/README.md) builds `ringbuffer.c`, `logger.c` and
`json.c` with whatever compiler you already have and runs them as ordinary
programs. It takes a couple of seconds, so it belongs in your edit loop:

```bash
cd tests/host
make
```

It is the fastest way to find out whether a change to the log ring or the JSON
builder broke something, and the one test in the project that actually executes
code in CI rather than only compiling it. `make SAN=1` reruns everything under the
address and undefined-behaviour sanitizers, which is worth doing after touching
the ring buffer arithmetic. Three of the eighteen findings in
[CODE_REVIEW.md](CODE_REVIEW.md) are covered here, each checked by putting the bug
back and confirming the suite goes red. The
[README](../tests/host/README.md#what-this-does-not-prove) is explicit about the
limits: the FreeRTOS stubs are a counter rather than a scheduler, so real
concurrency is not covered, the cJSON parse half of `json.c` cannot run on the
host, NVS is a stand-in, and a green run is not a firmware build.

### On-target Unity tests

[`firmware/test`](../firmware/test) covers more modules (`ringbuffer`, `json`,
`utils`) including stress cases, and runs on the real scheduler. It reuses the
production sources directly, so it tests shipping code.

```bash
idf.py -C firmware/test set-target esp32
idf.py -C firmware/test flash monitor     # on hardware
# or, no board:
idf.py -C firmware/test qemu monitor      # under QEMU
```

CI (`.github/workflows/build.yml`) runs the host suites and builds both the
firmware and the test app on every push. See
[firmware/test/README.md](../firmware/test/README.md) for what is and isn't
covered.

### Adding a test

For the host suites, follow the steps at the end of
[tests/host/README.md](../tests/host/README.md#adding-a-suite). Two of them carry
the weight: if your test needs a new ESP-IDF header, write the smallest stub that
links and say in its header comment what it fakes, because a stub that quietly
lies is worse than having no test; and break the code on purpose to watch your
test fail before you trust it.

For the on-target app, add a `TEST_CASE("name", "[tag]")` to
[`test_main.c`](../firmware/test/main/test_main.c). If it needs another source
file from `main/`, add that file to `SRCS` in
[`firmware/test/main/CMakeLists.txt`](../firmware/test/main/CMakeLists.txt) and
its dependency to `REQUIRES`. Prefer validating behaviour through public APIs;
for the JSON builder, round-trip through the parser rather than asserting exact
whitespace.

---

## Soak & stress testing

Automated unit tests can't cover RF behaviour or long-run stability. Recommended
manual/semi-automated checks before a release:

- **24-72 h soak.** Run on real hardware and watch `heap_free` vs `heap_min`
  in **System** (or `GET /api/status`). A flat `heap_min` over days indicates no
  leak; a steady decline is a red flag.
- **Watchdog integrity.** Confirm no unexpected Task-WDT reboots in the event
  log over the soak window.
- **CSI throughput.** At the max sampling rate (100 Hz), confirm `csi.pps`
  tracks the setting and the packet ring never wedges (the ring stress test
  covers the data structure; this covers the live path).
- **Web load.** Open several dashboard tabs (up to `NEXUS_WS_MAX_CLIENTS`) and
  confirm pushes continue and stale sockets are reaped.
- **OTA cycle.** Flash a build via the browser, confirm reboot + rollback
  behaviour with a deliberately-bad image.
- **Provisioning loop.** Factory reset → AP → join → reboot, a few times.

A simple heap logger over time:

```bash
while true; do
  curl -s -b cookies.txt http://<ip>/api/status \
    | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d["heap_free"], d["heap_min"])'
  sleep 60
done
```

---

## Extending the firmware

### Add a REST endpoint

1. Write a handler `esp_err_t h_api_foo(httpd_req_t *req)` in `webserver.c`,
   following the existing handlers (call `require_auth(req, need_csrf)` for
   protected/mutating routes, build the body with `json_builder_t`, respond via
   the `send_json` helpers).
2. Register it in `webserver_start` and bump `max_uri_handlers` if needed.
3. Document it in [API.md](API.md) and, if the dashboard uses it, wire it in
   `web/js/app.js`.

### Add a configuration field

1. Add the field to `nexus_config_t` in [`config.h`](../firmware/main/config.h)
   and a default in `config_defaults`/`app_config.h`.
2. **Bump `NEXUS_CONFIG_VERSION`** so old NVS blobs are rejected and rebuilt.
3. Clamp it in `config_validate`, emit it in `config_to_json` (never emit
   secrets), and accept it in `config_apply_json`.
4. Surface it in the Settings form (`web/index.html` + `app.js`) and in
   [API.md](API.md).

### Add a new module / future component

New cross-cutting features (MQTT, HA, BLE, TinyML, ESP-NOW) should become
components under [`firmware/components/`](../firmware/components/README.md).
Consume the sensing core through `motion_get_result()` /
`motion_history_snapshot()` - don't reach into the pipeline. Register the
component and add it to the consumer's `REQUIRES`.

---

## Debugging tips

- `idf.py monitor` decodes panic backtraces automatically (addresses → symbols).
- Raise a module's log level at runtime: `esp_log_level_set("motion", ESP_LOG_DEBUG);`
- `idf.py size` / `idf.py size-components` to watch binary/RAM budgets.
- For CSI experiments, temporarily log raw `csi_sample_t` values from
  `csi_snapshot_samples()` to correlate the pipeline against real movement.

---

## Pull requests

- Keep changes modular and within existing boundaries.
- No new magic numbers; update `app_config.h` and the docs together.
- Ensure `idf.py build` is warning-free and the test app builds.
- Run `make -C tests/host`. It is fast, it needs nothing installed, and if your
  change touched the ring buffer, the logger, or the JSON builder it is the
  cheapest evidence you can attach to the PR.
- Update the relevant doc(s) and, for user-visible changes, the
  [changelog](../releases/CHANGELOG.md).
