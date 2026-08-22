#ifndef STUB_WDT_H
#define STUB_WDT_H
#include "esp_err.h"
static inline esp_err_t esp_task_wdt_add(void *h) { (void)h; return ESP_OK; }
static inline esp_err_t esp_task_wdt_reset(void) { return ESP_OK; }
#endif
