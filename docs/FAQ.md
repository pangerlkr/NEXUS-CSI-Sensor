# Frequently Asked Questions

The questions people actually ask, in roughly the order they ask them. If you
want a procedure rather than an explanation, the guides are better: start at
[USAGE.md](USAGE.md) for daily use, [CALIBRATION.md](CALIBRATION.md) for tuning,
and [TROUBLESHOOTING.md](TROUBLESHOOTING.md) when something is broken.

---

## The basics

### What is this, in one paragraph?

An ESP32 sits on your WiFi and listens to the fine detail of the radio signal it
receives, called Channel State Information. When a person moves through a room,
their body changes how the radio waves bounce around, and that shows up as
measurable variation in the CSI. The firmware turns that variation into a single
score between 0.00 and 1.00, then into four states: Idle, Presence, Motion, and
High Motion. It serves a dashboard from the device itself so you can watch it
live.

### Does it use a camera?

No. There is no camera, no microphone, no PIR sensor, no ultrasonic transducer,
and no radar module. The only sensor is the WiFi radio that is already inside the
ESP32. That is the entire point of the project: an ESP32 and a USB cable is the
real, complete parts list.

### So how does it detect me without any of that?

Radio waves reflect off everything in a room, including you. They arrive at the
receiver by many paths at once, and those paths add up into a pattern of
amplitudes across the WiFi subcarriers. Move a body through the room and you
change the geometry, so the pattern shifts. Stand perfectly still and it settles
into a new pattern that is still different from an empty room.

