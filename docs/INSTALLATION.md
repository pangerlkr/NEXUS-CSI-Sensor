# Installation Guide

This guide takes you from a bare ESP32 to a running NEXUS CSI Sensor on your
network. Two paths are covered:

- **[A. Build from source](#a-build-from-source)** - the normal developer path.
- **[B. Flash a prebuilt binary](#b-flash-a-prebuilt-binary)** - no toolchain,
  just a flashing utility.

---

## Prerequisites

### Hardware

- An **ESP32-WROOM-32** (or compatible ESP32) board with **≥ 4 MB flash**.
- A USB cable and a 5 V supply capable of ≥ 500 mA.
- A 2.4 GHz Wi-Fi network (the ESP32 radio is 2.4 GHz only).
- *(Optional)* a 1.8″ ST7735 128×160 SPI TFT - see [hardware/README.md](../hardware/README.md).

### Software

| Path | Requirement |
|------|-------------|
| Build from source | **ESP-IDF v5.1 or v5.2** and its toolchain |
| Prebuilt binary   | `esptool.py` **or** the [ESP Web Flasher](https://espressif.github.io/esptool-js/) in a browser |

> The firmware is developed and tested against ESP-IDF **v5.1.x** and
> **v5.2.x**. Other 5.x releases will likely work but are not verified.

---

## A. Build from source

### 1. Install ESP-IDF

Follow Espressif's official
[Get Started guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/)
for your OS. In short (Linux/macOS):

```bash
mkdir -p ~/esp && cd ~/esp
git clone -b v5.2.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32
```

Then, **in every new terminal** where you build, export the environment:

```bash
. ~/esp/esp-idf/export.sh
```

### 2. Get the source

```bash
git clone https://github.com/nexus-sensing/NEXUS-CSI-Sensor.git
cd NEXUS-CSI-Sensor/firmware
```

### 3. Select the target and build

```bash
idf.py set-target esp32
idf.py build
```

The first build downloads managed components and compiles everything; it can
take a few minutes. A successful build ends with the binary size report and
writes `build/nexus_csi_sensor.bin`.

> The web assets in [`../web`](../web) are **embedded into the binary** at build
> time (see [`main/CMakeLists.txt`](../firmware/main/CMakeLists.txt)); there is
> no separate filesystem image to flash.

### 4. Flash and monitor

Find your serial port first:

- Linux: usually `/dev/ttyUSB0` or `/dev/ttyACM0`
- macOS: `/dev/cu.usbserial-*` or `/dev/cu.SLAB_USBtoUART`
- Windows: `COMx`

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Exit the monitor with `Ctrl-]`. If flashing fails, hold the **BOOT** button
while it connects, or lower the baud rate: `idf.py -p PORT -b 115200 flash`.

Continue to [First-boot provisioning](#first-boot-provisioning).

---

## B. Flash a prebuilt binary

If you only want to run the sensor, grab the assets from the
[Releases](../releases/README.md) page: `bootloader.bin`,
`partition-table.bin` and `nexus_csi_sensor.bin`.

### Using `esptool.py`

The offsets come from [`partitions.csv`](../firmware/partitions.csv) and the
ESP32 boot layout:

```bash
esptool.py -p /dev/ttyUSB0 -b 460800 write_flash \
  0x1000  bootloader.bin \
  0x8000  partition-table.bin \
  0x20000 nexus_csi_sensor.bin
```

### Using the browser flasher

Open <https://espressif.github.io/esptool-js/>, connect to the board, add the
three files at the offsets above (`0x1000`, `0x8000`, `0x20000`) and program.

---

## First-boot provisioning

With no stored Wi-Fi credentials, the device starts a **setup access point**
so the dashboard is immediately reachable.

1. On a phone or laptop, join the Wi-Fi network:
   - **SSID:** `NEXUS-CSI-Setup`
   - **Password:** `nexus1234`
2. Open a browser to **`http://192.168.4.1`**.
3. Log in:
   - **Username:** `admin`
   - **Password:** `nexus-admin`
4. Go to **Settings** and:
   - enter your home Wi-Fi **SSID** and **password**,
   - **change the admin password** (do this now - the default is public),
   - optionally set a device name and display brightness,
   - click **Save settings**.
5. The device reboots and joins your network. Its new IP address is shown on
   the serial monitor (`got ip:`) and on the TFT if attached.
6. Reconnect your phone/laptop to your normal Wi-Fi and browse to the device's
   new IP.

> [!TIP]
> If the device can't join your network (wrong password, out of range) it
> automatically falls back to the `NEXUS-CSI-Setup` AP again so you can retry.

After provisioning, run through the [Calibration guide](CALIBRATION.md) once for
reliable detection.

---

## Updating firmware later (OTA)

Once the device is on your network you don't need the USB cable again:

1. Build (or download) a new `nexus_csi_sensor.bin`.
2. Open the dashboard → **System & OTA**.
3. Choose the `.bin` and click **Upload & flash**. Watch the progress bar.
4. The device verifies the image, reboots into it, and - thanks to bootloader
   rollback - reverts automatically if the new image fails to come up.

Details and the manual `esptool` alternative are in the
[API reference](API.md#post-apiota) and
[§B above](#b-flash-a-prebuilt-binary).

---

## Uninstall / factory reset

- **Settings → Factory reset** erases all stored configuration and the event
  log, then reboots into the setup AP.
- To wipe the chip completely: `idf.py -p PORT erase-flash`.
