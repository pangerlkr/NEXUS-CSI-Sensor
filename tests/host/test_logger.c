/* Host test for the streaming log export. The interesting case is an entry
 * arriving while logger_to_json() is walking the ring: every index shifts, and
 * without the sequence guard the same entry comes out twice. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "logger.h"

int g_lock_fail = 0;

/* ---- host stubs the logger links against ------------------------------ */
static uint32_t g_now;
uint32_t utils_millis(void) { return g_now; }

esp_err_t storage_set_blob(const char *k, const void *d, size_t l)
{ (void)k; (void)d; (void)l; return ESP_OK; }
esp_err_t storage_get_blob(const char *k, void *o, size_t *l)
{ (void)k; (void)o; (void)l; return 0x105; /* not found */ }
size_t storage_blob_size(const char *k) { (void)k; return 0; }
esp_err_t storage_erase_key(const char *k) { (void)k; return ESP_OK; }

/* ---- a json_builder_t stand-in that records what was emitted ---------- */
#define MAX_SEEN 256
static uint32_t seen[MAX_SEEN];
static size_t   seen_n;
static int      inject_at = -1;   /* emit index at which to log a new entry */
static int      emit_i;

void json_arr_open(json_builder_t *b, const char *key) { (void)b; (void)key; }
void json_arr_close(json_builder_t *b) { (void)b; }
void json_elem_obj_open(json_builder_t *b) { (void)b; }
void json_obj_close(json_builder_t *b) { (void)b; }
void json_kv_str(json_builder_t *b, const char *key, const char *val)
{ (void)b; (void)key; (void)val; }

void json_kv_uint(json_builder_t *b, const char *key, unsigned long long val)
{
    (void)b;
    if (strcmp(key, "seq") != 0) return;
    if (seen_n < MAX_SEEN) seen[seen_n++] = (uint32_t)val;
    /* Simulate a concurrent logger_log() partway through the walk. */
    if (emit_i++ == inject_at) {
        g_now += 5;
        logger_log(LOG_LVL_EVENT, "arrived mid walk");
    }
}

/* ---- test ------------------------------------------------------------- */
static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { \
    printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void run(const char *label, int inject)
{
    seen_n = 0; emit_i = 0; inject_at = inject;
    json_builder_t b;
    memset(&b, 0, sizeof(b));
    logger_to_json(&b);

    /* newest first, strictly decreasing, no repeats */
    for (size_t i = 1; i < seen_n; ++i) {
        CHECK(seen[i] < seen[i - 1], "%s: seq %u after %u at %zu",
              label, seen[i], seen[i - 1], i);
    }
    for (size_t i = 0; i < seen_n; ++i) {
        for (size_t j = i + 1; j < seen_n; ++j) {
            CHECK(seen[i] != seen[j], "%s: seq %u emitted twice", label, seen[i]);
        }
    }
    printf("  %-22s emitted %zu entries, newest seq %u\n",
           label, seen_n, seen_n ? seen[0] : 0);
}

int main(void)
{
    if (logger_init() != ESP_OK) { printf("logger_init failed\n"); return 1; }

    run("empty ring", -1);

    for (int i = 0; i < 10; ++i) { g_now += 100; logger_log(LOG_LVL_INFO, "entry %d", i); }
    run("partial, no writer", -1);
    run("partial, writer at 3", 3);

    /* fill past capacity so the ring wraps, which is where indices shift */
    for (int i = 0; i < NEXUS_LOG_CAPACITY + 20; ++i) {
        g_now += 100; logger_log(LOG_LVL_INFO, "fill %d", i);
    }
    CHECK(logger_count() == NEXUS_LOG_CAPACITY, "count %zu want %d",
          logger_count(), NEXUS_LOG_CAPACITY);
    run("full, no writer", -1);
    run("full, writer at 0", 0);
    run("full, writer at 1", 1);
    run("full, writer mid", NEXUS_LOG_CAPACITY / 2);
    run("full, writer at end", NEXUS_LOG_CAPACITY - 1);

    /* the full ring with no writer must emit every entry, newest first */
    seen_n = 0; emit_i = 0; inject_at = -1;
    json_builder_t b; memset(&b, 0, sizeof(b));
    logger_to_json(&b);
    CHECK(seen_n == NEXUS_LOG_CAPACITY, "full walk emitted %zu want %d",
          seen_n, NEXUS_LOG_CAPACITY);

    /* peek must agree with the walk: index count-1 is the newest */
    log_entry_t newest;
    CHECK(logger_peek(logger_count() - 1, &newest), "peek newest refused");
    CHECK(newest.seq == seen[0], "peek newest seq %u, walk said %u",
          newest.seq, seen[0]);
    log_entry_t oldest;
    CHECK(logger_peek(0, &oldest), "peek oldest refused");
    CHECK(oldest.seq == seen[seen_n - 1], "peek oldest seq %u, walk said %u",
          oldest.seq, seen[seen_n - 1]);
    CHECK(!logger_peek(logger_count(), &oldest), "peek past end accepted");

    /* persist path: sizes itself to what is held and must not blow up empty */
    CHECK(logger_persist() == ESP_OK, "persist of a full ring failed");
    logger_clear();
    CHECK(logger_count() == 0, "clear left %zu entries", logger_count());
    CHECK(logger_persist() == ESP_OK, "persist of an empty ring failed");
    run("after clear", -1);

    printf("%s (%d failure%s)\n", fails ? "FAILED" : "all checks passed",
           fails, fails == 1 ? "" : "s");
    return fails != 0;
}
