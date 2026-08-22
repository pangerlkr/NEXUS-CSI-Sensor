/**
 * @file json.c
 * @brief JSON builder implementation and cJSON parse wrappers.
 */
#include "json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>

#include "cJSON.h"

/* ================================================================== */
/* Builder                                                            */
/* ================================================================== */

#define JSON_MIN_CAP 64

bool json_builder_init(json_builder_t *b, size_t initial_cap)
{
    if (b == NULL) {
        return false;
    }
    if (initial_cap < JSON_MIN_CAP) {
        initial_cap = JSON_MIN_CAP;
    }
    b->buf = (char *)malloc(initial_cap);
    if (b->buf == NULL) {
        b->cap = 0;
        b->len = 0;
        b->error = true;
        return false;
    }
    b->buf[0] = '\0';
    b->cap = initial_cap;
    b->len = 0;
    b->error = false;
    return true;
}

void json_builder_free(json_builder_t *b)
{
    if (b == NULL) {
        return;
    }
    free(b->buf);
    b->buf = NULL;
    b->cap = 0;
    b->len = 0;
}

/** Ensure at least @p extra additional bytes (plus NUL) fit. */
static bool json_reserve(json_builder_t *b, size_t extra)
{
    if (b->error) {
        return false;
    }
    size_t need = b->len + extra + 1;
    if (need <= b->cap) {
        return true;
    }
    size_t new_cap = b->cap ? b->cap : JSON_MIN_CAP;
    while (new_cap < need) {
        /* Stop before the doubling wraps, otherwise this loop never ends. */
        if (new_cap > SIZE_MAX / 2) {
            new_cap = need;
            break;
        }
        new_cap *= 2;
    }
    char *nb = (char *)realloc(b->buf, new_cap);
    if (nb == NULL) {
        b->error = true;
        return false;
    }
    b->buf = nb;
    b->cap = new_cap;
    return true;
}

static void json_append(json_builder_t *b, const char *s, size_t n)
{
    if (!json_reserve(b, n)) {
        return;
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
}

static void json_append_cstr(json_builder_t *b, const char *s)
{
    json_append(b, s, strlen(s));
}

/* Append a comma before the next item if the previous char requires it. */
static void json_maybe_comma(json_builder_t *b)
{
    if (b->len == 0) {
        return;
    }
    char last = b->buf[b->len - 1];
    if (last != '{' && last != '[' && last != ':' && last != ',') {
        json_append(b, ",", 1);
    }
}

/* Append a JSON-escaped string literal (with surrounding quotes). */
static void json_append_escaped(json_builder_t *b, const char *s)
{
    json_append(b, "\"", 1);
    if (s == NULL) {
        json_append(b, "\"", 1);
        return;
    }
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        unsigned char c = *p;
        switch (c) {
            case '\"': json_append(b, "\\\"", 2); break;
            case '\\': json_append(b, "\\\\", 2); break;
            case '\n': json_append(b, "\\n", 2); break;
            case '\r': json_append(b, "\\r", 2); break;
            case '\t': json_append(b, "\\t", 2); break;
            case '\b': json_append(b, "\\b", 2); break;
            case '\f': json_append(b, "\\f", 2); break;
            default:
                if (c < 0x20) {
                    char u[7];
                    snprintf(u, sizeof(u), "\\u%04x", c);
                    json_append_cstr(b, u);
                } else {
                    json_append(b, (const char *)&c, 1);
                }
                break;
        }
    }
    json_append(b, "\"", 1);
}

static void json_append_key(json_builder_t *b, const char *key)
{
    json_maybe_comma(b);
    json_append_escaped(b, key);
    json_append(b, ":", 1);
}

void json_obj_open(json_builder_t *b)  { json_maybe_comma(b); json_append(b, "{", 1); }
void json_obj_close(json_builder_t *b) { json_append(b, "}", 1); }

void json_kv_obj_open(json_builder_t *b, const char *key)
{
    json_append_key(b, key);
    json_append(b, "{", 1);
}

void json_arr_open(json_builder_t *b, const char *key)
{
    if (key) {
        json_append_key(b, key);
    } else {
        json_maybe_comma(b);
    }
    json_append(b, "[", 1);
}
void json_arr_close(json_builder_t *b) { json_append(b, "]", 1); }

void json_kv_str(json_builder_t *b, const char *key, const char *val)
{
    json_append_key(b, key);
    json_append_escaped(b, val);
}

void json_kv_int(json_builder_t *b, const char *key, long long val)
{
    char tmp[24];
    snprintf(tmp, sizeof(tmp), "%lld", val);
    json_append_key(b, key);
    json_append_cstr(b, tmp);
}

