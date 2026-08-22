#ifndef STUB_TASK_H
#define STUB_TASK_H
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
#define pdPASS 1
static inline BaseType_t xTaskCreate(void (*fn)(void *), const char *n,
                                     uint32_t stack, void *arg,
                                     unsigned prio, TaskHandle_t *out) {
    (void)fn; (void)n; (void)stack; (void)arg; (void)prio;
    if (out) *out = NULL;
    return pdPASS;   /* the task body is never run on the host */
}
static inline void vTaskDelay(TickType_t t) { (void)t; }
static inline void vTaskDelete(TaskHandle_t h) { (void)h; }
#endif
