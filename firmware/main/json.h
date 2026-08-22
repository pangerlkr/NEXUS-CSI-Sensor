/**
 * @file json.h
 * @brief Minimal JSON serialisation builder and cJSON-backed parse helpers.
 *
 * Response bodies are produced with the streaming @ref json_builder_t (no
 * intermediate object tree, low heap pressure). Incoming request bodies are
 * parsed with the small typed accessors, which wrap the cJSON component that
 * ships with ESP-IDF. Keeping all JSON handling behind this module avoids
 * scattering cJSON calls throughout the codebase.
 */
#ifndef NEXUS_JSON_H
#define NEXUS_JSON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Builder                                                            */
/* ------------------------------------------------------------------ */

/** Growable JSON output buffer. Initialise with @ref json_builder_init. */
typedef struct {
    char  *buf;        /**< Heap buffer, NUL-terminated. */
    size_t len;        /**< Bytes used (excluding NUL). */
    size_t cap;        /**< Allocated capacity. */
    bool   error;      /**< Set on allocation failure. */
} json_builder_t;

/** Initialise a builder with an initial capacity hint (bytes). */
bool json_builder_init(json_builder_t *b, size_t initial_cap);

/** Free the builder's buffer. */
void json_builder_free(json_builder_t *b);

void json_obj_open(json_builder_t *b);
void json_obj_close(json_builder_t *b);
void json_kv_obj_open(json_builder_t *b, const char *key); /* writes `"key":{` */
void json_arr_open(json_builder_t *b, const char *key); /* key NULL => bare array */
void json_arr_close(json_builder_t *b);

void json_kv_str(json_builder_t *b, const char *key, const char *val);
void json_kv_int(json_builder_t *b, const char *key, long long val);
void json_kv_uint(json_builder_t *b, const char *key, unsigned long long val);
void json_kv_float(json_builder_t *b, const char *key, float val, int decimals);
void json_kv_bool(json_builder_t *b, const char *key, bool val);
void json_kv_raw(json_builder_t *b, const char *key, const char *raw_json);

/* Array element writers (no key). */
void json_elem_int(json_builder_t *b, long long val);
void json_elem_float(json_builder_t *b, float val, int decimals);
void json_elem_str(json_builder_t *b, const char *val);
void json_elem_obj_open(json_builder_t *b);

/** Return the current buffer (still owned by the builder). */
const char *json_builder_str(json_builder_t *b, size_t *out_len);

/* ------------------------------------------------------------------ */
/* Parse helpers (wrap cJSON)                                         */
/* ------------------------------------------------------------------ */

/** Opaque parsed document handle. */
typedef struct json_doc json_doc_t;

/** Parse @p text (length @p len). Returns NULL on failure. Free with json_free. */
json_doc_t *json_parse(const char *text, size_t len);
void        json_free(json_doc_t *doc);

bool json_get_str(const json_doc_t *doc, const char *key, char *out, size_t out_sz);
bool json_get_int(const json_doc_t *doc, const char *key, int *out);
bool json_get_float(const json_doc_t *doc, const char *key, float *out);
bool json_get_bool(const json_doc_t *doc, const char *key, bool *out);

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_JSON_H */
