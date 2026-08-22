# Components You Need

The honest truth about this project is that the shopping list is tiny. The clever part is all in the software. You are not buying a special sensor, because the sensor is the WiFi radio already baked into the ESP32.

This page explains every part, why it is on the list, what to avoid, and how to wire the one optional extra.

---

## The whole list at a glance

| Part | Required? | Rough cost | Why you need it |
|------|-----------|------------|-----------------|
| ESP32-WROOM-32 board | Yes | Low | The brain and the sensor in one. |
| USB data cable | Yes | You own one | Flashing and power. |
| 2.4 GHz WiFi network | Yes | You have one | The signal that gets measured. |
| Stable 5V USB power source | Yes | You own one | Clean power for reliable readings. |
| ST7735 1.8 inch TFT screen | Optional | Low | An on device status display. |
| Female to female jumper wires | Optional | Very low | To connect the screen. |

If you only buy one thing, buy the ESP32 board. Everything else you very likely already have.

---

## 1. The ESP32 board (required)

This is the star of the show. It is a small, cheap, dual core chip with WiFi and Bluetooth built in, and crucially its WiFi radio can expose Channel State Information, the raw signal data this whole project runs on.

**What to buy**

Get a development board based on the **ESP32-WROOM-32** module. These are the classic, ubiquitous ESP32 dev boards, often labeled "ESP32 DevKit," "NodeMCU-32S," "ESP32 DevKitC," or similar. They have a USB port on board, so you can flash and power them with a single cable.

Two things to confirm before you buy:

- **It is a genuine ESP32, not an ESP8266.** This matters a lot. The older ESP8266 looks similar and is cheaper, but it does not provide CSI the way this firmware needs. It will not work. If the listing says ESP8266, keep scrolling.
- **It has at least 4 MB of flash.** Almost every WROOM-32 board does. The firmware uses a layout with two 1.5 MB slots for safe over the air updates, plus space for settings, so it needs the room.

**What also works**

Other classic ESP32 boards (different module variants, boards with more flash or extra RAM) are generally fine, as long as the underlying chip is the original ESP32 and the target is `esp32`. Newer chips in the family, like the S2, S3, C3, and so on, use different CSI support and pinouts and are not what this build targets.

---

## 2. A USB data cable (required)

You need a cable that carries **data**, not only power. This sounds obvious, but it is the number one reason a first flash fails, because a lot of bundled charging cables have the data lines missing entirely.

If your computer does not detect the board at all when you plug it in, swap the cable before you troubleshoot anything else. A cable that has ever successfully synced a phone to a computer is a safe bet.

The connector on the board is usually Micro USB, sometimes USB-C on newer boards. Match your cable to whatever the board has.

---

## 3. A 2.4 GHz WiFi network (required)

The sensor works by measuring your WiFi signal, so it needs one to join. Two points that save a lot of confusion:

- **It is 2.4 GHz only.** This is a hardware limit of the ESP32, not a choice in the firmware. If your router only broadcasts 5 GHz, or hides the 2.4 GHz band, the device cannot see it. Most home routers broadcast both; you may just need to make sure the 2.4 GHz band is enabled.
- **A phone hotspot works too.** For testing away from home, a 2.4 GHz phone hotspot is perfectly fine. The device does not care whether the network is a router or a hotspot.

You do not need internet access for the core sensing and dashboard to work. Everything runs locally on the device. Internet is only used to load the charting library from a CDN, and even that has a built in offline fallback.

---

## 4. Stable 5V USB power (required)

Once flashed, the device just needs power. Any decent 5V USB source works: a phone charger, a powered USB hub, a computer port, or a USB battery pack for portable use.

The one thing that matters is that the power is **stable**. A weak or flaky supply causes brownouts, brief dips in voltage that reset the board. Brownouts do two bad things: they reboot your sensor, and they corrupt the very WiFi measurements the whole system depends on. A short, good quality cable and a supply rated for at least 500 mA keeps things clean.

---

## 5. The optional display (ST7735 1.8 inch TFT)

