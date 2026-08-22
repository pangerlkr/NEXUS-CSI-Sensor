# Project Structure

An annotated map of the repository. For *why* the firmware is arranged this way
see [ARCHITECTURE.md](ARCHITECTURE.md); this document is the *where*.

```
NEXUS-CSI-Sensor/
├── README.md                     Project overview, quick start, feature list
├── LICENSE                       MIT license
├── CONTRIBUTING.md               How to contribute: workflow, style, PR checklist
├── CODE_OF_CONDUCT.md            Community standards and how to report a problem
├── SECURITY.md                   Threat model, hardening notes, how to report a vuln
├── .gitignore                    Build output, generated sdkconfig, editor/OS cruft
├── .clang-format                 C formatting rules (run before every PR)
├── .editorconfig                 Indent/EOL/charset defaults for every file type
│
├── .github/
│   ├── PULL_REQUEST_TEMPLATE.md  PR form: what/why, testing, what you did NOT test
│   ├── ISSUE_TEMPLATE/
│   │   ├── config.yml            Template chooser + contact links, blank issues off
│   │   ├── bug_report.yml        Bug form: repro, serial log, board, IDF version
│   │   ├── detection_tuning.yml  False positives / missed people: room + thresholds
│   │   └── feature_request.yml   Feature form: problem first, then the proposal
│   └── workflows/
│       └── build.yml             CI: runs host tests, builds firmware + test app
│
├── firmware/                     ── ESP-IDF project root ──
│   ├── CMakeLists.txt            Top-level project file (project name + version)
│   ├── sdkconfig.defaults        Committed IDF configuration (CSI, partitions, WDT, …)
│   ├── partitions.csv            Dual-OTA + NVS + SPIFFS partition table (4 MB)
│   │
│   ├── main/                     ── firmware sources (one .c/.h per concern) ──
│   │   ├── CMakeLists.txt         Component registration + web-asset EMBED_FILES
│   │   ├── app_config.h           ★ Single source of truth for every constant
│   │   │
│   │   ├── main.c                 Boot sequence, task creation, health gate, watchdog
│   │   │
│   │   ├── wifi.c / .h            STA join, soft-AP fallback, IP/event handling
│   │   ├── csi.c / .h             CSI capture callback, feature extraction, traffic gen
│   │   ├── motion.c / .h          Signal pipeline + detection state machine + history
│   │   ├── display.c / .h         Optional ST7735 TFT status screen
│   │   ├── webserver.c / .h       HTTP server, REST API, /live WebSocket, OTA upload
│   │   │
│   │   ├── auth.c / .h            Sessions, CSRF, login rate-limiting, password hashing
│   │   ├── ota.c / .h             Streamed image write, verification, rollback
│   │   ├── logger.c / .h          Event ring buffer, NVS persistence, JSON/CSV export
│   │   ├── config.c / .h          Versioned CRC-checked NVS config blob + validation
│   │   ├── storage.c / .h         Thin error-checked NVS wrapper
│   │   │
│   │   ├── utils.c / .h           Time, clamp/map, hex, SHA-256, RNG, const-time compare
│   │   ├── ringbuffer.c / .h      Generic thread-safe circular buffer
│   │   └── json.c / .h            Streaming JSON builder + cJSON parse helpers
│   │
│   ├── components/               Reserved for future extractable components
│   │   └── README.md              What goes here + extraction guidance
│   │
│   └── test/                     ── on-target Unity test app ──
│       ├── CMakeLists.txt         Test project file
│       ├── README.md              How to run (hardware/QEMU), coverage scope
│       └── main/
│           ├── CMakeLists.txt     Reuses production ringbuffer/json/utils sources
│           └── test_main.c        Unit + stress TEST_CASEs
│
├── tests/                        ── tests that need no hardware ──
│   └── host/                      Native build of the portable modules, `make`
│       ├── Makefile               Builds and runs every suite, `SAN=1` for sanitizers
│       ├── README.md              How to run + what a green run does NOT prove
│       ├── test_ringbuffer.c      Ring buffer vs a reference model
│       ├── test_logger.c          Log ring read while it is being written to
│       ├── test_logger_restore.c  Boot-time NVS restore, including an oversized blob
│       ├── test_json.c            Builder escaping, floats, growth, error state
│       └── stubs/                 Minimal ESP-IDF + FreeRTOS fakes to link against
│
├── web/                          ── dashboard assets (embedded into firmware) ──
│   ├── index.html                Dashboard SPA (served at /)
│   ├── login.html                Login page (served at /login)
│   ├── css/
│   │   └── style.css             Dark "glass" cybersecurity UI styling
│   └── js/
│       └── app.js                Dashboard controller: WS/poll, charts, settings, OTA
│
├── docs/                         ── documentation set ──
│   ├── COMPONENTS.md              Parts list and what each component is for
│   ├── FLASHING.md                Step-by-step flashing guide (source + prebuilt)
│   ├── USAGE.md                   Provisioning, dashboard, and daily use
│   ├── USE_CASES.md               Real-world scenarios + responsible-use note
│   ├── FAQ.md                     The questions people actually ask, with honest limits
│   ├── INSTALLATION.md            Build-from-source + prebuilt-flash + provisioning
│   ├── CALIBRATION.md             Placement, calibration procedure, threshold tuning
│   ├── API.md                     Exact REST + WebSocket contract for every endpoint
│   ├── ARCHITECTURE.md            Modules, tasks, data flow, pipeline, resilience
│   ├── TROUBLESHOOTING.md         Symptom, cause, fix, grouped by phase
│   ├── DEVELOPER.md               Build/test/extend, conventions, soak testing
│   ├── CODE_REVIEW.md             Module-by-module static review, findings by severity
│   └── STRUCTURE.md               This file
│
├── hardware/
│   └── README.md                 Pinout, wiring, bill of materials
│
└── releases/
    ├── README.md                 How releases are built and versioned
    └── CHANGELOG.md              Version history
```

