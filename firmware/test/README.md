# Tests

On-target [Unity](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/unit-tests.html)
test suite for the modules that carry the sensing logic but do **not** depend
on live radio state:

- **`ringbuffer`** - FIFO ordering, overwrite-when-full, oldest-first
  snapshots, and a 100 000-element wrap-around stress test.
- **`json`** - builder/parser round-trip of every scalar type, nested
  objects/arrays, malformed-input rejection, and a capacity-growth stress test.
- **`utils`** - clamp/map maths, hex encoding, a SHA-256 known-answer vector,
  random-token charset, constant-time compare, uptime formatting, `strlcpy`.

The test application reuses the production `.c` files directly from
[`../main`](../main), so the code exercised here is byte-for-byte the code that
ships in the firmware.

## Run on hardware

```bash
idf.py -C firmware/test set-target esp32
idf.py -C firmware/test flash monitor
```

Unity prints a per-case pass/fail summary and a final total over the serial
monitor. Exit the monitor with `Ctrl-]`.

## Run under QEMU (no board required)

```bash
idf.py -C firmware/test set-target esp32
idf.py -C firmware/test qemu monitor
```

## Continuous integration

`.github/workflows/build.yml` builds this test app on every push, so it can
never silently break the build. Executing the cases still requires hardware or
QEMU (see above).

## The faster suite

[`../../tests/host/`](../../tests/host/README.md) builds `ringbuffer.c`,
`logger.c` and `json.c` with your machine's own compiler and runs them in a
couple of seconds, with no ESP-IDF and no QEMU:

```bash
make -C tests/host
```

It covers fewer modules than this app and its FreeRTOS stubs are a counter
rather than a scheduler, so it cannot speak to real concurrency. Use it while
you are editing, and use this app before you trust a change on hardware.

## What is *not* covered here

Modules with hard dependencies on the Wi-Fi driver, SPI panel, NVS or the HTTP
stack (`wifi`, `csi`, `display`, `webserver`, `ota`, `storage`, `auth`) are
validated with the end-to-end procedures in
[`../../docs/CALIBRATION.md`](../../docs/CALIBRATION.md) and the soak/stress
recommendations in [`../../docs/DEVELOPER.md`](../../docs/DEVELOPER.md).
