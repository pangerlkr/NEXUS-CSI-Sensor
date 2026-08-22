/**
 * @file ringbuffer.c
 * @brief Implementation of the generic thread-safe circular buffer.
 */
#include "ringbuffer.h"

#include <stdlib.h>
#include <string.h>

/* Take/give helpers keep the locking logic in one place. */
static inline bool rb_lock(ringbuffer_t *rb)
{
    return rb->lock && xSemaphoreTake(rb->lock, portMAX_DELAY) == pdTRUE;
}

static inline bool rb_lock_timeout(ringbuffer_t *rb, uint32_t timeout_ms)
{
    return rb->lock &&
           xSemaphoreTake(rb->lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static inline void rb_unlock(ringbuffer_t *rb)
{
    if (rb->lock) {
        xSemaphoreGive(rb->lock);
    }
}

esp_err_t ringbuffer_init(ringbuffer_t *rb, size_t elem_size, size_t capacity)
{
    if (rb == NULL || elem_size == 0 || capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(rb, 0, sizeof(*rb));

    rb->data = (uint8_t *)calloc(capacity, elem_size);
    if (rb->data == NULL) {
        return ESP_ERR_NO_MEM;
    }
    rb->lock = xSemaphoreCreateMutex();
    if (rb->lock == NULL) {
        free(rb->data);
        rb->data = NULL;
        return ESP_ERR_NO_MEM;
    }
    rb->elem_size = elem_size;
    rb->capacity  = capacity;
    rb->head      = 0;
    rb->count     = 0;
    rb->total_pushed = 0;
    return ESP_OK;
}

void ringbuffer_free(ringbuffer_t *rb)
{
    if (rb == NULL) {
        return;
    }
    if (rb->lock) {
        vSemaphoreDelete(rb->lock);
        rb->lock = NULL;
    }
    free(rb->data);
    rb->data = NULL;
    rb->capacity = 0;
    rb->count = 0;
}

bool ringbuffer_push(ringbuffer_t *rb, const void *elem)
{
    if (rb == NULL || rb->data == NULL || elem == NULL) {
        return false;
    }
    bool overwrote = false;
    if (!rb_lock(rb)) {
        return false;
    }
    memcpy(rb->data + rb->head * rb->elem_size, elem, rb->elem_size);
    rb->head = (rb->head + 1) % rb->capacity;
    if (rb->count < rb->capacity) {
        rb->count++;
    } else {
        overwrote = true; /* tail advanced implicitly by head wrap */
    }
    rb->total_pushed++;
    rb_unlock(rb);
    return overwrote;
}

bool ringbuffer_push_timeout(ringbuffer_t *rb, const void *elem,
                             uint32_t timeout_ms)
{
    if (rb == NULL || rb->data == NULL || elem == NULL) {
        return false;
    }
    if (!rb_lock_timeout(rb, timeout_ms)) {
        return false;   /* caller decides what a dropped element means */
    }
    memcpy(rb->data + rb->head * rb->elem_size, elem, rb->elem_size);
    rb->head = (rb->head + 1) % rb->capacity;
    if (rb->count < rb->capacity) {
        rb->count++;
    }
    rb->total_pushed++;
    rb_unlock(rb);
    return true;
}

bool ringbuffer_pop(ringbuffer_t *rb, void *out)
{
    if (rb == NULL || rb->data == NULL) {
        return false;
    }
    bool ok = false;
    if (!rb_lock(rb)) {
        return false;
    }
    if (rb->count > 0) {
        size_t tail = (rb->head + rb->capacity - rb->count) % rb->capacity;
        if (out) {
            memcpy(out, rb->data + tail * rb->elem_size, rb->elem_size);
        }
        rb->count--;
        ok = true;
    }
    rb_unlock(rb);
    return ok;
}

size_t ringbuffer_count(ringbuffer_t *rb)
{
    if (rb == NULL) {
        return 0;
    }
    size_t c = 0;
    if (rb_lock(rb)) {
        c = rb->count;
        rb_unlock(rb);
    }
    return c;
}

uint64_t ringbuffer_total(ringbuffer_t *rb)
{
    if (rb == NULL) {
        return 0;
    }
    uint64_t t = 0;
    if (rb_lock(rb)) {
        t = rb->total_pushed;
        rb_unlock(rb);
    }
    return t;
}

void ringbuffer_clear(ringbuffer_t *rb)
{
    if (rb == NULL) {
        return;
    }
    if (rb_lock(rb)) {
        rb->head = 0;
        rb->count = 0;
        rb_unlock(rb);
    }
}

size_t ringbuffer_snapshot(ringbuffer_t *rb, void *out, size_t max_elems)
{
    if (rb == NULL || rb->data == NULL || out == NULL || max_elems == 0) {
        return 0;
    }
    size_t copied = 0;
    if (!rb_lock(rb)) {
        return 0;
    }
    size_t n = rb->count < max_elems ? rb->count : max_elems;
    /* Oldest of the last n elements starts here. */
    size_t start = (rb->head + rb->capacity - n) % rb->capacity;
    uint8_t *dst = (uint8_t *)out;
    for (size_t i = 0; i < n; ++i) {
        size_t idx = (start + i) % rb->capacity;
        memcpy(dst + i * rb->elem_size,
               rb->data + idx * rb->elem_size,
               rb->elem_size);
    }
    copied = n;
    rb_unlock(rb);
    return copied;
}

bool ringbuffer_peek(ringbuffer_t *rb, size_t index, void *out)
{
    if (rb == NULL || rb->data == NULL || out == NULL) {
        return false;
    }
    if (!rb_lock(rb)) {
        return false;
    }
    bool ok = false;
    if (index < rb->count) {
        /* Same arithmetic as the snapshot: step forward from the oldest of the
         * count elements currently held. */
        size_t oldest = (rb->head + rb->capacity - rb->count) % rb->capacity;
        size_t idx = (oldest + index) % rb->capacity;
        memcpy(out, rb->data + idx * rb->elem_size, rb->elem_size);
        ok = true;
    }
    rb_unlock(rb);
    return ok;
}
