# Use Cases

The fun of a WiFi presence sensor is that it slots into places a camera never could, either because a camera would be creepy, or overkill, or simply not allowed. Below are real scenarios people build with NEXUS, what makes each one work, and how to set it up well.

At the end there is a section on using this responsibly. Please read it. A sensor that watches a space is a responsibility, even a camera free one.

---

## Presence based home automation

**The idea.** Lights, heating, and fans that respond to whether a room is genuinely occupied, not to a timer and not to a motion sensor that forgets you the moment you sit still.

This is where WiFi sensing quietly beats a classic PIR motion sensor. A PIR only sees heat that moves, so it famously switches your lights off while you are sitting reading, because you stopped waving your arms around. NEXUS reports **Presence** even when you are still, and drops to **Idle** only when the room is actually empty. Lights that stay on while you read and turn off when you leave, with nothing pointed at you.

**How to set it up.** Place the device so the room's main area sits between it and your router. Calibrate with the room empty. Then have your automation platform watch the presence state. Direct integration with home platforms is on the roadmap; today you would read the state from the device's local API. See [API reference](API.md#get-apistatus).

---

## Gentle wellness and independent living

**The idea.** Peace of mind that an older relative living alone is up and moving through their normal day, without a camera in their home and without asking them to wear or charge anything.

A camera in a parent's living room is a hard sell, and understandably so. A small box on a shelf that never sees them, only notices that the room has its usual rhythm of activity, is a very different conversation. You can watch for the reassuring pattern of normal movement, or for its absence at times you would expect it.

**How to set it up.** One device per key room (living room, kitchen). Calibrate carefully, since this is a case where false alarms and misses both matter. Lean on the activity level, which reflects sustained movement over time, rather than instantaneous motion. Be honest and open with the person being monitored; this only works as a tool of care, with their knowledge and agreement.

> This is not a medical device and must not be relied on for emergencies or fall detection. Treat it as a gentle awareness aid, never as a safety system someone's wellbeing depends on.

---

## Security and intrusion awareness

**The idea.** Know when there is movement in a space that should be empty: a back room, a garage, a workshop, a holiday home.

Because detection does not need line of sight the way a camera does, and because the device is small and unremarkable, it makes a discreet motion tripwire for spaces you want to keep an eye on. Pair the event log with an automation that alerts you when the state jumps to Motion during hours the space should be still.

**How to set it up.** Position it to cover the entry path into the space. Set the thresholds a little higher so only real movement trips it, reducing false alarms from, say, a curtain near a vent. Use the CSV log export to keep a record of activity. This is an awareness tool that complements a real alarm system, not a replacement for one.

> Use this only on property you own or are explicitly authorized to monitor. Covertly surveilling a space, even without a camera, can be illegal and is never okay. More on this below.

---

## Occupancy for energy saving

**The idea.** Stop heating, cooling, and lighting rooms that nobody is in.

Empty conference rooms, spare bedrooms, and storage areas quietly burn energy all day. A presence signal lets you gate climate control and lighting on real occupancy. Because NEXUS distinguishes an empty room from an occupied but quiet one, you avoid the classic failure where the heating cuts out on a room full of people who happen to be sitting still.

**How to set it up.** One device per zone you want to control. Favor the presence state for occupancy decisions and add a sensible delay before you cut power to a room, so a brief stillness does not flip everything off. Auto calibration on, so seasonal changes in the room do not require you to fiddle.

---

## Workspace and desk presence

**The idea.** A privacy respecting "at my desk" signal for status, focus timers, or lighting, without a webcam watching you work.

Set a busy indicator when you are present at your desk, drive focus or break reminders off your real activity, or just log your own patterns. Since it runs entirely locally and captures no image or audio, it is a status source you can actually feel comfortable having in a home office.

**How to set it up.** Place the device on or near the desk area, calibrate while away from it, and lower the presence threshold a touch since the movements of working are subtle. Everything stays on your machine and your LAN.

---

## Learning, research, and tinkering

**The idea.** WiFi sensing is a genuinely fascinating corner of signal processing, and this is a complete, readable, affordable way into it on real hardware.

The whole pipeline is open and documented: how raw CSI becomes a clean feature, how the baseline is learned and frozen, how the score is normalized, and how the state machine debounces it all. It is a great platform for a class project, a thesis experiment, or just satisfying your own curiosity. You can log raw features, tweak the maths, and watch the effect live.

**How to set it up.** Read [docs/ARCHITECTURE.md](ARCHITECTURE.md#5-the-signal-processing-pipeline) for the pipeline, then [docs/DEVELOPER.md](DEVELOPER.md) to build and experiment. Every tunable lives in one header, [firmware/main/app_config.h](../firmware/main/app_config.h), so you can change one thing and rebuild.

---

## On the horizon: multi room and smart home

**The idea.** Several devices covering a whole home, their readings fused together, feeding a smart home platform directly.

The firmware is deliberately structured so this can be added without tearing up the core. The roadmap includes MQTT and Home Assistant integration, and multi node coverage using ESP-NOW so devices can talk to each other directly. If you want to build toward this, the extension points are documented in [docs/ARCHITECTURE.md](ARCHITECTURE.md#11-future-ready-architecture).

---

## Using this responsibly

A camera free sensor feels harmless, and in terms of the data it captures it genuinely is more private than a camera, since there is no image or audio anywhere. But it still detects when people are present and moving, and that is information about real people. Please hold yourself to a simple standard.

**Only monitor spaces you own or are clearly permitted to monitor.** Your own home, your own office, a space where you have explicit authorization. Not someone else's home, not a shared space where others have not agreed, not anywhere you would be uncomfortable if the roles were reversed.

**Tell the people who share the space.** If others live, work, or spend time in a monitored area, they deserve to know it is there and what it does. "It is not a camera" is not a substitute for their consent.

**Know your local laws.** Rules on monitoring and surveillance vary widely by country and region, and they can apply even to sensors that never record images or sound. When in doubt, check, or ask someone who knows.

**Keep it secure.** Change the default password immediately, keep the device on a network you trust, and do not expose its dashboard directly to the public internet. The security features described in the [README](../README.md#security-is-not-an-afterthought-here) protect the people whose space it senses; use them.

**Do not use it to harm, stalk, or surveil.** This should not need saying, but building a tool means thinking about how it could be misused. Detecting presence without consent to track, intimidate, or control someone is an abuse of it, full stop.

Build thoughtful things. The best projects respect the people they touch.
