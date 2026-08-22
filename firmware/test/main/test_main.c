/**
 * @file test_main.c
 * @brief On-target Unity test suite for the NEXUS CSI Sensor pure-logic modules.
 *
 * These tests exercise the modules that do not depend on live hardware state:
 * the circular buffer, the JSON builder/parser and the small utility helpers.
 * They run on the ESP32 itself (or under QEMU) so the code under test is the
 * exact code that ships in the firmware, compiled by the same toolchain.
 *
 * Build & run:
 *   idf.py -C firmware/test set-target esp32
 *   idf.py -C firmware/test flash monitor
 *
 * The suite also includes throughput/stress cases (see TEST_CASE names ending
 * in "stress") that push a large number of elements through the data
 * structures to confirm bounded memory use and correct wrap-around.
 *
 * @copyright MIT License. See LICENSE at the repository root.
 */
#include <string.h>
#include <math.h>

#include "unity.h"
#include "esp_log.h"

#include "ringbuffer.h"
#include "json.h"
#include "utils.h"

static const char *TAG = "test";

/* ===================================================================== */
/* ringbuffer                                                            */
/* ===================================================================== */

TEST_CASE("ringbuffer: FIFO order and count", "[ringbuffer]")
{
    ringbuffer_t rb;
    TEST_ASSERT_EQUAL(ESP_OK, ringbuffer_init(&rb, sizeof(int), 4));
    TEST_ASSERT_EQUAL_UINT(0, ringbuffer_count(&rb));

    int v;
    for (int i = 1; i <= 3; i++) {
        bool overwrote = ringbuffer_push(&rb, &i);
        TEST_ASSERT_FALSE(overwrote);
    }
    TEST_ASSERT_EQUAL_UINT(3, ringbuffer_count(&rb));
    TEST_ASSERT_EQUAL_UINT64(3, ringbuffer_total(&rb));

    TEST_ASSERT_TRUE(ringbuffer_pop(&rb, &v));
    TEST_ASSERT_EQUAL_INT(1, v);            /* oldest out first */
    TEST_ASSERT_EQUAL_UINT(2, ringbuffer_count(&rb));

    ringbuffer_free(&rb);
}

TEST_CASE("ringbuffer: overwrite oldest when full", "[ringbuffer]")
{
    ringbuffer_t rb;
    TEST_ASSERT_EQUAL(ESP_OK, ringbuffer_init(&rb, sizeof(int), 4));

    for (int i = 1; i <= 4; i++) ringbuffer_push(&rb, &i);   /* [1,2,3,4] full */
    TEST_ASSERT_EQUAL_UINT(4, ringbuffer_count(&rb));

    int five = 5;
    bool overwrote = ringbuffer_push(&rb, &five);            /* drops 1 -> [2,3,4,5] */
    TEST_ASSERT_TRUE(overwrote);
    TEST_ASSERT_EQUAL_UINT(4, ringbuffer_count(&rb));
    TEST_ASSERT_EQUAL_UINT64(5, ringbuffer_total(&rb));

    int out[4] = {0};
    size_t n = ringbuffer_snapshot(&rb, out, 4);
    TEST_ASSERT_EQUAL_UINT(4, n);
    TEST_ASSERT_EQUAL_INT(2, out[0]);       /* snapshot is oldest-first */
    TEST_ASSERT_EQUAL_INT(5, out[3]);

    ringbuffer_free(&rb);
}

TEST_CASE("ringbuffer: pop empty and clear", "[ringbuffer]")
{
    ringbuffer_t rb;
    TEST_ASSERT_EQUAL(ESP_OK, ringbuffer_init(&rb, sizeof(int), 4));

    int v;
    TEST_ASSERT_FALSE(ringbuffer_pop(&rb, &v));   /* empty */

    int one = 1;
    ringbuffer_push(&rb, &one);
    ringbuffer_clear(&rb);
    TEST_ASSERT_EQUAL_UINT(0, ringbuffer_count(&rb));
    TEST_ASSERT_FALSE(ringbuffer_pop(&rb, &v));

    ringbuffer_free(&rb);
}

