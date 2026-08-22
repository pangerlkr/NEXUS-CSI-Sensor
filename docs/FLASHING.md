# Flashing Guide

This guide gets the NEXUS firmware onto a physical ESP32 board. It assumes nothing. If you have never flashed a microcontroller before, follow it top to bottom and you will be fine.

There are two ways to do this, and you only need one of them:

- **Path A, from source.** You install the ESP-IDF toolchain, build the firmware yourself, and flash it. Pick this if you want to change the code, or if there is no prebuilt binary for your version. This is the path the project is designed around.
- **Path B, prebuilt binary.** You download a ready made `.bin` and write it to the board with a small flashing tool. Pick this if you just want it running and do not care to build anything.

Both paths finish at the same place: the device boots and starts hosting its setup network.

---

## Before you start: the checklist

You need all of these ready.

- [ ] An **ESP32-WROOM-32** board (or compatible classic ESP32 with 4 MB flash).
- [ ] A **USB data cable**. This trips people up constantly. Many cheap cables only carry power. If your computer never sees the board, suspect the cable first.
- [ ] A free **USB port** on your computer.
- [ ] For Path A only: a machine running macOS, Linux, or Windows with about 5 GB of free disk for the toolchain.

Plug the board into your computer with the USB cable now. A power LED on the board should light up.

---

## Step 1: Find the serial port

Your computer talks to the ESP32 over a virtual serial port. You need its name. This is the same for both paths.

**macOS**

```bash
ls /dev/cu.*
```

Look for something like `/dev/cu.usbserial-0001`, `/dev/cu.SLAB_USBtoUART`, or `/dev/cu.wchusbserial1420`. That is your port. If nothing WiFi looking shows up, you may be missing a USB to serial driver (see the note at the end of this step).

**Linux**

```bash
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

Usually `/dev/ttyUSB0`. On Linux you may also need permission to use the port:

```bash
sudo usermod -aG dialout $USER
```

Log out and back in after running that, so the group change takes effect.

**Windows**

Open Device Manager and expand **Ports (COM & LPT)**. The board appears as something like `Silicon Labs CP210x (COM5)` or `USB-SERIAL CH340 (COM5)`. Your port is `COM5` (or whatever number it shows).

> **No port appears at all?** Most ESP32 boards use a CP2102 or CH340 USB to serial chip. If your operating system did not install the driver automatically, grab it from Silicon Labs (CP210x) or WCH (CH340), install it, replug the board, and check again.

Write your port down. Everywhere below says `PORT`, put yours in its place.

---

## Path A: Build from source and flash

This installs the official toolchain once, then it is a three command routine forever after.

### A1. Install ESP-IDF

You only do this part one time. Full official instructions live at the Espressif site, but here is the short version for macOS and Linux.

```bash
mkdir -p ~/esp && cd ~/esp && git clone -b v5.2.1 --recursive https://github.com/espressif/esp-idf.git
```

That download is large and pulls in submodules, so give it a few minutes. Then install the tools for the ESP32 target:

```bash
cd ~/esp/esp-idf && ./install.sh esp32
```

On Windows, install ESP-IDF using the official Windows Installer instead, which sets up Python, Git, and the toolchain for you, then use the "ESP-IDF Command Prompt" it creates.

### A2. Activate the toolchain in your shell

Here is the single most common mistake, so read this twice. The `idf.py` command does not exist until you "activate" ESP-IDF in your current terminal. You must run this in every new terminal window where you want to build:

```bash
. ~/esp/esp-idf/export.sh
```

Note the leading dot and space. After it runs, this should print a version:

```bash
idf.py --version
```

If that prints something like `ESP-IDF v5.2.1`, you are ready. If it says command not found, the activation above did not run in this same terminal.

### A3. Build the firmware

Move into the firmware folder of this project and set the chip target once:

```bash
cd "path/to/NEXUS-CSI-Sensor/firmware" && idf.py set-target esp32
```

Then build:

```bash
idf.py build
```

The first build compiles the whole framework and takes a while, often several minutes. Later builds are much faster because only your changes recompile. When it finishes you will see a summary and the path to the output binaries. Success looks like a message telling you the project built and giving you a flashing command.

### A4. Flash and watch it boot

One command flashes the board and immediately opens the serial log so you can watch it start up:

```bash
idf.py -p PORT flash monitor
```

Replace `PORT` with the port from Step 1. This erases the old firmware, writes the new one at the correct addresses automatically, resets the board, and starts streaming its logs.

To leave the monitor, press `Ctrl` and `]` together.

Skip ahead to [Step 3: First boot](#step-3-first-boot).

---

## Path B: Flash a prebuilt binary

If you have three `.bin` files (a release bundle) rather than source, use this path. You do not need the full toolchain, only a small tool called `esptool`.

### B1. Install esptool

`esptool` is a Python program. If you have Python 3:

```bash
pip install esptool
```

### B2. Know your three files and their addresses

A NEXUS release contains three binaries, and each must be written to a specific address in flash. These addresses are not optional. If you write the app to the wrong offset, the board will boot loop.

| File | Flash address | What it is |
|------|---------------|------------|
| `bootloader.bin` | `0x1000` | The first code that runs on power up. |
| `partition-table.bin` | `0x8000` | The map of what lives where in flash. |
| `nexus_csi_sensor.bin` | `0x20000` | The actual application. |

### B3. Write all three at once

Put the three files in one folder, open a terminal there, and run this as a single command:

```bash
esptool.py -p PORT -b 460800 write_flash 0x1000 bootloader.bin 0x8000 partition-table.bin 0x20000 nexus_csi_sensor.bin
```

Replace `PORT` with your port. The `-b 460800` sets a fast baud rate; if the write fails partway, try again with `-b 115200` for a slower, more reliable transfer.

### B4. Watch it boot

`esptool` does not show logs, so open a serial monitor separately. Any serial terminal at **115200 baud** works. If you happen to have ESP-IDF installed you can use its monitor:

```bash
idf.py -p PORT monitor
```

Otherwise use a tool like `screen` on macOS or Linux:

```bash
screen PORT 115200
```

(To quit `screen`, press `Ctrl` and `a`, then `k`, then `y`.)

---

## Step 3: First boot

Whichever path you took, the serial log now tells the story. On a healthy first boot you will see, roughly in order:

- A startup banner with the firmware version.
- Storage, configuration, and logging coming up.
- The WiFi layer starting.
- Because there are no saved WiFi credentials yet, a line saying the setup access point has started, named `NEXUS-CSI-Setup`.
- The web server starting.
- A "system ready" style message.

That is a successful flash. The device is now waiting for you to tell it about your WiFi.

If instead you see the board resetting over and over, jump to [When flashing goes wrong](#when-flashing-goes-wrong) below.

---

## Step 4: Connect it to your WiFi

The device is now broadcasting its own network so you can configure it.

1. On your phone or laptop, open WiFi settings and join the network **`NEXUS-CSI-Setup`**. The password is `nexus1234`.
2. Open a browser and go to **`http://192.168.4.1`**.
3. Enter your home WiFi name and password, and save.
4. The device reboots, joins your network, and its setup network disappears.

