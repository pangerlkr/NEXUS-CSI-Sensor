/**
 * @file display.c
 * @brief ST7735 TFT driver + status screen (compiled only when enabled).
 */
#include "display.h"
#include "app_config.h"

#if NEXUS_ENABLE_DISPLAY

#include "config.h"
#include "wifi.h"
#include "csi.h"
#include "motion.h"
#include "logger.h"
#include "utils.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_attr.h"
#include "esp_task_wdt.h"
#include "esp_log.h"

static const char *TAG = "display";

/* ---- RGB565 palette (cyber blue / dark theme) -------------------- */
#define C_BG      0x0841   /* near-black navy   */
#define C_PANEL   0x10A2   /* dark blue panel   */
#define C_TEXT    0xFFFF   /* white             */
#define C_MUTED   0x8C71   /* grey              */
#define C_ACCENT  0x04FF   /* cyan/blue accent  */
#define C_OK      0x2FEB   /* green             */
#define C_WARN    0xFD20   /* amber             */
#define C_ALERT   0xF9A6   /* red/pink          */

/* Some ST7735 panels need a pixel offset; adjust if you see a shifted image. */
#define TFT_COL_OFFSET 0
#define TFT_ROW_OFFSET 0

/* ST7735 command set (subset). */
#define ST77_SWRESET 0x01
#define ST77_SLPOUT  0x11
#define ST77_INVOFF  0x20
#define ST77_DISPON  0x29
#define ST77_CASET   0x2A
#define ST77_RASET   0x2B
#define ST77_RAMWR   0x2C
#define ST77_MADCTL  0x36
#define ST77_COLMOD  0x3A
#define ST77_FRMCTR1 0xB1
#define ST77_INVCTR  0xB4
#define ST77_PWCTR1  0xC0
#define ST77_PWCTR2  0xC1
#define ST77_VMCTR1  0xC5
#define ST77_GMCTRP1 0xE0
#define ST77_GMCTRN1 0xE1

static spi_device_handle_t s_spi;

/* Classic 5x7 ASCII font (glyphs 0x20..0x7F), 5 columns per glyph. */
static const uint8_t FONT5x7[96][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14}, {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00}, {0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00}, {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02}, {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00}, {0x00,0x08,0x14,0x22,0x41}, {0x14,0x14,0x14,0x14,0x14},
    {0x41,0x22,0x14,0x08,0x00}, {0x02,0x01,0x51,0x09,0x06}, {0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00}, {0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40}, {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02}, {0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00},
    {0x7F,0x10,0x28,0x44,0x00}, {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38}, {0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C}, {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00}, {0x00,0x00,0x7F,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00}, {0x08,0x04,0x08,0x10,0x08}, {0x00,0x00,0x00,0x00,0x00},
};

/* ---- Low-level SPI helpers --------------------------------------- */
static void tft_send(const uint8_t *data, size_t len, bool is_data)
{
    if (len == 0) return;
    gpio_set_level(NEXUS_TFT_PIN_DC, is_data ? 1 : 0);
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void tft_cmd(uint8_t c)               { tft_send(&c, 1, false); }
static void tft_data8(uint8_t d)             { tft_send(&d, 1, true); }
static void tft_datan(const uint8_t *d, size_t n) { tft_send(d, n, true); }

static void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    x0 += TFT_COL_OFFSET; x1 += TFT_COL_OFFSET;
    y0 += TFT_ROW_OFFSET; y1 += TFT_ROW_OFFSET;
    uint8_t buf[4];
    tft_cmd(ST77_CASET);
    buf[0] = x0 >> 8; buf[1] = x0 & 0xFF; buf[2] = x1 >> 8; buf[3] = x1 & 0xFF;
    tft_datan(buf, 4);
    tft_cmd(ST77_RASET);
    buf[0] = y0 >> 8; buf[1] = y0 & 0xFF; buf[2] = y1 >> 8; buf[3] = y1 & 0xFF;
    tft_datan(buf, 4);
    tft_cmd(ST77_RAMWR);
}