TEST_CASE("ringbuffer: high-volume wrap-around stress", "[ringbuffer][stress]")
{
    const size_t CAP = 128;
    const uint32_t N = 100000;
    ringbuffer_t rb;
    TEST_ASSERT_EQUAL(ESP_OK, ringbuffer_init(&rb, sizeof(uint32_t), CAP));

    for (uint32_t i = 0; i < N; i++) ringbuffer_push(&rb, &i);

    TEST_ASSERT_EQUAL_UINT(CAP, ringbuffer_count(&rb));      /* bounded */
    TEST_ASSERT_EQUAL_UINT64(N, ringbuffer_total(&rb));      /* lifetime count */

    uint32_t out[128];
    size_t n = ringbuffer_snapshot(&rb, out, CAP);
    TEST_ASSERT_EQUAL_UINT(CAP, n);
    TEST_ASSERT_EQUAL_UINT32(N - 1, out[CAP - 1]);           /* most recent */
    TEST_ASSERT_EQUAL_UINT32(N - CAP, out[0]);               /* oldest retained */

    ringbuffer_free(&rb);
    ESP_LOGI(TAG, "pushed %u elements through a %u-slot ring", (unsigned)N, (unsigned)CAP);
}

/* ===================================================================== */
/* json builder + parser                                                 */
/* ===================================================================== */

