#ifndef STUB_FREERTOS_H
#define STUB_FREERTOS_H
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
#endif
