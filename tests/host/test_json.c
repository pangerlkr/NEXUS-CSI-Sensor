/**
 * @file test_json.c
 * @brief Host tests for the JSON builder in firmware/main/json.c.
 *
 * Covers the things that would corrupt an API response rather than merely look
 * wrong: string escaping, non-finite floats, comma placement, and the growth of
 * the output buffer. The parse half of json.c wraps cJSON and is not covered
 * here; see stubs/cJSON.h for why.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "json.h"

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { \
    printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void expect(const char *label, json_builder_t *b, const char *want)
{
    size_t len = 0;
    const char *got = json_builder_str(b, &len);
    CHECK(strcmp(got, want) == 0, "%s:\n  got  %s\n  want %s", label, got, want);
    CHECK(len == strlen(want), "%s: length %zu want %zu", label, len, strlen(want));
}

static void test_object(void)
{
    json_builder_t b;
    json_builder_init(&b, 64);
    json_obj_open(&b);
    json_kv_str(&b, "name", "nexus-csi");
    json_kv_int(&b, "rssi", -57);
    json_kv_uint(&b, "packets", 402113);
    json_kv_bool(&b, "presence", true);
    json_kv_float(&b, "score", 0.4249f, 2);
    json_obj_close(&b);
    expect("flat object", &b,
           "{\"name\":\"nexus-csi\",\"rssi\":-57,\"packets\":402113,"
           "\"presence\":true,\"score\":0.42}");
    json_builder_free(&b);
}

static void test_nesting(void)
{
    json_builder_t b;
    json_builder_init(&b, 16);   /* deliberately small: forces the growth path */
    json_obj_open(&b);
    json_kv_obj_open(&b, "wifi");
    json_kv_str(&b, "role", "STA");
    json_kv_int(&b, "channel", 6);
    json_obj_close(&b);
    json_arr_open(&b, "score");
    json_elem_float(&b, 0.0f, 2);
    json_elem_float(&b, 1.0f, 2);
    json_arr_close(&b);
    json_arr_open(&b, "state");
    json_elem_int(&b, 0);
    json_elem_int(&b, 2);
    json_arr_close(&b);
    json_arr_open(&b, "labels");
    json_elem_str(&b, "idle");
    json_elem_str(&b, "motion");
    json_arr_close(&b);
    json_obj_close(&b);
    expect("nested", &b,
           "{\"wifi\":{\"role\":\"STA\",\"channel\":6},"
           "\"score\":[0.00,1.00],\"state\":[0,2],"
           "\"labels\":[\"idle\",\"motion\"]}");
    json_builder_free(&b);
}

static void test_escaping(void)
{
    json_builder_t b;
    json_builder_init(&b, 32);
    json_obj_open(&b);
    json_kv_str(&b, "msg", "he said \"hi\"\\ then\nleft\twith\ra \b\f");
    json_kv_str(&b, "ctrl", "\x01\x1f");
    json_kv_str(&b, "null", NULL);
    json_obj_close(&b);
    expect("escaping", &b,
           "{\"msg\":\"he said \\\"hi\\\"\\\\ then\\nleft\\twith\\ra \\b\\f\","
           "\"ctrl\":\"\\u0001\\u001f\",\"null\":\"\"}");
    json_builder_free(&b);
}

static void test_non_finite(void)
{
    /* A single nan or inf in the output makes the whole document unparseable, so
     * the dashboard stops updating rather than showing one wrong number. Both the
     * key and the array element paths must substitute. */
    json_builder_t b;
    json_builder_init(&b, 64);
    json_obj_open(&b);
    json_kv_float(&b, "nan", NAN, 2);
    json_kv_float(&b, "inf", INFINITY, 2);
    json_kv_float(&b, "ninf", -INFINITY, 3);
    json_arr_open(&b, "arr");
    json_elem_float(&b, NAN, 2);
    json_elem_float(&b, 1.5f, 2);
    json_arr_close(&b);
    json_obj_close(&b);
    expect("non-finite", &b,
           "{\"nan\":0,\"inf\":0,\"ninf\":0,\"arr\":[0,1.50]}");
    json_builder_free(&b);
}

static void test_growth(void)
{
    /* Start below the minimum capacity and write far past it. */
    json_builder_t b;
    json_builder_init(&b, 1);
    json_arr_open(&b, NULL);
    for (int i = 0; i < 2000; ++i) {
        json_elem_int(&b, i);
    }
    json_arr_close(&b);

    size_t len = 0;
    const char *s = json_builder_str(&b, &len);
    CHECK(!b.error, "growth: builder reported an error");
    CHECK(len == strlen(s), "growth: length %zu disagrees with strlen %zu",
          len, strlen(s));
    CHECK(s[0] == '[' && s[len - 1] == ']', "growth: not bracketed");
    CHECK(strstr(s, "[0,1,2,3,") == s, "growth: bad head: %.16s", s);
    CHECK(strstr(s, ",1998,1999]") != NULL, "growth: bad tail");
    CHECK(b.cap >= len + 1, "growth: capacity %zu below length %zu", b.cap, len);
    json_builder_free(&b);
}

static void test_error_state(void)
{
    /* A builder that never allocated must not be written through, and must
     * report an empty string rather than a NULL a caller would hand to strlen. */
    json_builder_t b;
    memset(&b, 0, sizeof(b));
    b.error = true;
    json_obj_open(&b);
    json_kv_str(&b, "key", "value");
    json_obj_close(&b);
    size_t len = 123;
    const char *s = json_builder_str(&b, &len);
    CHECK(s != NULL && s[0] == '\0', "error state: not an empty string");
    CHECK(len == 0, "error state: length %zu want 0", len);

    /* free() on a zeroed struct is a no-op, not a crash */
    json_builder_free(&b);
    json_builder_free(NULL);
    CHECK(json_builder_init(NULL, 64) == false, "init accepted NULL");
}

int main(void)
{
    test_object();
    test_nesting();
    test_escaping();
    test_non_finite();
    test_growth();
    test_error_state();
    printf("%s (%d failure%s)\n", fails ? "FAILED" : "all checks passed",
           fails, fails == 1 ? "" : "s");
    return fails != 0;
}
