/**
 * @file cJSON.h
 * @brief Link-time shim so json.c compiles on the host. Not a JSON parser.
 *
 * The host tests exercise the **builder** half of `json.c`: escaping, float
 * formatting, comma placement and buffer growth. The parse half wraps cJSON,
 * which is an ESP-IDF component and is not vendored here, so these declarations
 * exist only to satisfy the compiler and linker.
 *
 * Everything below fails or returns nothing. That means `json_parse()` and the
 * `json_get_*()` family cannot be tested on the host, and the tests do not
 * pretend to: they never call them. If you are changing those functions, the
 * only real check is a device build.
 */
#ifndef NEXUS_TEST_CJSON_SHIM_H
#define NEXUS_TEST_CJSON_SHIM_H

#include <stddef.h>

typedef int cJSON_bool;

typedef struct cJSON {
    char  *valuestring;
    int    valueint;
    double valuedouble;
} cJSON;

static inline cJSON *cJSON_ParseWithLength(const char *value, size_t len)
{
    (void)value; (void)len;
    return NULL;   /* the host tests never parse */
}

static inline void cJSON_Delete(cJSON *item) { (void)item; }

static inline cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *const object,
                                                      const char *const string)
{
    (void)object; (void)string;
    return NULL;
}

static inline cJSON_bool cJSON_IsString(const cJSON *const item) { (void)item; return 0; }
static inline cJSON_bool cJSON_IsNumber(const cJSON *const item) { (void)item; return 0; }
static inline cJSON_bool cJSON_IsBool(const cJSON *const item)   { (void)item; return 0; }
static inline cJSON_bool cJSON_IsTrue(const cJSON *const item)   { (void)item; return 0; }

#endif /* NEXUS_TEST_CJSON_SHIM_H */
