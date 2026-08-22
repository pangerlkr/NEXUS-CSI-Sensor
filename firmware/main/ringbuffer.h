/**
 * @file ringbuffer.h
 * @brief Generic, thread-safe fixed-capacity circular buffer.
 *
 * Stores elements of an arbitrary fixed size. When the buffer is full the
 * oldest element is overwritten (a common requirement for streaming sensor
 * data). All operations are guarded by an internal FreeRTOS mutex, so the
 * buffer is safe to share between the CSI callback context and worker tasks.
 */
#ifndef NEXUS_RINGBUFFER_H
#define NEXUS_RINGBUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Circular buffer control block. Treat fields as private. */
typedef struct {
    uint8_t          *data;      /**< Backing store (capacity * elem_size). */
    size_t            elem_size; /**< Size of a single element in bytes. */
    size_t            capacity;  /**< Maximum number of elements. */
    size_t            head;      /**< Index of next write slot. */
    size_t            count;     /**< Number of valid elements. */
    uint64_t          total_pushed; /**< Lifetime element count (monotonic). */
    SemaphoreHandle_t lock;      /**< Mutex protecting the structure. */
} ringbuffer_t;

/**
 * @brief Initialise a ring buffer, allocating its backing store.
 * @param rb        Buffer to initialise.
 * @param elem_size Size of one element in bytes (> 0).
 * @param capacity  Maximum number of elements (> 0).
 * @return ESP_OK, ESP_ERR_INVALID_ARG or ESP_ERR_NO_MEM.
 */
esp_err_t ringbuffer_init(ringbuffer_t *rb, size_t elem_size, size_t capacity);

/** Release the backing store and mutex. Safe to call on a zeroed struct. */
void ringbuffer_free(ringbuffer_t *rb);

/**
 * @brief Push one element, overwriting the oldest if full.
 *
 * Waits indefinitely for the internal mutex. Do not call this from a context
 * that must not block, such as a driver callback; use
 * @ref ringbuffer_push_timeout there instead.
 *
 * @return true if an old element was overwritten, false otherwise.
 */
bool ringbuffer_push(ringbuffer_t *rb, const void *elem);

/**
 * @brief Push one element, giving up if the mutex is not free in time.
 *
 * For callers that run somewhere blocking is unacceptable. The CSI receive
 * callback is the motivating case: it executes in the WiFi task, so waiting on a
 * mutex held by a slower reader stalls packet reception itself. Dropping the
 * occasional sample is much cheaper than that.
 *
 * Note the different return convention from @ref ringbuffer_push: this reports
 * whether the element was stored, not whether an old one was overwritten.
 *
 * @param timeout_ms Maximum wait for the mutex. 0 means try once and return.
 * @return true if the element was stored, false if it was dropped.
 */
bool ringbuffer_push_timeout(ringbuffer_t *rb, const void *elem,
                             uint32_t timeout_ms);

/**
 * @brief Remove and copy out the oldest element.
 * @return true if an element was returned, false if the buffer was empty.
 */
bool ringbuffer_pop(ringbuffer_t *rb, void *out);

/** @return Current number of valid elements. */
size_t ringbuffer_count(ringbuffer_t *rb);

/** @return Lifetime number of pushed elements. */
uint64_t ringbuffer_total(ringbuffer_t *rb);

/** Remove all elements (does not free memory). */
void ringbuffer_clear(ringbuffer_t *rb);

/**
 * @brief Copy up to @p max_elems of the most recent elements into @p out,
 *        ordered oldest-first. Non-destructive.
 * @param rb        Source buffer.
 * @param out       Destination array of at least @p max_elems elements.
 * @param max_elems Capacity of @p out in elements.
 * @return Number of elements copied.
 */
size_t ringbuffer_snapshot(ringbuffer_t *rb, void *out, size_t max_elems);

/**
 * @brief Copy a single element out by position, oldest first. Non-destructive.
 *
 * For readers that want to walk the buffer without a destination array big
 * enough to hold all of it. One element is copied per call, so the caller needs
 * room for one element rather than @ref ringbuffer_count of them.
 *
 * Positions are relative to the current contents, so they shift as elements are
 * pushed: whatever sits at index 1 becomes index 0 once the buffer is full and
 * wraps. A caller walking the whole buffer across many calls can therefore see
 * an element twice or miss one if writes land mid-walk. If that matters, dedupe
 * on something in the element itself rather than trusting the index.
 *
 * @param rb    Source buffer.
 * @param index 0 for the oldest held element, count-1 for the newest.
 * @param out   Destination for one element.
 * @return true if an element was copied, false if @p index is out of range.
 */
bool ringbuffer_peek(ringbuffer_t *rb, size_t index, void *out);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_RINGBUFFER_H */