The firmware measures how much that pattern is varying over a short window,
compares it against a learned baseline for your specific empty room, and
normalises the difference. There is a much fuller walkthrough in
[ARCHITECTURE.md](ARCHITECTURE.md#5-the-signal-processing-pipeline).

### Does it need the internet?

No. It needs a 2.4 GHz WiFi network to be a client of, because that is where the
CSI comes from, but nothing leaves your LAN. The dashboard, the charts, the API,
and the update mechanism all run on the device. There is no cloud account, no
telemetry, and no phone-home. Unplug your router's uplink and it keeps working.

### Does it need a router at all?

For sensing, yes. The device measures the signal on packets it receives, so it
needs something transmitting to it. On first boot, or when it cannot reach your
saved network, it hosts its own setup access point called `NEXUS-CSI-Setup`,
but that mode exists for provisioning rather than for sensing.

---

## What it can and cannot detect

### Can it tell me how many people are in the room?

No. It produces one aggregate score for the space, not a count. Two people
moving usually push the score higher than one, but not in a way you could
calibrate into a reliable headcount. If you need occupancy counting, this is not
that device.

### Can it identify who is in the room?

No, and it is not built to. There is no identity model, no gait signature, and no
device fingerprinting. It reports that the space is active, not who made it
active.

### Will it detect someone sitting completely still?

Often, yes, and this is one of the better parts of the design. A still human body
still breathes, shifts weight, and moves slightly, and that is usually enough to
keep the score above the presence threshold even when it is well below the motion
threshold. That is exactly what the separate Presence and Motion states are for.

The trick that makes this work is that baseline learning freezes the moment
motion is detected. A naive adaptive system slowly learns a stationary person as
"background" and then declares the room empty while they are sitting in it. This
one refuses to update its idea of "empty" while it believes someone is there.

That said, a very still person over a long period is the hardest case in all of
CSI sensing. If somebody naps on the sofa for an hour, expect the score to sag.
Lowering the presence threshold helps; see
[CALIBRATION.md](CALIBRATION.md#4-tuning-the-thresholds).

### Does it see through walls?

Somewhat, and this cuts both ways. Radio passes through plasterboard and wood
fairly well, so movement in an adjacent room can register. That is genuinely
useful if you want whole-flat coverage from one node, and genuinely annoying if
you want one specific room and keep picking up the hallway.

Placement is your main control here. Putting the sensor and the router on the
same side of the room you care about, so the direct path crosses that room rather
than the neighbouring one, makes a large difference. Brick, concrete, and
anything with metal in it attenuate much more than plasterboard.

### What is the detection range?

There is no single number, because the sensing volume is the radio path between
the router and the sensor, not a cone in front of the device. Practically:
movement anywhere in the room containing that path is usually detected, movement
in adjacent rooms is often detected, and movement far off the path in a large
open space may not be. A typical room of 4 by 5 metres is comfortably covered by
one node.

### Can it tell the difference between a person and my cat?

Not reliably. A large dog moving around will look a lot like a person. A cat
usually produces a weaker signal and may sit below your presence threshold, but
tuning it so cats never trigger it and humans always do is fiddly and depends on
your room. If you have pets, expect to spend real time on
[calibration](CALIBRATION.md), and expect some overlap.

### What about fans, air conditioning, and curtains?

Anything that moves repeatedly will contribute to the signal. A ceiling fan on a
timer is the single most common cause of a restless empty-room baseline. Because
the baseline is adaptive, a fan that runs constantly gets learned as background
and mostly stops mattering. A fan that switches on and off is harder, because it
looks like something arriving in the room.

If you have one, calibrate with it running the way it normally runs, and see the
interference notes in [TROUBLESHOOTING.md](TROUBLESHOOTING.md#detection-quality).

### Can it classify what someone is doing?

Not today. It reports how much movement there is, not what kind. Activity
classification with an on-device TinyML model is on the roadmap, and the
architecture was deliberately laid out so that a classifier can consume the
existing feature pipeline without a rewrite. See
[ARCHITECTURE.md](ARCHITECTURE.md#11-future-ready-architecture).

---

## Hardware and compatibility

### Which exact board should I buy?

A classic ESP32-WROOM-32 development board with 4 MB of flash. An ESP32 DevKitC
or any of the common clones is fine. The full reasoning, plus what to avoid, is
in [COMPONENTS.md](COMPONENTS.md).

### Will an ESP8266 work?

No. The ESP8266 WiFi driver does not expose CSI. This needs an ESP32.

### What about the ESP32-S3, C3, C6, or an ESP32-CAM?

The build targets `esp32` (see `CONFIG_IDF_TARGET` in
`firmware/sdkconfig.defaults`), and that is the only target this project has been
developed against. Several newer chips do support CSI in ESP-IDF, so porting is
plausible rather than fanciful, but it is not a matter of just changing the
target. You would need to check CSI support for that specific chip in your IDF
version, revisit the SPI pins and host for the optional display in
`app_config.h`, and confirm the partition table fits the flash on your module.

If you get another target working, a pull request would be genuinely welcome.

An ESP32-CAM board will physically work as a plain ESP32, and the camera will sit
there unused. That is not a recommendation, it is just not a blocker.

### Do I have to connect the display?

No. The ST7735 is entirely optional and the firmware runs headless without
complaint. If you want one, wiring is in
[COMPONENTS.md](COMPONENTS.md#wiring-the-display) and you can also compile the
driver out completely by setting `NEXUS_ENABLE_DISPLAY` to 0 in `app_config.h`.

### Does it need a battery or special power supply?

A stable 5 V USB supply. This matters more than it sounds: a weak or noisy
supply causes brownouts, and brownouts show up as garbage in your readings before
they show up as crashes. A cheap phone charger that sags under load is a real
source of mystery false positives. Use a decent supply and a cable that carries
data, not just power.

### How much does it cost to run?

It is an ESP32 at 240 MHz with the radio active, so on the order of a few hundred
milliwatts. Cents per month.

---

## Network questions

### Why 2.4 GHz only?

The ESP32's radio is 2.4 GHz. There is no 5 GHz option to enable. If your router
runs a single combined network name across both bands, the device will simply
join on 2.4 GHz. If your 2.4 GHz radio is disabled entirely, the device cannot
connect at all, which is a surprisingly common first-run problem.

### Will this slow down my WiFi?

Barely. The device is one more client, and the traffic generator sends small
ICMP pings to your gateway at the sampling rate, 20 per second by default, to
keep packets flowing when the network is otherwise idle. That is a trivial load
on any modern router. If you want it quieter, lower the sampling rate in
Settings, or compile the generator out with `NEXUS_ENABLE_TRAFFIC_GEN`.

### Why does it need to generate traffic at all?

CSI is measured from received packets. No packets means no measurements, and an
idle network can go quiet for long stretches. Pinging the gateway guarantees a
steady stream of receptions to measure, which is what keeps the detection latency
predictable.

### Does it work on a hidden SSID, or on WPA3, or on a captive portal network?

Hidden SSIDs work if you type the name exactly. WPA2 and WPA2/WPA3 mixed mode
work. Networks that require a browser login (hotels, universities, most guest
networks) will not work, because the device has no way to complete a captive
portal sign-in. Enterprise WPA2-E with per-user credentials is not supported.

### Can I reach the dashboard from outside my house?

You can, but do not simply forward a port to it. The dashboard speaks plain HTTP,
which is fine on your own LAN behind a router and not fine on the open internet.
If you need remote access, put it behind a VPN, or behind a reverse proxy that
terminates TLS and requires its own authentication. See the deployment note in
the [README](../README.md#security-is-not-an-afterthought-here).

### How many people can watch the dashboard at once?

Four WebSocket clients and four sessions, set by `NEXUS_WS_MAX_CLIENTS` and
`NEXUS_MAX_SESSIONS`. Beyond that, the oldest session gets evicted. These are
compile-time constants you can raise, at the cost of RAM.

---

## Privacy, ethics, and the law

### Is this less invasive than a camera?

In one clear way, yes: there is no image, so there is nothing to leak, nothing to
recognise a face in, and nothing that is embarrassing if the flash is dumped. The
device stores a score history and an event log, not footage.

In another way it deserves more thought, not less. A camera is visible and people
understand what it does. A small box on a shelf that senses through a wall is not
obvious at all, and that invisibility is precisely what makes it worth being
careful with.

### Do I need to tell people it is there?

Ethically, yes, and we would ask you to. Legally, it depends where you live and
whose space it is. The honest position: only deploy this in spaces you own or
have clear permission to monitor, tell the people who share that space, and check
your local law before you get creative. There is a fuller discussion in
[USE_CASES.md](USE_CASES.md#using-this-responsibly).

### Can I use it to monitor a tenant, an employee, or a housemate without telling them?

Please do not, and we will not help with it. Covert monitoring of people who have
not consented is outside what this project is for, and issues asking for help
with it will be closed. See the project-specific section of the
[Code of Conduct](../CODE_OF_CONDUCT.md).

Elderly care monitoring with the knowledge and agreement of the person being
monitored is a completely different thing, and is one of the use cases this was
built for.

### What data does it keep, and where?

On the device, in flash, and nowhere else:

- Configuration, including the salted password hash, in NVS.
- The most recent 64 event log entries, persisted to NVS periodically.
- 120 points of score history, in RAM only. This is lost on reboot.

A factory reset clears configuration and logs. See
[USAGE.md](USAGE.md#factory-reset).

### Why are the log timestamps not real dates?

Because the firmware does not set its clock. There is no SNTP client, so the
device has no idea what day it is; log entries are stamped with milliseconds
since boot instead. That is deliberate for now, since it removes a network
dependency, but it does mean a reboot restarts the clock at zero. If you are
exporting the CSV into something that wants wall-clock time, you will need to
offset it yourself.

---

## Security

### What actually protects the device?

Login with a salted SHA-256 password hash and constant-time comparison,
HttpOnly and SameSite=Strict session cookies with a one hour expiry, CSRF tokens
on every mutating request, login rate limiting with lockout, verified OTA with
automatic rollback, and standard hardening headers. The table in the
[README](../README.md#security-is-not-an-afterthought-here) is the quick version;
[API.md](API.md#authentication-model) and
[ARCHITECTURE.md](ARCHITECTURE.md#9-security-architecture) are the detailed ones.

### The default password is in the README. Is that not a problem?

It is a problem exactly until you change it, which is why the first-run
instructions and the dashboard both push you to do it immediately. A shipped
default that is documented is better than a shipped default that is secret and
guessable. Change it under Settings before you do anything else.

### What happens if I forget the admin password?

You reflash or factory reset. There is deliberately no recovery backdoor. The
procedure is in
[TROUBLESHOOTING.md](TROUBLESHOOTING.md#i-forgot-the-admin-password--device-ip).

### Is the traffic encrypted?

The WiFi link is, by your router's WPA2. The HTTP session is not: the dashboard
is plain HTTP. On a home LAN that is a reasonable trade for fitting everything on
an ESP32, and it is the reason for the "do not expose this to the internet"
advice. If you need TLS, terminate it on a reverse proxy in front of the device.

### How many login attempts before it locks me out?

Five failures within a 60 second window triggers a two minute lockout for that
client address. The constants are `NEXUS_LOGIN_MAX_ATTEMPTS`,
`NEXUS_LOGIN_WINDOW_S`, and `NEXUS_LOGIN_LOCKOUT_S`. A locked-out client gets an
HTTP 429 with a message saying so, rather than a generic wrong-password error.

### I found a vulnerability. Where do I send it?

[SECURITY.md](../SECURITY.md), not the public issue tracker. Security research is
welcome and credited.

---

## Accuracy and tuning

### It says the room is occupied when it is empty. What now?

That is the most common single complaint, and it is nearly always one of three
things: something is moving in the room that you have stopped noticing, the
sensor was calibrated while somebody was in the room, or the presence threshold
is below your room's natural noise floor.

In order: re-run calibration with the room genuinely empty, look for fans and
draughts, then raise the presence threshold. One change at a time, walk test in
between. Full procedure in [CALIBRATION.md](CALIBRATION.md).

### How long does calibration take?

A couple of minutes of the room being left alone, plus a walk test. It is not a
once-forever operation: rearranging furniture, moving the router, or moving the
sensor all change the radio geometry enough to justify redoing it.

### How fast does it react?

At the default 20 Hz sampling with a 64 sample window, presence changes settle in
the region of a couple of seconds, because a rising edge needs three consecutive
confirming windows and a falling edge needs eight. The asymmetry is on purpose:
quick to notice you, slow to forget you. Raise the sampling rate for faster
statistics, at some CPU and airtime cost.

### Why does it not clear immediately when I leave?

The falling-edge debounce, above. If it cleared the instant the score dipped, a
person who briefly stopped moving would flip the state to empty and back
constantly. Eight confirming windows is the damping that stops that.

### Can I get the raw CSI data out for my own analysis?

Not as full per-subcarrier CSI over the API, no. The device exposes the derived
features and state over REST and the WebSocket, documented in
[API.md](API.md). If you want the raw arrays, the place to tap in is the CSI
callback in `firmware/main/csi.c`, where the per-packet subcarrier data is still
intact before it is reduced to features.

---

## Building, flashing, and updating

### What do I need installed?

ESP-IDF v5.2 or newer, with the toolchain for the `esp32` target. CI builds
against v5.2.1. Setup is in [INSTALLATION.md](INSTALLATION.md) and the flashing
walkthrough is in [FLASHING.md](FLASHING.md).

### Do I need to build from source, or is there a binary?

Both paths are documented in [FLASHING.md](FLASHING.md). Building from source is
the better option if you plan to change any of the compile-time constants in
`app_config.h`, which most people end up doing.

### I changed something in `web/` and the dashboard looks the same. Why?

The web assets are embedded into the firmware binary at build time via
`EMBED_FILES`, so a change to `web/` needs a rebuild and reflash, not just a
browser refresh. There is no filesystem to drop files into. Hard-refresh the
browser too, since it caches aggressively.

### Why is there a `storage` partition if there is no filesystem?

Honest answer: the partition table reserves 768 KB as SPIFFS for future use, and
the current firmware does not mount it. Configuration and logs live in NVS, and
the web assets are compiled into the app. It is headroom, not something in use
today.

### How big can a firmware image be?

Each OTA slot is 0x180000 bytes, which is 1.5 MB. An upload larger than the
target partition is now rejected before anything is erased, so a too-large image
gets an HTTP 413 and leaves your working firmware untouched.

### What happens if an OTA update goes wrong?

The image is validated before the boot partition is switched, and
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` means a new image that fails to confirm
itself healthy is rolled back automatically on the next boot. A failed upload
leaves the running firmware alone. This is the mechanism that makes remote
updates something other than a gamble.

### Why did my device reboot on its own?

Most likely the task watchdog doing its job. Any subscribed task that stalls for
more than 10 seconds panics the system, and the panic handler reboots rather than
halting. That is deliberate: an unattended sensor should recover by itself rather
than sit there wedged. If it is rebooting repeatedly, the serial log will say
which task starved, and
[TROUBLESHOOTING.md](TROUBLESHOOTING.md#device-reboots-periodically) covers the
usual causes.

---

## How it compares

### Why not just use a PIR sensor? They cost a dollar.

PIR is excellent at detecting motion crossing its field of view and poor at
detecting a person who is present but still, which is why office lights turn off
while you are sitting at your desk. CSI sensing covers a whole room rather than a
cone, works through furniture and some walls, and holds presence for a stationary
person. PIR is cheaper, simpler, and lower power. Pick per problem; they are not
the same tool.

### Why not mmWave radar?

Radar is better than this at fine-grained sensing, including breathing rate and
rough positioning, and a good 60 GHz module is a genuinely superior presence
sensor. It also costs meaningfully more, needs careful mounting, and is another
component to source. This project's pitch is that you get room-scale presence out
of hardware you may already own, with no additional sensor at all.

### How does this compare to commercial WiFi sensing?

The commercial products generally have more antennas, access to more of the radio
stack, and years of tuning behind their models. What you get here is something you
can read end to end in an afternoon, run entirely on your own network, and change.
That is a different set of virtues, and for a lot of home projects it is the more
useful one.

---

## The project

### Is this actively maintained?

See [releases/CHANGELOG.md](../releases/CHANGELOG.md) for what has landed and the
[roadmap](../README.md#roadmap) for what is planned.

### Can I use this commercially?

Yes. It is MIT licensed, so commercial use, modification, and redistribution are
all fine, provided you keep the copyright and license notice. See
[LICENSE](../LICENSE). The MIT license also means there is no warranty: do not
build a life-safety system on this without doing your own validation.

### I want to help. Where do I start?

[CONTRIBUTING.md](../CONTRIBUTING.md) covers the workflow, the house style, and
the pull request checklist. If you want a concrete starting point,
[CODE_REVIEW.md](CODE_REVIEW.md) is a list of known issues with severities
attached, and several of them are small and self-contained.

### My question is not here.

Open a [discussion](https://github.com/nexus-sensing/NEXUS-CSI-Sensor/discussions)
rather than an issue. Questions there stay findable for the next person with the
same one, and good ones end up in this file.
