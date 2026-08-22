# Contributing to NEXUS CSI Sensor

First off: thank you. A presence sensor that runs entirely on a WiFi radio is a
slightly unusual thing to build, and every improvement, bug report, and question
makes it better. This guide explains how to get involved without friction.

New here? The friendliest first contributions are documentation fixes, adding a
tested edge case, or trying the flashing guide on a board we have not listed yet
and telling us what happened.

---

## Ways to contribute

- **Report a bug.** Open an issue with the bug report template. Real logs and
  exact steps are worth more than a paragraph of description.
- **Suggest a feature.** Use the feature request template. The
  [roadmap](docs/USE_CASES.md) and the
  [`firmware/components/`](firmware/components/README.md) notes show where we are
  already heading.
- **Improve the docs.** If something in the guides was confusing, fixing it
  helps the next person more than you would think.
- **Send code.** Bug fixes, new REST endpoints, board support, signal-processing
  tuning, or one of the planned components.
- **Report a security issue.** Please do this privately. See
  [SECURITY.md](SECURITY.md), not the public issue tracker.

---

## Before you start on something big

For anything larger than a bug fix or a small self-contained addition, open an
issue first and say what you want to do. It is much nicer to agree on the shape
of a change before you have written 400 lines than after. Small fixes can go
straight to a pull request.

---

## Development setup

You need the ESP-IDF toolchain. The full walkthrough is in
[`docs/INSTALLATION.md`](docs/INSTALLATION.md) and the developer workflow is in
[`docs/DEVELOPER.md`](docs/DEVELOPER.md). The short version:

```bash
# Install ESP-IDF v5.2.x once, then in every new shell:
. ~/esp/esp-idf/export.sh

# Build and flash the firmware
cd firmware
idf.py set-target esp32
idf.py build
idf.py -p /dev/YOUR_PORT flash monitor
```

The on-target test app lives in [`firmware/test/`](firmware/test/README.md).

If you do not have a board yet, [`docs/COMPONENTS.md`](docs/COMPONENTS.md) lists
exactly what to buy, and much of the code can be read and reviewed without
hardware. You can also run part of it: the host suites in
[`tests/host/`](tests/host/README.md) build the ring buffer, the logger, and the
JSON builder with the compiler already on your machine, with no ESP-IDF install
at all.

```bash
make -C tests/host
```

---

## Project conventions

These keep the codebase readable and the diffs small. Most of them are enforced
mechanically so you do not have to memorise them.

- **Formatting is automated.** Run `clang-format` before you commit; the repo
  ships a [`.clang-format`](.clang-format) so everyone gets the same result.
  Editors that honour [`.editorconfig`](.editorconfig) will match indentation
  automatically.
- **One concern per module.** Each `.c` / `.h` pair owns one job. If you are
  adding a genuinely separate capability, see whether it belongs in
  [`firmware/components/`](firmware/components/README.md).
- **No magic numbers.** Thresholds, timeouts, pins, and buffer sizes live in
  [`firmware/main/app_config.h`](firmware/main/app_config.h). Add a named
  constant there rather than a literal in the logic.
- **Naming.** Functions are `module_verb_noun()` in `snake_case`
  (`motion_get_state`, `auth_check_session`). Public functions are declared in
  the header with a Doxygen comment.
- **Error handling.** Functions that can fail return `esp_err_t` and callers
  check it. Do not swallow errors silently.
- **Concurrency.** Shared state is read through a lock. If you add a field that
  more than one task touches, protect it and provide a snapshot getter rather
  than exposing the raw variable.
- **Comments explain why, not what.** The code says what it does; a comment
  should say why it is done that way when it is not obvious.

There is one house style rule for all prose and comments in this repo: **do not
use em-dashes.** Use a comma, a colon, or a full stop. This keeps the writing
consistent across every file.

The deeper "how to add an endpoint / a config field / a component" recipes are
in [`docs/DEVELOPER.md`](docs/DEVELOPER.md).

---

## Pull request checklist

Before you open a PR, please make sure:

- [ ] The firmware builds cleanly: `idf.py build` with no new warnings.
- [ ] `make -C tests/host` passes. This one needs no board and no ESP-IDF, so
      there is no excuse for skipping it.
- [ ] You ran `clang-format` on changed files.
- [ ] Any new tunable is a named constant in `app_config.h`, not a literal.
- [ ] New shared state is lock-protected.
- [ ] You updated the docs that describe the behaviour you changed (API,
      configuration, usage) and the [CHANGELOG](releases/CHANGELOG.md) if the
      change is user-visible.
- [ ] Tests still pass, and you added a test if you fixed a logic bug or added a
      primitive. A host test in [`tests/host/`](tests/host/README.md) is usually
      the easiest place to put one.
- [ ] Your prose contains no em-dashes.

Keep pull requests focused. One logical change per PR is much easier to review
and to revert if something goes wrong. Write a commit message that explains the
why, not just the what.

---

## Reviews and merging

A maintainer will review as soon as they can. Expect questions; they are about
the code, never about you. Once it is approved and CI is green, a maintainer
merges it. If your PR goes quiet for a while, a polite nudge is welcome.

---

## Code of conduct

Taking part in this project means agreeing to the
[Code of Conduct](CODE_OF_CONDUCT.md). In short: be kind, assume good faith, and
help make this a place people want to contribute to.
