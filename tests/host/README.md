# Host tests

Four test suites that build the platform-independent firmware modules with the
compiler already on your machine and run them as ordinary programs. No ESP-IDF,
no toolchain install, no board, no QEMU. On a laptop the whole thing takes a
couple of seconds.

```bash
cd tests/host
make
```

That builds one binary per suite into `build/`, runs all of them, and prints
`all suites passed` or a non-zero exit code with the failures named. To run one
on its own:

```bash
cd tests/host && make && ./build/test_logger
```

`make clean` removes `build/`. `make SAN=1` rebuilds with the address and
undefined-behaviour sanitizers on, which is worth doing after any change to the
ring buffer arithmetic; all four suites pass clean under it today. That target sets
`ASAN_OPTIONS=detect_leaks=0`, because `test_logger_restore` re-runs
`logger_init()` on purpose and the logger has no deinit to pair with it. If you run
a sanitized binary by hand on Linux, set the same variable or expect leak reports
you already know about. Override the compiler with `make CC=gcc-14` if you want a
second opinion. The suites build clean under `-Wall -Wextra -Wshadow
-Wpointer-arith -Wcast-align -Wstrict-prototypes -Wwrite-strings`, so a new
warning is a real finding rather than noise to scroll past.

## What is tested, and why these four

The suites compile the real `firmware/main/*.c` files. Nothing is copied or
reimplemented, so a fix here is a fix in the shipping firmware.

**`test_ringbuffer`** builds a reference model in plain arrays and compares it
against the ring for every interesting state: empty, partially filled, exactly
full, and wrapped several times over. `ringbuffer_peek()` is checked element by
element against `ringbuffer_snapshot()`, because the two compute the
oldest-element position separately and a disagreement between them is exactly
the bug that would silently reorder an event log. It also checks that an
out-of-range peek leaves the caller's buffer untouched, that the mutex is
released on every exit path including the failure ones, and that
`ringbuffer_push()` and `ringbuffer_push_timeout()` keep their different return
conventions.

**`test_logger`** exists for one specific hazard. The log ring is read newest
first while other tasks are still writing to it, and every write shifts each
remaining position down by one. Without a guard, one entry can be emitted twice
in the same JSON response. The test drives `logger_to_json()` through a stub
that calls `logger_log()` from inside the emit loop, at the start, at the end,
and in the middle, then asserts that sequence numbers come out strictly
decreasing and that no entry appears more than once. The printed line for each
case shows how many entries came out, so the one-row shortfall of an
oldest-first walk is visible rather than hidden behind a pass.

**`test_logger_restore`** covers the boot path that reads persisted entries back
out of NVS, which is finding 3 in
[the code review](../../docs/CODE_REVIEW.md). That bug was invisible at the call
site: `nvs_get_blob()` reads its length argument as the capacity of the caller's
buffer, so passing the stored blob's size while having allocated room for only
`NEXUS_LOG_CAPACITY` entries let NVS write past the end of the allocation. The
`storage_get_blob()` stub here models `nvs_get_blob()` faithfully rather than
conveniently, which puts that overflow back in reach, and the suite runs an empty
key, a partial blob, an exactly-full blob, an oversized blob, and a blob that is
not a whole number of entries.

**`test_json`** covers the failure mode where a single bad character makes the
whole API response unparseable and the dashboard just stops updating. It checks
escaping of quotes, backslashes, the `\n\t\r\b\f` set, control bytes as
`\u00xx`, and a NULL string pointer; that a non-finite float is substituted on
both the key and the array-element paths; comma placement across nested objects
and arrays; growth from a deliberately absurd 1-byte initial capacity through
2000 elements; and that a builder already in its error state returns an empty
string rather than a NULL that a caller would hand to `strlen`.

## Do the tests actually catch anything

A suite that passes proves nothing on its own, so both of the bugs these were
written for were put back temporarily to check that the tests notice. Reverting
the NVS length fix produces:

```
FAIL: get_blob asked for 9504 bytes, more than the 6912 the ring can hold.
      This is finding 3 back again.
```

and disabling the sequence-number guard in `logger_to_json()` produces
`FAIL: full, writer at 1: seq 94 emitted twice`. If you change either of those
code paths, do the same thing: break it on purpose first and confirm you get a
red run, then fix it. A test you have never seen fail is a test you do not know
the strength of.

## What this does not prove

Worth being blunt about, because a green run here is easy to over-read.

**It is not a build of the firmware.** These suites touch `ringbuffer.c`,
`logger.c` and `json.c`. Everything that talks to the radio, the SPI panel, NVS,
or the HTTP stack is absent. `idf.py build` is still the only thing that will
tell you the firmware compiles.

**Concurrency is simulated, not real.** The FreeRTOS stubs in `stubs/` are a
counter, not a scheduler. `test_logger` reproduces the interleaving that the
dedupe guard exists for by calling the writer at a chosen point in the reader's
loop, which is a deterministic re-enactment of one ordering. It is not evidence
that the locking is correct under a preemptive scheduler on two cores.

**The JSON parser is not covered.** `stubs/cJSON.h` is a link-time shim that
returns nothing, so `json_parse()` and the `json_get_*()` family cannot run
here. The tests never call them. Changes to that half of `json.c`, including the
`json_get_int()` fix in the current changelog, need a device build.

**NVS is a stand-in, not NVS.** `test_logger_restore` models the one behaviour of
`nvs_get_blob()` that the bug depended on. It says nothing about wear levelling,
a partition that fills up, a blob written by an older firmware version, or what
happens when a write is interrupted by a power cut. `logger_persist()` is called,
but the write itself goes to a stub that always succeeds.

**Some failures stay unreachable.** `esp_task_wdt` and `esp_log` do nothing here,
and allocation failure is only injected where a test asks for it, so an
out-of-memory path that no test names is still untested.

## Layout

| Path | What it is |
|------|-----------|
| `Makefile` | Build and run. One binary per suite, so fault injection in one cannot leak into another. |
| `test_ringbuffer.c` | Ring buffer against a reference model. |
| `test_logger.c` | Log ring read under concurrent writes. |
| `test_logger_restore.c` | The NVS restore path at boot, including the oversized blob. |
| `test_json.c` | JSON builder escaping, floats, growth, error state. |
| `stubs/` | Just enough ESP-IDF and FreeRTOS to link. Read these before trusting a result. |
| `build/` | Generated. Gitignored. |

Two stubs are worth knowing about. `stubs/freertos/semphr.h` makes the mutex a
`{held, takes, gives}` counter, and `g_lock_fail` lets a test force the next take
to fail so the release-on-error paths get walked. The `storage_*` stubs live in
the test files rather than in `stubs/`, because each suite wants NVS to behave
differently and a shared version would end up with a flag for every caller.

## The other test suite

[`firmware/test/`](../../firmware/test/) is a separate, on-target
[Unity](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/unit-tests.html)
application. It covers more modules and runs the real scheduler, but it needs
ESP-IDF plus a board or QEMU. The two are complements: run the host suites while
you are editing, run the Unity app before you trust a change on hardware.

## Adding a suite

1. Write `test_<module>.c` with a `main()` that returns non-zero on failure.
2. Add the name to `SUITES` in the `Makefile` and a `test_<module>_SRCS` line
   listing your file plus the production sources it needs. The build rule is
   generic, so those two lines are the whole wiring.
3. If it pulls in a new ESP-IDF header, add the smallest possible stub and say
   in the stub's header comment what it fakes and what that makes untestable.
   A stub that quietly lies is worse than no test.
4. Break the code on purpose and watch your test fail before you trust it.

The last one matters most for a suite that models an ESP-IDF API. `test_logger_restore`
only catches finding 3 because its stub reproduces the awkward half of
`nvs_get_blob()`; a stub written for convenience would have passed on the broken
code too.
