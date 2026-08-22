# Usage Guide

Your firmware is flashed and the board is powered. This guide takes you from that first power up through daily use: connecting to WiFi, logging in, touring the dashboard, calibrating, tuning detection, reading the logs, updating firmware, and resetting if you ever need to.

If you have not flashed yet, start with the [Flashing guide](FLASHING.md).

---

## First run: connect it to your WiFi

The very first time the device boots, it has no idea what network to join, so it creates its own and waits for you.

1. On your phone or laptop, open WiFi settings. You will see a network called **`NEXUS-CSI-Setup`**. Join it. The password is `nexus1234`.
2. Your device may warn that this network has no internet. That is expected. Stay connected to it.
3. Open a browser and go to **`http://192.168.4.1`**.
4. A setup page appears. Enter your real WiFi network name and password, then save.
5. The device reboots, joins your network, and the `NEXUS-CSI-Setup` network vanishes.

Now you need the device's new address on your network. You have three ways to find it:

- Read it off the device screen, if you attached one.
- Look at the serial log for a line containing `got ip`, if you still have a monitor open.
- Check your router's list of connected devices for the device name.

From here on, you reach the dashboard at `http://THAT-IP`.

> **A note on the setup password.** The `nexus1234` setup network password and the setup mode itself only exist while the device has no saved WiFi credentials. Once it joins your network, that open door closes. It only reappears if you do a factory reset.

---

## Logging in, and the first thing you must change

Open the device IP in your browser. You are greeted by a login screen, because the dashboard is protected by design.

The default credentials are:

- Username: `admin`
- Password: `nexus-admin`

**Log in, then change this immediately.** This is the single most important thing you will do. Go to **Settings**, set a new username and a strong password, and save. Until you do, anyone on your network who finds the device can read your presence data and change its settings.

A few things the login system is quietly doing for you:

- Your password is never stored as text. It is salted and hashed with SHA-256, so it cannot be read back out of the device.
- Your session lives in a secure cookie that JavaScript cannot touch, and it expires after an hour of use.
- If someone tries to guess the password, they get locked out for a couple of minutes after a handful of wrong attempts.

If you ever forget your password, there is no secret backdoor, that is the point. Your only recovery is a factory reset over USB, described at the end of this guide.

---

## The dashboard, room by room

The sidebar on the left is your map. Here is what each view is for.

### Dashboard (the live view)

This is the heart of it. At the top, a large glowing ring shows the current state and changes color as activity rises:

| State | What it means |
|-------|---------------|
| **Idle** | The space looks empty and still. |
| **Presence** | Someone or something is there, even if barely moving. |
| **Motion** | Clear movement is happening. |
| **High Motion** | Vigorous or sustained movement. |

Around and below the ring, live cards track the underlying numbers: the motion score, packets per second feeding the sensor, WiFi signal strength, the CSI variance (the raw "how much is the channel moving" figure), and the learned baseline. Small charts keep a rolling history so you can watch trends, not just the current instant.

Everything here updates in real time. If the live connection ever drops, the page automatically falls back to refreshing every few seconds, so the numbers are never stale for long.

### Logs

A running list of events, newest first: every state change, each calibration, reboots, and any warnings or errors. Each entry has a timestamp (measured from boot), a level, and a message. You can read them on screen, and there is a button to download the whole log as a CSV file for spreadsheets or record keeping.

### Settings

Where you configure everything. See the next section for the full list.

### System and OTA

Device health at a glance (uptime, free memory, which firmware slot is running, active sessions) and the firmware update page. More on updating below.

---

## Teaching it your room: calibration

Because every room reflects WiFi differently, the device learns what "empty and still" looks like in your specific space. Getting this right is the difference between reliable detection and constant false alarms.

The quick version:

1. Put the device in its final spot first (calibration is tied to placement).
2. Leave the monitored area so it is genuinely empty and still.
3. From a phone outside the area, go to **Settings** and use **Recalibrate now**.
4. Wait about 30 to 60 seconds while it settles. The CSI variance should drop to a small steady value and the state should read Idle.
5. Walk back in and move around. You should watch the state climb through Presence and Motion.

That is enough for most people. The full theory, the placement rules that matter most, and a threshold tuning table are in the dedicated [Calibration guide](CALIBRATION.md). It is worth reading once.

---

## Settings you can change

All of these live on the Settings page and save to the device, surviving reboots.

| Setting | What it controls |
|---------|------------------|
| **Device name** | The friendly name shown on the dashboard and screen. |
| **WiFi network and password** | Which network the device joins. Changing it reconnects live, no reboot. |
| **Presence threshold** | How much signal disturbance counts as "someone is here." |
| **Motion threshold** | How much counts as clear movement. |
| **High motion threshold** | How much counts as vigorous movement. |
| **Sampling rate** | How many measurements per second (5 to 100 Hz). Higher is faster and smoother but uses a little more CPU and bandwidth. |
| **Auto calibration** | Let the baseline slowly track environmental drift on its own. Best left on. |
| **Display brightness** | The backlight level for the optional screen. |
| **Admin username and password** | Your dashboard login. Change the default. |

Two rules of thumb for the thresholds:

- If you get **false alarms in an empty room**, raise the presence threshold.
- If it **misses people or reacts slowly**, lower the thresholds or raise the sampling rate.

Change one thing at a time and re test with a walk through. The [Calibration guide](CALIBRATION.md#4-tuning-the-thresholds) has a full symptom to fix table.

---

## Updating the firmware over the air

After the first flash, you never need the USB cable again. Updates happen through the browser.

1. Get a new `nexus_csi_sensor.bin`, either by building it or downloading a release.
2. Open the dashboard and go to **System and OTA**.
3. Choose the `.bin` file and start the upload. A progress bar tracks it.
4. The device verifies the image, then reboots into the new firmware.

This is safe by design. The new image is written to a spare slot, not over the running one, and it is checked before it is trusted. If a new build fails to start up properly, the bootloader automatically rolls back to the previous working version on the next boot. A bad update cannot brick the device.

Upload only the application image (`nexus_csi_sensor.bin`), not the bootloader or a merged binary. Full technical detail is in the [API reference](API.md#over-the-air-update).

---

## Factory reset

If you want to wipe everything and start fresh, or you are locked out and need to recover, a factory reset erases all saved settings and the event log and returns the device to its first boot state, including the default login and the setup network.

You have two ways to do it:

- **From the dashboard**, if you can still log in: go to **Settings** and use the factory reset action.
- **Over USB**, if you are locked out: erase the flash and reflash the firmware. With ESP-IDF installed, that is:

```bash
idf.py -p PORT erase-flash
```

Then flash again as in the [Flashing guide](FLASHING.md). After a reset the defaults return: login `admin` / `nexus-admin`, and the `NEXUS-CSI-Setup` network for WiFi setup.

---

## Where to go next

- Ideas for what to actually do with it: [Use cases](USE_CASES.md)
- Dialing in detection quality: [Calibration guide](CALIBRATION.md)
- Something not behaving: [Troubleshooting](TROUBLESHOOTING.md)
- Building on the firmware yourself: [Developer guide](DEVELOPER.md)
