/**
 * @file display.h
 * @brief Optional ST7735 128x160 TFT status display.
 *
 * Renders a compact live status screen (device name, IP, presence, motion
 * level, signal quality, packet rate, system status). Compiled out entirely
 * when @c NEXUS_ENABLE_DISPLAY is 0, in which case the functions become no-ops
 * so the rest of the firmware is unaffected.
 */
#ifndef NEXUS_DISPLAY_H
#define NEXUS_DISPLAY_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise the SPI bus, panel and backlight. */
esp_err_t display_init(void);

/** Spawn the display refresh task. */
esp_err_t display_start_task(void);

/** Set backlight brightness (0..100 %). */
void display_set_brightness(uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_DISPLAY_H */