Everything works without a screen. The display driver only ever writes to the SPI bus, so it cannot actually tell whether a panel is attached; with nothing wired up those writes go nowhere and the device carries on happily while you use the web dashboard instead. If you would rather not build the driver at all, set `NEXUS_ENABLE_DISPLAY` to 0 in [`app_config.h`](../firmware/main/app_config.h).

But a little screen is genuinely nice. It shows, at a glance and with no phone in hand:

- The device name and its IP address on your network
- The current presence state
- The motion level
- Signal quality
- Packets per second
- Overall system status

**What to buy**

A **1.8 inch SPI TFT module using the ST7735 driver, 128 by 160 resolution**. These are common and inexpensive. They usually have eight pins along one edge.

> **One safety note worth reading.** Most of these modules run on **3.3V logic**. Power the module's `VCC` from the ESP32's **3V3** pin, not the 5V pin, unless the module's own documentation clearly says it has a regulator and level shifting on board. Feeding 3.3V logic with 5V can damage the panel.

### Wiring the display

Eight wires, and that is the whole job. These pin choices match the firmware exactly. If you wire to different pins, you would change them in [firmware/main/app_config.h](../firmware/main/app_config.h) and rebuild.

| Screen pin (common labels) | Connect to ESP32 pin | Purpose |
|----------------------------|----------------------|---------|
| `VCC` | `3V3` | Power (3.3V, see the note above) |
| `GND` | `GND` | Ground |
| `SCL` or `SCK` | `GPIO18` | SPI clock |
| `SDA` or `MOSI` | `GPIO23` | SPI data |
| `CS` | `GPIO5` | Chip select |
| `DC`, `A0`, or `RS` | `GPIO2` | Data or command select |
| `RES` or `RST` | `GPIO4` | Reset |
| `BLK` or `LED` | `GPIO15` | Backlight (brightness is adjustable in Settings) |

```
   ESP32-WROOM-32                     ST7735 1.8" TFT
  +--------------+                   +--------------+
  |          3V3 +-------------------+ VCC          |
  |          GND +-------------------+ GND          |
  |   GPIO18 SCLK+-------------------+ SCL / SCK    |
  |   GPIO23 MOSI+-------------------+ SDA / MOSI   |
  |     GPIO5 CS +-------------------+ CS           |
  |     GPIO2 DC +-------------------+ DC / A0      |
  |    GPIO4 RST +-------------------+ RES          |
  |   GPIO15 BLK +-------------------+ BLK / LED    |
  +--------------+                   +--------------+
```

> **If the screen shows garbage or wrong colors:** ST7735 modules come in a few variants, identified by the color of the little plastic tab on the panel (red, green, or black). They use slightly different startup settings. If your image is color inverted, shifted, or has a border, that is the variant, and you adjust the init sequence in [firmware/main/display.c](../firmware/main/display.c). More on this in [Troubleshooting](TROUBLESHOOTING.md#display-st7735).

A couple of the display pins (GPIO2 and GPIO15) are also "strapping" pins the chip reads at boot. If you ever have trouble flashing **with the screen connected**, just unplug the `DC` and `BLK` wires during flashing, then reconnect them.

---

## What you deliberately do not need

It is worth saying plainly, because it is the whole point of the project. You do **not** need:

- A camera
- A microphone
- A PIR or infrared motion sensor
- An ultrasonic or radar module
- Any add on sensor at all

All of the sensing comes from the WiFi radio inside the ESP32. That is what makes this private by nature: there is no image, no audio, and no dedicated occupancy sensor to capture anything in the first place.

---

## A sensible bench setup

For your first run, before you mount it anywhere, put the board on a desk a few meters from your WiFi router with clear space between the two. Power it from a reliable source, keep it still, and open the dashboard on your phone from across the room.

Placement genuinely affects how well it detects, more than any setting does, so once you are happy it works, read [docs/CALIBRATION.md](CALIBRATION.md#2-placement) before choosing a permanent home for it.

Ready to put firmware on it? Head to the [Flashing guide](FLASHING.md).
