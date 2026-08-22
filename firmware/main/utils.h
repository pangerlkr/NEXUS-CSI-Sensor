/**
 * @file utils.h
 * @brief Small, dependency-free helpers used across the firmware.
 */
#ifndef NEXUS_UTILS_H
#define NEXUS_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @return Milliseconds since boot (monotonic, wraps after ~49 days). */
uint32_t utils_millis(void);

/** @return Microseconds since boot (monotonic 64-bit). */
int64_t utils_micros(void);

/** Clamp @p v to the inclusive range [@p lo, @p hi]. */
float utils_clampf(float v, float lo, float hi);

/** Clamp integer @p v to [@p lo, @p hi]. */
int utils_clampi(int v, int lo, int hi);

/** Linear re-map of @p v from [@p in_lo, @p in_hi] to [@p out_lo, @p out_hi]. */
float utils_mapf(float v, float in_lo, float in_hi, float out_lo, float out_hi);

/**
 * @brief Encode a byte buffer as lowercase hexadecimal.
 * @param in      Source bytes.
 * @param in_len  Number of source bytes.
 * @param out     Destination, must hold at least 2*in_len + 1 chars.
 */
void utils_bytes_to_hex(const uint8_t *in, size_t in_len, char *out);

/**
 * @brief Compute SHA-256(@p data) and store it as a 64-char hex string.
 * @param data    Input buffer.
 * @param len     Input length in bytes.
 * @param out_hex Destination buffer, must hold at least 65 bytes.
 * @return true on success.
 */
bool utils_sha256_hex(const void *data, size_t len, char *out_hex);

/**
 * @brief Fill @p out with @p hex_len random lowercase hex characters
 *        (backed by the hardware RNG), NUL-terminated.
 * @param out     Destination, must hold hex_len + 1 bytes.
 * @param hex_len Number of hex characters to generate (even numbers only).
 */
void utils_random_hex(char *out, size_t hex_len);

/**
 * @brief Constant-time comparison of two NUL-terminated strings of equal use.
 *        Prevents timing side-channels when comparing secrets/tokens.
 * @return true if the strings are identical.
 */
bool utils_consttime_equal(const char *a, const char *b);

/**
 * @brief Human-readable uptime formatter, e.g. "3d 04:12:07".
 * @param seconds Uptime in seconds.
 * @param buf     Destination buffer.
 * @param buf_len Size of @p buf.
 * @return Number of characters written (excluding NUL).
 */
size_t utils_format_uptime(uint32_t seconds, char *buf, size_t buf_len);

/** Safe bounded string copy that always NUL-terminates. */
void utils_strlcpy(char *dst, const char *src, size_t dst_size);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_UTILS_H */
