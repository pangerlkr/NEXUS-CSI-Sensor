/**
 * @file utils.c
 * @brief Implementation of small firmware-wide helpers.
 */
#include "utils.h"

#include <string.h>
#include <stdio.h>

#include "esp_timer.h"
#include "esp_random.h"
#include "mbedtls/sha256.h"

static const char k_hex_lut[] = "0123456789abcdef";

uint32_t utils_millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

int64_t utils_micros(void)
{
    return esp_timer_get_time();
}

float utils_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int utils_clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float utils_mapf(float v, float in_lo, float in_hi, float out_lo, float out_hi)
{
    if (in_hi == in_lo) {
        return out_lo;
    }
    float t = (v - in_lo) / (in_hi - in_lo);
    return out_lo + t * (out_hi - out_lo);
}

void utils_bytes_to_hex(const uint8_t *in, size_t in_len, char *out)
{
    for (size_t i = 0; i < in_len; ++i) {
        out[i * 2]     = k_hex_lut[(in[i] >> 4) & 0x0F];
        out[i * 2 + 1] = k_hex_lut[in[i] & 0x0F];
    }
    out[in_len * 2] = '\0';
}

bool utils_sha256_hex(const void *data, size_t len, char *out_hex)
{
    uint8_t digest[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);

    /* Second argument 0 selects SHA-256 (not SHA-224). */
    if (mbedtls_sha256_starts(&ctx, 0) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    if (mbedtls_sha256_update(&ctx, (const unsigned char *)data, len) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    if (mbedtls_sha256_finish(&ctx, digest) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    mbedtls_sha256_free(&ctx);

    utils_bytes_to_hex(digest, sizeof(digest), out_hex);
    return true;
}

void utils_random_hex(char *out, size_t hex_len)
{
    for (size_t i = 0; i < hex_len; i += 2) {
        uint32_t r = esp_random();
        out[i] = k_hex_lut[(r >> 4) & 0x0F];
        if (i + 1 < hex_len) {
            out[i + 1] = k_hex_lut[r & 0x0F];
        }
    }
    out[hex_len] = '\0';
}

bool utils_consttime_equal(const char *a, const char *b)
{
    size_t la = strlen(a);
    size_t lb = strlen(b);
    /* Accumulate at full width. Narrowing the length XOR to a byte would discard
     * every difference that is a multiple of 256, so lengths 1 and 257 would
     * compare as equal-length and only the first byte would then be examined. */
    size_t diff = la ^ lb;
    size_t n = (la < lb) ? la : lb;
    for (size_t i = 0; i < n; ++i) {
        diff |= (size_t)((unsigned char)a[i] ^ (unsigned char)b[i]);
    }
    return diff == 0;
}

size_t utils_format_uptime(uint32_t seconds, char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return 0;
    }

    uint32_t days = seconds / 86400U;
    uint32_t hrs  = (seconds % 86400U) / 3600U;
    uint32_t mins = (seconds % 3600U) / 60U;
    uint32_t secs = seconds % 60U;

    int n;
    if (days > 0) {
        n = snprintf(buf, buf_len, "%ud %02u:%02u:%02u",
                     (unsigned)days, (unsigned)hrs, (unsigned)mins, (unsigned)secs);
    } else {
        n = snprintf(buf, buf_len, "%02u:%02u:%02u",
                     (unsigned)hrs, (unsigned)mins, (unsigned)secs);
    }
    if (n < 0) {
        if (buf_len) buf[0] = '\0';
        return 0;
    }
    return (size_t)n < buf_len ? (size_t)n : buf_len - 1;
}

void utils_strlcpy(char *dst, const char *src, size_t dst_size)
{
    if (dst_size == 0) {
        return;
    }
    size_t i = 0;
    for (; i + 1 < dst_size && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}