static void tft_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= NEXUS_TFT_WIDTH || y >= NEXUS_TFT_HEIGHT) return;
    if (x + w > NEXUS_TFT_WIDTH)  w = NEXUS_TFT_WIDTH - x;
    if (y + h > NEXUS_TFT_HEIGHT) h = NEXUS_TFT_HEIGHT - y;
    /* A zero-width or zero-height fill is a normal request: the motion bar is
     * exactly 0 px wide whenever the score is 0, which is the idle state. Reject
     * it here, because x + w - 1 would underflow into an inverted window and the
     * SPI transaction below would have a length of zero bits. */
    if (w == 0 || h == 0) return;
    tft_set_window(x, y, x + w - 1, y + h - 1);

    uint8_t hi = color >> 8, lo = color & 0xFF;
    /* SPI DMA wants a word-aligned transmit buffer. */
    WORD_ALIGNED_ATTR static uint8_t line[NEXUS_TFT_WIDTH * 2];
    for (uint16_t i = 0; i < w; ++i) {
        line[i * 2] = hi;
        line[i * 2 + 1] = lo;
    }
    gpio_set_level(NEXUS_TFT_PIN_DC, 1);
    for (uint16_t row = 0; row < h; ++row) {
        spi_transaction_t t = { .length = (size_t)w * 2 * 8, .tx_buffer = line };
        spi_device_polling_transmit(s_spi, &t);
    }
}

static void tft_draw_char(uint16_t x, uint16_t y, char ch, uint16_t fg, uint16_t bg, uint8_t scale)
{
    if (ch < 0x20 || ch > 0x7F) ch = '?';
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    const uint8_t *glyph = FONT5x7[ch - 0x20];
    /* Build a 6x8 cell scaled by @p scale into a pixel buffer, then blit. */
    uint16_t cw = 6 * scale, chh = 8 * scale;
    static uint16_t cell[6 * 8 * 4 * 4]; /* max scale 4 */
    for (uint8_t col = 0; col < 6; ++col) {
        uint8_t bits = (col < 5) ? glyph[col] : 0x00;
        for (uint8_t rowp = 0; rowp < 8; ++rowp) {
            uint16_t color = (bits & (1 << rowp)) ? fg : bg;
            for (uint8_t sy = 0; sy < scale; ++sy) {
                for (uint8_t sx = 0; sx < scale; ++sx) {
                    uint16_t px = col * scale + sx;
                    uint16_t py = rowp * scale + sy;
                    cell[py * cw + px] = color;
                }
            }
        }
    }
    tft_set_window(x, y, x + cw - 1, y + chh - 1);
    /* Convert to big-endian byte stream. Word-aligned for SPI DMA. */
    WORD_ALIGNED_ATTR static uint8_t out[6 * 8 * 4 * 4 * 2];
    size_t px_count = (size_t)cw * chh;
    for (size_t i = 0; i < px_count; ++i) {
        out[i * 2] = cell[i] >> 8;
        out[i * 2 + 1] = cell[i] & 0xFF;
    }
    tft_datan(out, px_count * 2);
}

static void tft_draw_text(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale)
{
    uint16_t cx = x;
    for (; *s; ++s) {
        if (cx + 6 * scale > NEXUS_TFT_WIDTH) break;
        tft_draw_char(cx, y, *s, fg, bg, scale);
        cx += 6 * scale;
    }
}