void json_kv_uint(json_builder_t *b, const char *key, unsigned long long val)
{
    char tmp[24];
    snprintf(tmp, sizeof(tmp), "%llu", val);
    json_append_key(b, key);
    json_append_cstr(b, tmp);
}

/* Format a float for JSON output.
 *
 * "%f" prints NaN and infinity as "nan" and "inf", which are not valid JSON
 * numbers. A single one of those anywhere in the document makes the whole
 * response unparseable, so the dashboard would stop updating entirely rather
 * than showing one wrong value. Substitute 0 instead. */
static void json_fmt_float(char *tmp, size_t tmp_len, float val, int decimals)
{
    if (!isfinite((double)val)) {
        snprintf(tmp, tmp_len, "0");
        return;
    }
    snprintf(tmp, tmp_len, "%.*f", decimals, (double)val);
}

void json_kv_float(json_builder_t *b, const char *key, float val, int decimals)
{
    char tmp[32];
    json_fmt_float(tmp, sizeof(tmp), val, decimals);
    json_append_key(b, key);
    json_append_cstr(b, tmp);
}

void json_kv_bool(json_builder_t *b, const char *key, bool val)
{
    json_append_key(b, key);
    json_append_cstr(b, val ? "true" : "false");
}

void json_kv_raw(json_builder_t *b, const char *key, const char *raw_json)
{
    json_append_key(b, key);
    json_append_cstr(b, raw_json ? raw_json : "null");
}

void json_elem_int(json_builder_t *b, long long val)
{
    char tmp[24];
    snprintf(tmp, sizeof(tmp), "%lld", val);
    json_maybe_comma(b);
    json_append_cstr(b, tmp);
}

void json_elem_float(json_builder_t *b, float val, int decimals)
{
    char tmp[32];
    json_fmt_float(tmp, sizeof(tmp), val, decimals);
    json_maybe_comma(b);
    json_append_cstr(b, tmp);
}

void json_elem_str(json_builder_t *b, const char *val)
{
    json_maybe_comma(b);
    json_append_escaped(b, val);
}

void json_elem_obj_open(json_builder_t *b)
{
    json_maybe_comma(b);
    json_append(b, "{", 1);
}

const char *json_builder_str(json_builder_t *b, size_t *out_len)
{
    if (b->error) {
        if (out_len) *out_len = 0;
        return "";
    }
    if (out_len) {
        *out_len = b->len;
    }
    return b->buf ? b->buf : "";
}

/* ================================================================== */
/* Parse (cJSON)                                                      */
/* ================================================================== */

struct json_doc {
    cJSON *root;
};

json_doc_t *json_parse(const char *text, size_t len)
{
    if (text == NULL || len == 0) {
        return NULL;
    }
    cJSON *root = cJSON_ParseWithLength(text, len);
    if (root == NULL) {
        return NULL;
    }
    json_doc_t *doc = (json_doc_t *)malloc(sizeof(json_doc_t));
    if (doc == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    doc->root = root;
    return doc;
}

void json_free(json_doc_t *doc)
{
    if (doc == NULL) {
        return;
    }
    cJSON_Delete(doc->root);
    free(doc);
}

bool json_get_str(const json_doc_t *doc, const char *key, char *out, size_t out_sz)
{
    if (doc == NULL || out == NULL || out_sz == 0) {
        return false;
    }
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(doc->root, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    size_t i = 0;
    for (; i + 1 < out_sz && item->valuestring[i]; ++i) {
        out[i] = item->valuestring[i];
    }
    out[i] = '\0';
    return true;
}

bool json_get_int(const json_doc_t *doc, const char *key, int *out)
{
    if (doc == NULL || out == NULL) {
        return false;
    }
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(doc->root, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    /* Read valuedouble, not valueint: cJSON saturates valueint at INT_MAX and
     * truncates anything fractional, so a caller range-checking the result would
     * be checking a value the client never sent. Reject what will not fit
     * instead of silently clamping it. */
    double v = item->valuedouble;
    if (!isfinite(v) || v > (double)INT_MAX || v < (double)INT_MIN) {
        return false;
    }
    *out = (int)v;
    return true;
}

bool json_get_float(const json_doc_t *doc, const char *key, float *out)
{
    if (doc == NULL || out == NULL) {
        return false;
    }
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(doc->root, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    *out = (float)item->valuedouble;
    return true;
}

bool json_get_bool(const json_doc_t *doc, const char *key, bool *out)
{
    if (doc == NULL || out == NULL) {
        return false;
    }
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(doc->root, key);
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item);
        return true;
    }
    if (cJSON_IsNumber(item)) {
        *out = item->valueint != 0;
        return true;
    }
    return false;
}
