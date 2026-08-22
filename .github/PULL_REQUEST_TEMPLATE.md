<!--
Thanks for contributing. Fill in what applies and delete what does not.
A short honest PR description saves a whole round of review questions.
Full guidance: CONTRIBUTING.md
-->

## What this changes

<!-- One or two sentences. What is different after this merges? -->

## Why

<!-- The reason, not a restatement of the diff. What was broken, missing, or
     awkward? Link the issue if there is one: "Fixes #12" / "Part of #34". -->

## How it works

<!-- Only if the approach is not obvious from the diff. Mention anything a
     reviewer would otherwise have to reverse-engineer: a new lock, a changed
     task interaction, why you picked one algorithm over another. -->

---

## Type of change

- [ ] Bug fix (no behaviour change beyond fixing the bug)
- [ ] New feature
- [ ] Refactor or cleanup (no functional change intended)
- [ ] Documentation only
- [ ] Build, CI, or tooling
- [ ] Security fix (please also read SECURITY.md before opening this publicly)

## Breaking changes

- [ ] No breaking changes
- [ ] Breaks something, described below

<!-- If it breaks something, say what and what users have to do about it:
     a config key renamed, an API response shape changed, NVS layout altered
     (does an existing device need a factory reset after upgrading?). -->

---

## How you tested it

Be specific. "Tested on hardware" tells a reviewer almost nothing; "flashed on a
WROOM-32, watched the score for 20 minutes in an empty room, then walked through"
tells them a lot.

- [ ] `make -C tests/host` passes (no board or ESP-IDF needed, takes seconds)
- [ ] `idf.py build` succeeds with no new warnings
- [ ] Flashed and ran on real hardware. Board used: <!-- e.g. ESP32-WROOM-32 DevKitC -->
- [ ] On-target tests pass (`firmware/test/`)
- [ ] Exercised the dashboard in a browser, if this touches the web layer
- [ ] Checked the serial log for new warnings, resets, or watchdog trips

**What I actually did:**

<!-- Steps, and what you observed. Include the relevant serial output or a
     screenshot if it helps. -->

## What I did not test

<!-- This is the most useful box on the form. No display connected? No 5 GHz
     router to try? Could not reproduce the original bug reliably? Say so.
     An honest gap is fine; a silent one wastes the reviewer's time. -->

---

## Checklist

- [ ] Any new tunable is a named constant in `firmware/main/app_config.h`, not a
      literal buried in a `.c` file
- [ ] New shared state is protected by a lock, and I said which one in the code
- [ ] Allocations are checked, and freed on every path out including error paths
- [ ] I ran `clang-format` on the files I touched (`.clang-format` is in the root)
- [ ] Docs updated for behaviour I changed (API, configuration, usage,
      architecture) and `releases/CHANGELOG.md` if this is user-visible
- [ ] My prose contains no em-dashes (house style, see CONTRIBUTING.md)

## Anything else

<!-- Open questions for the reviewer, a decision you were unsure about, a
     follow-up you deliberately left out of scope. "I am not sure this is the
     right place for this function" is a completely reasonable thing to write. -->