/* ---- Panel init -------------------------------------------------- */
static void tft_hw_reset(void)
{
    gpio_set_level(NEXUS_TFT_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(NEXUS_TFT_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(NEXUS_TFT_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static void tft_panel_init(void)
{
    tft_hw_reset();
    tft_cmd(ST77_SWRESET);         vTaskDelay(pdMS_TO_TICKS(150));
    tft_cmd(ST77_SLPOUT);          vTaskDelay(pdMS_TO_TICKS(255));

    tft_cmd(ST77_FRMCTR1);
    { uint8_t d[] = {0x01, 0x2C, 0x2D}; tft_datan(d, sizeof(d)); }
    tft_cmd(ST77_INVCTR);          tft_data8(0x07);
    tft_cmd(ST77_PWCTR1);
    { uint8_t d[] = {0xA2, 0x02, 0x84}; tft_datan(d, sizeof(d)); }
    tft_cmd(ST77_PWCTR2);          tft_data8(0xC5);
    tft_cmd(ST77_VMCTR1);
    { uint8_t d[] = {0x0E, 0x00}; tft_datan(d, sizeof(d)); }
    tft_cmd(ST77_INVOFF);
    tft_cmd(ST77_MADCTL);          tft_data8(0xC8); /* RGB, row/col order */
    tft_cmd(ST77_COLMOD);          tft_data8(0x05); /* 16-bit/pixel */

    tft_cmd(ST77_GMCTRP1);
    { uint8_t d[] = {0x02,0x1c,0x07,0x12,0x37,0x32,0x29,0x2d,
                     0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10}; tft_datan(d, sizeof(d)); }
    tft_cmd(ST77_GMCTRN1);
    { uint8_t d[] = {0x03,0x1d,0x07,0x06,0x2E,0x2C,0x29,0x2D,
                     0x2E,0x2E,0x37,0x3F,0x00,0x00,0x02,0x10}; tft_datan(d, sizeof(d)); }

    tft_cmd(ST77_DISPON);          vTaskDelay(pdMS_TO_TICKS(100));
}

/* ---- Backlight (LEDC PWM) ---------------------------------------- */
static void backlight_init(void)
{
    ledc_timer_config_t tcfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&tcfg);

    ledc_channel_config_t ccfg = {
        .gpio_num   = NEXUS_TFT_PIN_BLK,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ccfg);
}

void display_set_brightness(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t duty = (uint32_t)percent * 255U / 100U;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/* ---- Init -------------------------------------------------------- */
esp_err_t display_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << NEXUS_TFT_PIN_DC) |
                        (1ULL << NEXUS_TFT_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);

    spi_bus_config_t bus = {
        .mosi_io_num     = NEXUS_TFT_PIN_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = NEXUS_TFT_PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = NEXUS_TFT_WIDTH * NEXUS_TFT_HEIGHT * 2 + 16,
    };
    esp_err_t err = spi_bus_initialize(NEXUS_TFT_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    spi_device_interface_config_t dev = {
        .clock_speed_hz = NEXUS_TFT_SPI_CLOCK_HZ,
        .mode           = 0,
        .spics_io_num   = NEXUS_TFT_PIN_CS,
        .queue_size     = 4,
        .flags          = SPI_DEVICE_NO_DUMMY,
    };
    err = spi_bus_add_device(NEXUS_TFT_SPI_HOST, &dev, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device: %s", esp_err_to_name(err));
        return err;
    }

    backlight_init();
    tft_panel_init();
    tft_fill_rect(0, 0, NEXUS_TFT_WIDTH, NEXUS_TFT_HEIGHT, C_BG);

    nexus_config_t cfg;
    config_get(&cfg);
    display_set_brightness(cfg.display_brightness);

    /* Splash. */
    tft_draw_text(6, 60, "NEXUS", C_ACCENT, C_BG, 3);
    tft_draw_text(10, 92, "CSI SENSOR", C_TEXT, C_BG, 1);
    LOG_INFO("Display initialised (ST7735 %dx%d)", NEXUS_TFT_WIDTH, NEXUS_TFT_HEIGHT);
    return ESP_OK;
}

/* ---- Rendering --------------------------------------------------- */
static uint16_t state_color(motion_state_t st)
{
    switch (st) {
        case MOTION_STATE_HIGH_MOTION: return C_ALERT;
        case MOTION_STATE_MOTION:      return C_WARN;
        case MOTION_STATE_PRESENCE:    return C_ACCENT;
        default:                       return C_OK;
    }
}

static void draw_labeled(uint16_t y, const char *label, const char *value, uint16_t vcolor)
{
    tft_fill_rect(0, y, NEXUS_TFT_WIDTH, 10, C_BG);
    tft_draw_text(4, y, label, C_MUTED, C_BG, 1);
    tft_draw_text(58, y, value, vcolor, C_BG, 1);
}

static void render(void)
{
    nexus_config_t cfg;
    config_get(&cfg);
    motion_result_t mr;
    motion_get_result(&mr);
    csi_metrics_t cm;
    csi_get_metrics(&cm);

    char ip[16];
    wifi_get_ip_str(ip, sizeof(ip));
    char buf[24];

    /* Header bar coloured by state. */
    uint16_t hc = state_color(mr.state);
    tft_fill_rect(0, 0, NEXUS_TFT_WIDTH, 16, C_PANEL);
    tft_draw_text(4, 4, cfg.device_name, C_TEXT, C_PANEL, 1);
    tft_fill_rect(0, 16, NEXUS_TFT_WIDTH, 2, hc);

    uint16_t y = 24;
    draw_labeled(y, "IP", ip[0] ? ip : "-", C_TEXT);            y += 14;
    draw_labeled(y, "STATE", motion_state_str(mr.state), hc);   y += 14;

    snprintf(buf, sizeof(buf), "%s", mr.presence ? "YES" : "no");
    draw_labeled(y, "PRESENCE", buf, mr.presence ? C_ACCENT : C_MUTED); y += 14;

    snprintf(buf, sizeof(buf), "%3.0f %%", (double)mr.motion_intensity);
    draw_labeled(y, "MOTION", buf, C_TEXT);                     y += 14;

    snprintf(buf, sizeof(buf), "%3.0f %%", (double)mr.signal_quality);
    draw_labeled(y, "SIGNAL", buf, C_TEXT);                     y += 14;

    snprintf(buf, sizeof(buf), "%.1f/s", (double)cm.packets_per_sec);
    draw_labeled(y, "PACKETS", buf, C_TEXT);                    y += 14;

    snprintf(buf, sizeof(buf), "%d dBm", (int)cm.rssi);
    draw_labeled(y, "RSSI", buf, C_TEXT);                       y += 16;

    /* Motion bar. */
    tft_fill_rect(4, y, NEXUS_TFT_WIDTH - 8, 8, C_PANEL);
    uint16_t bw = (uint16_t)((NEXUS_TFT_WIDTH - 8) * utils_clampf(mr.motion_score, 0, 1));
    tft_fill_rect(4, y, bw, 8, hc);
    y += 14;

    /* Footer status. */
    const char *status = csi_is_active() ? "CSI LIVE" : "NO CSI";
    draw_labeled(y, "STATUS", status, csi_is_active() ? C_OK : C_ALERT);
}

static void display_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    /* Clear splash. */
    tft_fill_rect(0, 0, NEXUS_TFT_WIDTH, NEXUS_TFT_HEIGHT, C_BG);

    static uint8_t last_brightness = 255;
    for (;;) {
        esp_task_wdt_reset();

        nexus_config_t cfg;
        config_get(&cfg);
        if (cfg.display_brightness != last_brightness) {
            display_set_brightness(cfg.display_brightness);
            last_brightness = cfg.display_brightness;
        }

        render();
        vTaskDelay(pdMS_TO_TICKS(NEXUS_DISPLAY_REFRESH_MS));
    }
}

esp_err_t display_start_task(void)
{
    BaseType_t ok = xTaskCreate(display_task, "nexus_display",
                                NEXUS_TASK_STACK_DISPLAY, NULL,
                                NEXUS_TASK_PRIO_DISPLAY, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

#else /* NEXUS_ENABLE_DISPLAY == 0 : no-op stubs */

esp_err_t display_init(void)          { return ESP_OK; }
esp_err_t display_start_task(void)    { return ESP_OK; }
void      display_set_brightness(uint8_t percent) { (void)percent; }

#endif /* NEXUS_ENABLE_DISPLAY */
