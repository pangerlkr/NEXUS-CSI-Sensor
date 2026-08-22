/**
 * @file ota.h
 * @brief Over-the-air firmware update via streamed HTTP upload.
 *
 * The web layer streams an uploaded image through @ref ota_write; this module
 * writes it to the inactive OTA partition, validates it on @ref ota_end and
 * switches the boot partition. Combined with the bootloader rollback feature,
 * a freshly-flashed image that fails to confirm health (@ref ota_mark_valid) is
 * automatically rolled back on the next reboot.
 */
#ifndef NEXUS_OTA_H
#define NEXUS_OTA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Live OTA progress, safe to read from another task. */
typedef struct {
    bool     in_progress;
    bool     last_ok;
    size_t   received;
    size_t   total;         /**< 0 if the client did not send a length */
    int      percent;       /**< 0..100 (0 when total unknown) */
    char     message[64];
} ota_status_t;

/** Initialise OTA bookkeeping. Reports the running partition. */
esp_err_t ota_init(void);

/**
 * @brief Begin an update. @p total_size may be 0 if unknown.
 *
 * When @p total_size is non-zero it is checked against the target partition
 * before anything is erased, so an image that cannot possibly fit is refused
 * while the existing rollback copy is still intact.
 *
 * @return ESP_OK if the session started, ESP_ERR_INVALID_SIZE if @p total_size
 *         exceeds the target partition, ESP_ERR_INVALID_STATE if an update is
 *         already running, ESP_ERR_NOT_FOUND if there is no OTA partition.
 */
esp_err_t ota_begin(size_t total_size);

/** Write the next chunk of image data. */
esp_err_t ota_write(const void *data, size_t len);

/**
 * @brief Finalise the update: validate the image and set it as boot partition.
 *        The caller is expected to reboot afterwards.
 */
esp_err_t ota_end(void);

/** Abort an in-progress update and release resources. */
void ota_abort(void);

/** Copy the current progress into @p out. */
void ota_get_status(ota_status_t *out);

/**
 * @brief Confirm the running image is healthy, cancelling any pending
 *        rollback. Call once the system has reached a known-good state.
 */
esp_err_t ota_mark_valid(void);

/** @return name of the currently running partition (e.g. "ota_0"). */
const char *ota_running_partition(void);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_OTA_H */
