<div align="center">

# NEXUS CSI Sensor

### See presence and motion in a room using only WiFi. No camera. No microphone. No infrared. Just radio waves and math.

![Platform](https://img.shields.io/badge/platform-ESP32-000000?logo=espressif&logoColor=white)
![Framework](https://img.shields.io/badge/framework-ESP--IDF%20v5.x-E7352C?logo=espressif&logoColor=white)
![Language](https://img.shields.io/badge/firmware-C%20%2F%20FreeRTOS-00599C?logo=c&logoColor=white)
![Sensing](https://img.shields.io/badge/sensing-WiFi%20CSI-38bdf8)
![Privacy](https://img.shields.io/badge/privacy-camera--free-22c55e)
![License](https://img.shields.io/badge/license-MIT-yellow)

</div>

---

## The 30 second pitch

Every WiFi packet that crosses a room bounces off the walls, the furniture, and you. When you move, those reflections change. NEXUS listens to that change and turns it into a live presence and motion reading.

That is the whole trick. A five dollar chip you probably already own can tell whether a room is empty, whether someone is standing still in it, or whether someone is moving around, all without a single camera pointed at anyone.

You get a clean, real time dashboard in your browser, a login screen protecting it, an optional little screen on the device itself, and firmware you can update over the air. It is built to run for months on a shelf and quietly self heal if anything hangs.

> **Why you might love this:** it is genuinely private by design. There is no lens, no audio, no image of you stored anywhere, because none of that data is ever captured in the first place. Presence is inferred from radio noise, not from watching you.

---

## How it works, in plain language

Think of WiFi in a room like light in a pool of water. When the water is perfectly still, the pattern of light on the bottom is steady. Drop a pebble in, and the ripples scatter that light everywhere. A person walking through a room is the pebble, and the WiFi signal is the light.

The ESP32 radio can read a detailed fingerprint of each packet it receives, called **Channel State Information (CSI)**. NEXUS measures how much that fingerprint is "rippling" compared to a learned picture of the empty, still room. A little ripple means presence. A lot of ripple means motion. A storm of ripple means someone is really moving.

There is one practical catch. CSI only updates when packets actually arrive, and a quiet network sends very few. So NEXUS keeps a gentle heartbeat of pings going to your router, just enough traffic to keep the measurements flowing. It costs almost no bandwidth and you never have to think about it.

If you want the real signal processing detail, it lives in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#5-the-signal-processing-pipeline).

---

## What it can do

**Sensing**
- Detects four levels of activity: **Idle**, **Presence**, **Motion**, and **High Motion**.
- Reports live motion intensity, a sustained activity level, and a signal quality score.
- Learns your specific room automatically and adapts to slow drift over time.
- Freezes that learning the instant it sees motion, so a person who stops moving is never mistaken for background.

**The dashboard**
- A dark, glassy, cybersecurity styled web UI that runs entirely on the device.
- Live updates over a WebSocket, with charts that keep drawing even with no internet.
- An event log you can read on screen or download as a CSV file.
- A settings page for everything, and a firmware upload page with a progress bar.

**On the device**
- An optional 1.8 inch color screen shows presence, motion level, signal quality, packets per second, the device name, and its IP address.
- No screen attached? No problem. It runs perfectly headless.

**Staying alive**
- A watchdog reboots the device automatically if any task ever stalls.
- Over the air updates roll back on their own if a bad image fails to boot.
- If it cannot reach your WiFi, it falls back to hosting its own setup network so you are never locked out.

---

## Security is not an afterthought here

This device sits on your network and reports on your space, so it was built to be defended, not just demoed. Here is exactly what protects it.

| Layer | What it does | Why it matters |
|-------|--------------|----------------|
| **Login required** | The dashboard is gated by a username and password. | Nobody on your LAN can read your presence data by guessing the IP. |
| **Hashed passwords** | Credentials are stored as a salted SHA-256 hash, never as plain text. | Even with physical access to the flash, the password is not sitting there readable. |
| **Constant time checks** | Password comparison takes the same time whether it is right or wrong. | Closes the door on timing attacks that leak the password one character at a time. |
| **Session cookies** | Sessions use HttpOnly, SameSite=Strict cookies with a one hour expiry. | JavaScript cannot steal the token, and other sites cannot ride your session. |
| **CSRF tokens** | Every action that changes something requires a matching anti forgery token. | A malicious page you visit cannot trick your browser into rebooting the sensor. |
| **Login rate limiting** | Too many wrong guesses locks out that client for two minutes. | Brute forcing the password becomes impractical. |
| **Verified OTA + rollback** | New firmware is checked before it runs, and reverts if it crash loops. | A failed or tampered update cannot brick the device. |
| **Hardening headers** | Every response sends nosniff and anti framing headers. | Standard web hardening against sniffing and clickjacking. |

> ### Please do this before anything else
> The device ships with the default login `admin` / `nexus-admin`. Change it the moment you log in for the first time, under **Settings**. Treat this like any other device on your network: keep it on trusted WiFi, and do not expose it directly to the public internet. The dashboard speaks plain HTTP on your LAN, which is fine at home behind your router, but it is not meant to face the open web without a reverse proxy and TLS in front of it.

The full security model, down to the wire level flow, is documented in [docs/API.md](docs/API.md#authentication-model) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#9-security-architecture).

---

## What you need

The short version: an ESP32 and a USB cable. That is the real minimum.

| Item | Required? | Notes |
|------|-----------|-------|
| ESP32-WROOM-32 board | Yes | Any classic ESP32 with 4 MB flash. An ESP8266 will not work. |
| USB data cable | Yes | Must carry data, not just power. |
| 2.4 GHz WiFi | Yes | The sensor is a WiFi client. It is 2.4 GHz only. |
| Stable 5V USB power | Yes | A weak supply causes brownouts that corrupt readings. |
| 1.8 inch ST7735 screen | Optional | Adds the on device status display. |
| A few jumper wires | Optional | Only if you add the screen. |

The full parts list, the reasons behind each choice, and the exact wiring for the optional screen are in [docs/COMPONENTS.md](docs/COMPONENTS.md).

---

## Get it running

Three honest steps, plus the detail behind each one.

1. **Flash the firmware onto the board.** Follow [docs/FLASHING.md](docs/FLASHING.md). It walks you through both the quick path (a prebuilt binary) and the full path (building from source), with the exact commands and how to fix the usual snags.
2. **Point it at your WiFi.** On first boot the device hosts its own network called `NEXUS-CSI-Setup`. Join it from your phone, and a setup page lets you enter your real WiFi details. Full walkthrough in [docs/USAGE.md](docs/USAGE.md#first-run-connect-it-to-your-wifi).
3. **Open the dashboard, log in, and calibrate.** Change the default password, then run a quick calibration so it learns your room. See [docs/USAGE.md](docs/USAGE.md) and [docs/CALIBRATION.md](docs/CALIBRATION.md).

That is it. From there it just runs.

---

## What the dashboard looks like

When you log in you land on a live view. The top of the screen shows the current state as a glowing ring that shifts color as activity rises, from a calm idle circle to a bright high motion burst. Below that, status cards track the raw motion score, packets per second, signal strength, CSI variance, and the learned baseline, each with its own little live chart.

A sidebar takes you to the event log (every state change, calibration, reboot, and warning, newest first), the settings page, and the system and firmware update view. Everything updates in real time over a WebSocket, and if that connection ever drops the page quietly falls back to polling so you never stare at stale numbers.

---

## Where it shines

A few of the things people build with this. The full set, with setup notes and an important ethics and legal section, is in [docs/USE_CASES.md](docs/USE_CASES.md).

- **Presence based home automation.** Turn lights or heating on when a room is genuinely occupied, off when it is truly empty, without a camera in your living room.
- **Gentle wellness awareness.** Know that an elderly relative is moving around normally during the day, with far more privacy than a camera and no wearable to remember.
- **Occupancy for energy saving.** Stop heating, cooling, or lighting empty rooms.
- **Learning and research.** A hands on, affordable way to explore WiFi sensing and signal processing on real hardware.

---

## Project map

```
NEXUS-CSI-Sensor/
├── firmware/        The ESP-IDF firmware (C, FreeRTOS). One file per concern.
├── web/             The dashboard, embedded straight into the firmware binary.
├── tests/           Tests you can run on a laptop, no board required.
├── docs/            Guides, both the friendly ones and the deep technical references.
├── hardware/        Wiring and parts reference.
└── releases/        Changelog and release process.
```

A full annotated, file by file tour is in [docs/STRUCTURE.md](docs/STRUCTURE.md).

---

## Documentation index

Start with the friendly guides. Drop into the references when you want the exact detail.

**Start here**
- [Components you need](docs/COMPONENTS.md) - the parts list and why
- [Flashing guide](docs/FLASHING.md) - get the firmware onto the board
- [Usage guide](docs/USAGE.md) - provisioning, the dashboard, and daily use
- [Calibration guide](docs/CALIBRATION.md) - teach it your room
- [Use cases](docs/USE_CASES.md) - ideas, plus the ethics and legal note
- [FAQ](docs/FAQ.md) - what it can and cannot do, answered honestly

**Technical references**
- [API reference](docs/API.md) - every REST endpoint and the WebSocket
- [Architecture](docs/ARCHITECTURE.md) - modules, tasks, signal pipeline, resilience
- [Developer guide](docs/DEVELOPER.md) - build, test, and extend the firmware
- [Project structure](docs/STRUCTURE.md) - the file by file map
- [Troubleshooting](docs/TROUBLESHOOTING.md) - symptom, cause, fix
- [Code review](docs/CODE_REVIEW.md) - known issues by severity, and a good first-PR list
- [Host tests](tests/host/README.md) - `cd tests/host && make`, no board needed, and an honest list of what that does not prove

**Taking part**
- [Contributing](CONTRIBUTING.md) - workflow, house style, and the PR checklist
- [Code of Conduct](CODE_OF_CONDUCT.md) - what we expect of each other
- [Security policy](SECURITY.md) - the threat model, and how to report a vulnerability

---

## Roadmap

The core is deliberately built so these can slot in without a rewrite. Design notes are in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#11-future-ready-architecture).

- MQTT and Home Assistant integration
- BLE based WiFi provisioning as an alternative to the setup network
- On device TinyML to classify activity types, not just detect motion
- Multi node sensing with ESP-NOW for whole home coverage
- An optional cloud bridge for remote history

---

## A word on responsible use

This is a sensing device. Detecting presence and motion in a space, even without a camera, can affect other people's privacy. Only deploy it in spaces you own or have clear permission to monitor, tell the people who share that space, and check your local laws. There is a fuller discussion in [docs/USE_CASES.md](docs/USE_CASES.md#using-this-responsibly). Please build cool things, and be kind while you do it.

---

## License

Released under the [MIT License](LICENSE). Use it, learn from it, build on it, ship it. A credit back is always appreciated but never required.

<div align="center">

**Built for makers who would rather understand their tools than just use them.**

</div>