TEST_CASE("json: builder + parser round-trip of scalars", "[json]")
{
    json_builder_t b;
    TEST_ASSERT_TRUE(json_builder_init(&b, 64));

    json_obj_open(&b);
    json_kv_str(&b, "name", "nexus-csi");
    json_kv_int(&b, "count", -42);
    json_kv_uint(&b, "packets", 12345u);
    json_kv_float(&b, "score", 0.75f, 2);
    json_kv_bool(&b, "active", true);
    json_obj_close(&b);

    size_t len = 0;
    const char *s = json_builder_str(&b, &len);
    TEST_ASSERT_FALSE(b.error);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_UINT(strlen(s), len);
    TEST_ASSERT_EQUAL_CHAR('{', s[0]);
    TEST_ASSERT_EQUAL_CHAR('}', s[len - 1]);

    /* Validate by parsing the produced document (whitespace-agnostic). */
    json_doc_t *doc = json_parse(s, len);
    TEST_ASSERT_NOT_NULL(doc);

    char name[32];
    TEST_ASSERT_TRUE(json_get_str(doc, "name", name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("nexus-csi", name);

    int count = 0;
    TEST_ASSERT_TRUE(json_get_int(doc, "count", &count));
    TEST_ASSERT_EQUAL_INT(-42, count);

    int packets = 0;
    TEST_ASSERT_TRUE(json_get_int(doc, "packets", &packets));
    TEST_ASSERT_EQUAL_INT(12345, packets);

    float score = 0.0f;
    TEST_ASSERT_TRUE(json_get_float(doc, "score", &score));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, score);

    bool active = false;
    TEST_ASSERT_TRUE(json_get_bool(doc, "active", &active));
    TEST_ASSERT_TRUE(active);

    json_free(doc);
    json_builder_free(&b);
}

TEST_CASE("json: nested objects and arrays stay valid", "[json]")
{
    json_builder_t b;
    TEST_ASSERT_TRUE(json_builder_init(&b, 32));

    json_obj_open(&b);
    json_kv_obj_open(&b, "wifi");
    json_kv_str(&b, "ssid", "lab-ap");
    json_kv_int(&b, "rssi", -57);
    json_obj_close(&b);
    json_arr_open(&b, "scores");
    json_elem_float(&b, 0.1f, 2);
    json_elem_float(&b, 0.2f, 2);
    json_elem_int(&b, 3);
    json_arr_close(&b);
    json_obj_close(&b);

    size_t len = 0;
    const char *s = json_builder_str(&b, &len);
    TEST_ASSERT_FALSE(b.error);

    /* The interesting property: the whole thing is still valid JSON. */
    json_doc_t *doc = json_parse(s, len);
    TEST_ASSERT_NOT_NULL(doc);
    json_free(doc);
    json_builder_free(&b);
}

TEST_CASE("json: parser rejects malformed input", "[json]")
{
    const char *bad = "{ this is not json ]";
    json_doc_t *doc = json_parse(bad, strlen(bad));
    TEST_ASSERT_NULL(doc);
}

TEST_CASE("json: builder grows past initial capacity (stress)", "[json][stress]")
{
    json_builder_t b;
    TEST_ASSERT_TRUE(json_builder_init(&b, 16));   /* deliberately tiny */

    json_obj_open(&b);
    json_arr_open(&b, "series");
    for (int i = 0; i < 1000; i++) json_elem_int(&b, i);
    json_arr_close(&b);
    json_obj_close(&b);

    size_t len = 0;
    const char *s = json_builder_str(&b, &len);
    TEST_ASSERT_FALSE(b.error);                    /* grew without failing */
    TEST_ASSERT_TRUE(len > 2000);
    TEST_ASSERT_EQUAL_CHAR('{', s[0]);
    TEST_ASSERT_EQUAL_CHAR('}', s[len - 1]);

    json_doc_t *doc = json_parse(s, len);
    TEST_ASSERT_NOT_NULL(doc);
    json_free(doc);
    json_builder_free(&b);
}

/* ===================================================================== */
/* utils                                                                 */
/* ===================================================================== */

TEST_CASE("utils: clamp and map", "[utils]")
{
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, utils_clampf(5.0f, 0.0f, 1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, utils_clampf(-3.0f, 0.0f, 1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, utils_clampf(0.5f, 0.0f, 1.0f));

    TEST_ASSERT_EQUAL_INT(100, utils_clampi(250, 0, 100));
    TEST_ASSERT_EQUAL_INT(0, utils_clampi(-5, 0, 100));
    TEST_ASSERT_EQUAL_INT(42, utils_clampi(42, 0, 100));

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 50.0f, utils_mapf(0.5f, 0.0f, 1.0f, 0.0f, 100.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f,  utils_mapf(-90.0f, -90.0f, -30.0f, 0.0f, 100.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 100.0f, utils_mapf(-30.0f, -90.0f, -30.0f, 0.0f, 100.0f));
}

TEST_CASE("utils: bytes to hex", "[utils]")
{
    const uint8_t in[] = {0xDE, 0xAD, 0xBE, 0xEF};
    char out[9];
    utils_bytes_to_hex(in, sizeof(in), out);
    TEST_ASSERT_EQUAL_STRING("deadbeef", out);
}

TEST_CASE("utils: SHA-256 known-answer vector", "[utils]")
{
    /* FIPS 180-4 test vector: SHA-256("abc"). */
    char hex[65];
    TEST_ASSERT_TRUE(utils_sha256_hex("abc", 3, hex));
    TEST_ASSERT_EQUAL_STRING(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex);
}

TEST_CASE("utils: random hex length and charset", "[utils]")
{
    char a[33];
    utils_random_hex(a, 32);
    TEST_ASSERT_EQUAL_UINT(32, strlen(a));
    for (size_t i = 0; i < 32; i++) {
        char c = a[i];
        bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        TEST_ASSERT_TRUE(is_hex);
    }
}

TEST_CASE("utils: constant-time string compare", "[utils]")
{
    TEST_ASSERT_TRUE(utils_consttime_equal("token123", "token123"));
    TEST_ASSERT_FALSE(utils_consttime_equal("token123", "token124"));
    TEST_ASSERT_FALSE(utils_consttime_equal("short", "longer-string"));
}

TEST_CASE("utils: uptime formatting", "[utils]")
{
    char buf[32];
    /* 1 day + 1 hour + 1 minute + 1 second. */
    size_t n = utils_format_uptime(90061, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(strlen(buf), n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "1d"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "01:01:01"));
}

TEST_CASE("utils: strlcpy truncates and terminates", "[utils]")
{
    char dst[4];
    utils_strlcpy(dst, "abcdefgh", sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("abc", dst);   /* 3 chars + NUL */
    TEST_ASSERT_EQUAL_UINT(3, strlen(dst));
}

/* ===================================================================== */
/* Unity entry point                                                     */
/* ===================================================================== */

void app_main(void)
{
    ESP_LOGI(TAG, "NEXUS CSI Sensor test suite");
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
}
