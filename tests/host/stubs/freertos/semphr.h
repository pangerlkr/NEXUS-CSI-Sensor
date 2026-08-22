#ifndef STUB_SEMPHR_H
#define STUB_SEMPHR_H
#include "freertos/FreeRTOS.h"
/* Single-threaded host stub: the mutex is a counter so the test can assert the
 * lock is always released, which is the property the real code depends on. */
typedef struct { int held; int takes; int gives; } stub_mutex_t;
typedef stub_mutex_t *SemaphoreHandle_t;
extern int g_lock_fail;   /* set to make the next take fail, as a timeout would */
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return (SemaphoreHandle_t)calloc(1, sizeof(stub_mutex_t));
}
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t t) {
    (void)t;
    if (g_lock_fail) { g_lock_fail = 0; return pdFALSE; }
    h->takes++; h->held++; return pdTRUE;
}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t h) {
    h->gives++; h->held--; return pdTRUE;
}
static inline void vSemaphoreDelete(SemaphoreHandle_t h) { free(h); }
#endif
