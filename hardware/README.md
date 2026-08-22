# Hardware Guide

Everything you need to build the sensor: the bill of materials, the ST7735
display wiring, and power/placement notes. Pin assignments here mirror
[`firmware/main/app_config.h`](../firmware/main/app_config.h) exactly - if you
wire to different pins, change them there and rebuild.

---

## Bill of materials

| # | Part | Notes | Required? |
|---|------|-------|-----------|
| 1 | **ESP32-WROOM-32** dev board | Any classic ESP32 (dual-core, 2.4 GHz Wi-Fi). CSI is an ESP32 feature - an ESP8266 will **not** work. | ✅ Required |
| 2 | USB cable (**data**, not charge-only) | For flashing and power. | ✅ Required |
| 3 | 2.4 GHz Wi-Fi router / hotspot | The sensor is a Wi-Fi client; CSI is measured on its link. | ✅ Required |
| 4 | 5 V USB power supply (≥ 500 mA) | A stable supply - brown-outs corrupt CSI. | ✅ Required |
| 5 | **1.8" SPI TFT, ST7735, 128×160** | On-device status screen. | ⭕ Optional |
| 6 | Jumper wires (female-female) | To wire the display. | ⭕ Optional |

Nothing else is needed - **no PIR, no camera, no ultrasonic, no radar**. All
sensing is done from the Wi-Fi radio already inside the ESP32.

---

## ESP32 requirements

- **Chip:** ESP32 (WROOM-32 or equivalent). The firmware sets
  `CONFIG_IDF_TARGET=esp32`.
- **Flash:** **4 MB** minimum - the [partition table](../firmware/partitions.csv)
  defines two 1.5 MB OTA app slots plus NVS and a storage partition. Boards with
  less flash won't fit the layout.
- **Band:** 2.4 GHz only (this is an ESP32 limitation, not the firmware's). A
  5 GHz-only network is invisible to it.

---

## Display wiring (optional)

The display is **entirely optional**. With nothing wired up the firmware runs
normally and you use the web dashboard instead. Worth knowing how that works: the
ST7735 driver is write-only, so it cannot tell whether a panel is actually
connected. It initialises the SPI bus and starts drawing either way, and those
writes simply go nowhere. Nothing stalls and nothing fails.

If you want to leave the driver out of the build entirely, set
`NEXUS_ENABLE_DISPLAY` to 0 in
[`app_config.h`](../firmware/main/app_config.h) and the display task is not
created at all.

> ⚠️ Most ST7735 breakout boards are **3.3 V** logic. Power `VCC` from the
> ESP32's **3V3** pin, not 5 V, unless your board explicitly has an onboard
> regulator and level shifting. Powering logic at 5 V can damage the panel.

### Pinout

| ST7735 pin | Connects to ESP32 | `app_config.h` constant | Notes |
|------------|-------------------|-------------------------|-------|
| `VCC` | **3V3** | - | 3.3 V (see warning above) |
| `GND` | `GND` | - | Common ground |
| `SCL` / `SCK` | **GPIO18** | `NEXUS_TFT_PIN_SCLK` | SPI clock |
| `SDA` / `MOSI` | **GPIO23** | `NEXUS_TFT_PIN_MOSI` | SPI data (MOSI) |
| `CS` | **GPIO5** | `NEXUS_TFT_PIN_CS` | Chip select |
| `DC` / `A0` / `RS` | **GPIO2** | `NEXUS_TFT_PIN_DC` | Data/command |
| `RES` / `RST` | **GPIO4** | `NEXUS_TFT_PIN_RST` | Reset |
| `BLK` / `LED` | **GPIO15** | `NEXUS_TFT_PIN_BLK` | Backlight (PWM brightness) |

- **SPI host:** `SPI2_HOST` at **26 MHz** (`NEXUS_TFT_SPI_CLOCK_HZ`).
- **Resolution:** 128 × 160, portrait native.
- **Backlight:** driven via PWM; the **Display brightness** setting (default 80 %)
  maps to the `BLK` duty cycle.

### Wiring diagram

```
   ESP32-WROOM-32                     ST7735 1.8" TFT
  ┌──────────────┐                   ┌──────────────┐
  │          3V3 ├───────────────────┤ VCC          │
  │          GND ├───────────────────┤ GND          │
  │   GPIO18 SCLK├───────────────────┤ SCL / SCK    │
  │   GPIO23 MOSI├───────────────────┤ SDA / MOSI   │
  │     GPIO5 CS ├───────────────────┤ CS           │
  │     GPIO2 DC ├───────────────────┤ DC / A0      │
  │    GPIO4 RST ├───────────────────┤ RES          │
  │   GPIO15 BLK ├───────────────────┤ BLK / LED    │
  └──────────────┘                   └──────────────┘
```

### Panel variants (red / green / black tab)

ST7735 boards ship with different initialisation offsets, identified by the
colour of the protective tab. If after wiring you see a **colour-inverted,
shifted, or bordered** image, that's the panel variant - adjust the init
sequence / offsets in [`display.c`](../firmware/main/display.c). See
[Troubleshooting → Display](../docs/TROUBLESHOOTING.md#display-st7735).

### What the screen shows

Device name, IP address, presence state, motion level, signal quality,
packets/second, and overall system status - refreshed every
`NEXUS_DISPLAY_REFRESH_MS` (500 ms).

---

## Power & placement

- Power from a **stable 5 V USB** supply and a short, good-quality cable.
  Under-powering causes brown-out resets, which both reboot the device and
  corrupt CSI measurements.
- Mount the ESP32 **stationary** and in the open (a shelf or wall), antenna
  unobstructed, **3-8 m** from the router with the monitored area *between* the
  two. Detailed placement guidance is in the
  [Calibration guide](../docs/CALIBRATION.md#2-placement).

---

## Flashing pin notes

- Most dev boards enter the bootloader automatically. If flashing fails to
  connect, hold **BOOT** (GPIO0) while flashing starts and/or tap **EN/RST**.
- GPIO0, GPIO2, GPIO15 are **strapping pins**. GPIO2 and GPIO15 are used here
  for the display's DC and backlight; if you have flashing trouble *with the
  display attached*, disconnect `DC`/`BLK` during flashing, or reassign those
  pins in `app_config.h`.

See [Installation](../docs/INSTALLATION.md) for the full flashing procedure.