★ = read this first when changing behaviour.

---

## Where things live

| I want to change… | Go to |
|-------------------|-------|
| A threshold, timeout, pin, buffer size | [`firmware/main/app_config.h`](../firmware/main/app_config.h) |
| Detection maths / state machine | [`firmware/main/motion.c`](../firmware/main/motion.c) |
| CSI capture or the traffic generator | [`firmware/main/csi.c`](../firmware/main/csi.c) |
| A REST endpoint or the WebSocket | [`firmware/main/webserver.c`](../firmware/main/webserver.c) |
| Login / session / CSRF behaviour | [`firmware/main/auth.c`](../firmware/main/auth.c) |
| A persisted setting | [`firmware/main/config.c`](../firmware/main/config.c) + `config.h` |
| The dashboard look | [`web/css/style.css`](../web/css/style.css) |
| The dashboard behaviour | [`web/js/app.js`](../web/js/app.js) |
| The TFT layout | [`firmware/main/display.c`](../firmware/main/display.c) |
| Boot order / task creation | [`firmware/main/main.c`](../firmware/main/main.c) |
| Flash layout | [`firmware/partitions.csv`](../firmware/partitions.csv) |
| IDF build options | [`firmware/sdkconfig.defaults`](../firmware/sdkconfig.defaults) |
| Code formatting rules | [`.clang-format`](../.clang-format) |
| The bug or feature report form | [`.github/ISSUE_TEMPLATE/`](../.github/ISSUE_TEMPLATE/) |
| A test I can run without a board | [`tests/host/`](../tests/host/README.md) (`cd tests/host && make`) |
| A test that runs on the device | [`firmware/test/`](../firmware/test/README.md) |

---

## Layering rule

Dependencies flow **downward only**:

```
main  →  feature modules (wifi, csi, motion, display, webserver)
      →  services        (auth, ota, logger, config)
      →  primitives      (utils, ringbuffer, json, storage)
```

A primitive never includes a feature module; a service never includes
`webserver`. New code should respect this direction - it's what keeps the
future components in [`firmware/components/`](../firmware/components/README.md)
able to consume the sensing core without a rebuild of everything above it.