To find its new address on your network, look at the serial log for a line containing `got ip`, or read it off the device screen if you attached one. From then on you open the dashboard at `http://THAT-IP`.

The default dashboard login is `admin` / `nexus-admin`. Change it immediately once you are in. The full first run walkthrough, including the dashboard tour, is in [docs/USAGE.md](USAGE.md).

---

## Verifying it actually worked

You are fully up and running when all of these are true:

- The serial log shows a clean boot with no repeating resets.
- The device either hosts `NEXUS-CSI-Setup` (before provisioning) or reports a `got ip` on your network (after).
- The dashboard loads at the device IP and asks you to log in.
- After logging in, the packets per second number is climbing and the motion score responds when you move near the device.

---

## Updating later, the easy way

Once the device is on your network you never need the USB cable again. Build or download a new `nexus_csi_sensor.bin`, open the dashboard, go to the **System and OTA** view, choose the file, and upload it. The device verifies the image, reboots into it, and rolls back automatically if the new build fails to start. Details are in [docs/USAGE.md](USAGE.md#updating-the-firmware-over-the-air).

---

## When flashing goes wrong

The greatest hits, and how to fix them.

**"Failed to connect" or "No serial data received."**
Hold the **BOOT** button on the board while flashing begins, and keep holding until you see it start writing. Some boards need this to enter download mode. If that does not help, try a slower baud rate (`-b 115200`), and double check the cable is a data cable.

**The port is not listed anywhere.**
It is almost always a missing USB to serial driver (CP210x or CH340) or a power only cable. See the driver note under Step 1.

**Permission denied on the port (Linux).**
Add yourself to the `dialout` group as shown in Step 1, then log out and back in.

**The board boots, then resets over and over (Path B).**
Your flash addresses were wrong. The three files must go to `0x1000`, `0x8000`, and `0x20000` exactly. Re-read B2 and reflash.

**It flashes fine but the dashboard never appears.**
Confirm you actually joined `NEXUS-CSI-Setup` (before setup) or that the device got an IP on your WiFi (after setup). Watch the serial log for the reason. The band matters too: the ESP32 is 2.4 GHz only and cannot see a 5 GHz only network.

**Something else, or you want deeper diagnostics.**
The [Troubleshooting guide](TROUBLESHOOTING.md) covers build, flash, connection, detection, and runtime problems in far more detail.

---

## A quick command reference

Once ESP-IDF is set up, these are the commands you will actually use, all run from the `firmware/` folder after activating the toolchain with `. ~/esp/esp-idf/export.sh`:

```bash
idf.py set-target esp32      # once per fresh checkout
idf.py build                 # compile
idf.py -p PORT flash         # write to the board
idf.py -p PORT monitor       # watch the logs (Ctrl-] to exit)
idf.py -p PORT flash monitor # do both in one go
idf.py fullclean             # wipe the build if something is stuck
```
