# Troubleshooting

Symptoms are grouped by phase: build → flash → connect → detect → runtime.
Each entry lists the likely cause and the fix. When in doubt, watch the serial
log - `idf.py -p PORT monitor` - it prints a meaningful message for every
failure path in the firmware.

---

## Build

### `idf.py: command not found` / wrong versions

You haven't exported the ESP-IDF environment in this shell.

```bash
. $IDF_PATH/export.sh      # e.g. . ~/esp/esp-idf/export.sh
```

Confirm the version is 5.1.x or 5.2.x: `idf.py --version`.

### Build fails complaining about the target

Set the target once per checkout before the first build:

```bash
idf.py set-target esp32
```

### `CONFIG_ESP_WIFI_CSI_ENABLED` / CSI symbols missing

CSI must be enabled in sdkconfig. It is set in
[`sdkconfig.defaults`](../firmware/sdkconfig.defaults), but if you have a stale
`sdkconfig`, reset it:

```bash
rm firmware/sdkconfig && idf.py reconfigure
```

### Changes to `web/` don't appear

Web assets are embedded at build time. Rebuild after editing them; if the
embed seems cached, `idf.py fullclean && idf.py build`.

---

## Flash

### `Failed to connect to ESP32: No serial data received`

- Hold the **BOOT** button while flashing starts (some boards need it).
- Try a lower baud rate: `idf.py -p PORT -b 115200 flash`.
- Check the cable is a **data** cable, not charge-only.
- Verify the port: Linux `ls /dev/ttyUSB* /dev/ttyACM*`, macOS
  `ls /dev/cu.*`, Windows Device Manager → COM ports.

### Permission denied on `/dev/ttyUSB0` (Linux)

Add yourself to the `dialout` group and re-login:

```bash
sudo usermod -aG dialout $USER
```

### Boots but immediately crashes / bootloops after manual flash

When flashing prebuilt binaries, the **offsets must match** the partition
layout: bootloader `0x1000`, partition table `0x8000`, app `0x20000`. A wrong
offset bootloops. See [Installation §B](INSTALLATION.md#b-flash-a-prebuilt-binary).

---

## Connect / provisioning

### Can't see the `NEXUS-CSI-Setup` network

- It only appears when **no** valid Wi-Fi credentials are stored (first boot or
  after a factory reset). If the device already joined a network, it won't
  broadcast the AP.
- Give it ~10 s after power-up. Check the serial log for `AP start`.
- The AP is 2.4 GHz - some phones hide 2.4 GHz networks in crowded areas.

### Joined the AP but `192.168.4.1` won't load

- Disable mobile data so the phone routes to the AP, not cellular.
- Try `http://192.168.4.1` explicitly (not https, no autocomplete to a search).
- Only a few clients can associate (`NEXUS_AP_MAX_CONN`); disconnect others.

### Device won't join my home Wi-Fi

- The ESP32 is **2.4 GHz only** - it cannot see a 5 GHz-only SSID. Enable the
  2.4 GHz band or a combined SSID.
- Double-check the password (re-enter it in Settings).
- Special characters in the SSID/password can cause issues; test with a simple
  one.
- On repeated failure the device falls back to the setup AP automatically - 
  rejoin it and retry. Watch the serial log for the disconnect reason code.

### I forgot the admin password / device IP

- IP: read it from the serial monitor (`got ip:`) or the TFT.
- Password: there is no remote recovery by design. Do a hardware factory
  reset by re-flashing, or `idf.py -p PORT erase-flash` then reflash; the
  defaults (`admin` / `nexus-admin`) return.

---

## Login / dashboard

### Login says "Invalid credentials" with the defaults

The default is `admin` / `nexus-admin` **only until you change it**. If you
changed it and forgot, see above. After 5 rapid failures you're locked out for
2 minutes (rate limiting) - wait and retry.

### Dashboard loads but shows "reconnecting…" and no data

- The WebSocket couldn't open or was dropped; the UI should fall back to
  polling within a few seconds. If nothing updates, reload the page.
- Behind a strict proxy/VPN, WebSocket upgrades may be blocked - access the
  device directly on the LAN.
- Check the serial log for web-server errors or repeated reboots.

### Charts are blank

- If offline, Chart.js (CDN) won't load and `app.js` uses its built-in
  renderer - charts still draw once data arrives. Give it a few seconds to
  accumulate points, or open the **History** seeds by revisiting the Dashboard.

---

## Detection quality

### No CSI / "no CSI" on the card, packet rate 0

- CSI needs received packets. Confirm the device is **connected** (STA) - in
  soft-AP mode with no clients there is little traffic.
- The traffic generator pings the **gateway**; if the gateway blocks ICMP or
  the route is down, packets stall. Verify `wifi.gateway` in `/api/status`.
- Raise the **Sampling rate** in Settings.

### Constant false presence when the room is empty

- **Recalibrate** with the area empty and still (see
  [Calibration](CALIBRATION.md#3-calibration-procedure)).
- Raise the **Presence threshold**.
- Remove RF churn from the beam: fans, pets, moving curtains, a running
  microwave.
- Leave **auto-calibration** on to track slow drift.

### Misses people / slow to react

- Lower the **Presence/Motion thresholds**.
- Improve placement: the person should cross the **line of sight** between the
  ESP32 and the router. Re-read [Calibration §2](CALIBRATION.md#2-placement).
- Increase **Sampling rate** for faster statistics.

### Detection is twitchy (flips rapidly)

- Raise thresholds slightly. The built-in debounce and hysteresis damp most
  chatter; if you changed `NEXUS_DEBOUNCE_*` in a custom build, restore the
  defaults.

---

## Display (ST7735)

### Screen is blank / white / garbled

- Re-check wiring against [hardware/README.md](../hardware/README.md) - MOSI,
  SCLK, CS, DC, RST, BLK must match [`app_config.h`](../firmware/main/app_config.h).
- Some ST7735 boards use different init (red/green/black tabs); if colours are
  inverted or offset, that's the panel variant - adjust the driver init in
  `display.c`.
- Confirm the backlight pin (`BLK`, GPIO15) is driven and brightness isn't 0
  (Settings → Display brightness).
- If you don't have a display, that's fine - it degrades to a no-op and logs a
  warning; nothing else is affected.

---

## Runtime / stability

### Device reboots periodically

- Check the serial log for the reset reason. A **Task Watchdog** panic reboot
  is intentional self-recovery from a stall - note which task and file an issue.
- **Brown-out** resets mean insufficient power: use a better USB supply/cable
  (≥ 500 mA, short cable). Brown-outs also corrupt CSI.

### OTA update fails

- Upload the app image only (`nexus_csi_sensor.bin`), **not** the bootloader or
  a merged binary.
- A verification failure leaves the running firmware untouched - you're safe to
  retry.
- If a new image crash-loops, bootloader **rollback** reverts to the previous
  version automatically on the next boot.
- Ensure a stable connection during upload; a dropped connection aborts the
  session (retry).

### Free heap keeps shrinking

- Watch `heap_free` vs `heap_min` in **System**. A slow steady decline across
  days may indicate a leak - capture the values over time and open an issue with
  the serial log. See the soak-test guidance in
  [DEVELOPER.md](DEVELOPER.md#soak--stress-testing).

---

## Still stuck?

Collect the serial log (`idf.py monitor`), your ESP-IDF version
(`idf.py --version`), board model, and whether a display is attached, and open
an issue. For contributor-side debugging tips see the
[Developer guide](DEVELOPER.md).
