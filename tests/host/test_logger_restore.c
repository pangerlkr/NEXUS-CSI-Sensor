/* Host test for the restore half of logger_init(), which reads persisted entries
 * back out of NVS at boot.
 *
 * This is finding 3 in docs/CODE_REVIEW.md, and it is worth a test of its own
 * because the bug was invisible: nvs_get_blob() treats the length argument as the
 * capacity of the caller's buffer on the way in, so passing the *stored* size
 * while having allocated only NEXUS_LOG_CAPACITY entries let NVS write past the
 * end of the allocation. Nothing about the call site looked wrong.
 *
 * The storage_get_blob() stub below therefore models nvs_get_blob() faithfully
 * rather than conveniently, so the shape of the bug is reachable from here.
 *
 * Each scenario calls logger_init() again. The module has no deinit, so the
 * previous ring and mutex leak. That is deliberate and harmless in a test
 * process; ringbuffer_init() memsets and reallocates, so every scenario starts
 * from an empty ring.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "logger.h"

int g_lock_fail = 0;

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { \
    printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ---- host stubs -------------------------------------------------------- */

static uint32_t g_now;
uint32_t utils_millis(void) { return g_now; }

esp_err_t storage_set_blob(const char *k, const void *d, size_t l)
{ (void)k; (void)d; (void)l; return ESP_OK; }
esp_err_t storage_erase_key(const char *k) { (void)k; return ESP_OK; }

/* The pretend NVS contents for the scenario being run. */
static uint8_t  g_blob[(NEXUS_LOG_CAPACITY + 24) * sizeof(log_entry_t)];
static size_t   g_blob_len;      /* 0 means the key is absent */
static size_t   g_asked_cap;     /* capacity the caller declared, in bytes */
static int      g_get_calls;

/* Anything above this is a buffer the logger cannot have allocated. */
#define SANE_CAP (NEXUS_LOG_CAPACITY * sizeof(log_entry_t))

size_t storage_blob_size(const char *k)
{
    (void)k;
    return g_blob_len;
}

esp_err_t storage_get_blob(const char *k, void *out, size_t *len)
{
    (void)k;
    g_get_calls++;
    g_asked_cap = *len;

    if (g_blob_len == 0) {
        return 0x105;                    /* ESP_ERR_NVS_NOT_FOUND */
    }
    /* The regression check. A faithful stub would now write g_blob_len bytes
     * into a buffer the caller only sized for NEXUS_LOG_CAPACITY entries, which
     * is exactly the original bug. Report it and refuse instead of corrupting
     * the heap, so the failure is a readable line rather than a crash in
     * whatever allocation happened to sit next door. */
    if (*len > SANE_CAP) {
        printf("FAIL: get_blob asked for %zu bytes, more than the %zu the ring "
               "can hold. This is finding 3 back again.\n", *len, SANE_CAP);
        fails++;
        return 0x105;
    }
    if (*len < g_blob_len) {
        return 0x1070;                   /* ESP_ERR_NVS_INVALID_LENGTH */
    }
    memcpy(out, g_blob, g_blob_len);
    *len = g_blob_len;
    return ESP_OK;
}

/* ---- scenario helpers -------------------------------------------------- */

/* Stage `n` entries with sequence numbers base .. base+n-1. */
static void stage(size_t n, uint32_t base)
{
    g_blob_len = n * sizeof(log_entry_t);
    memset(g_blob, 0, sizeof(g_blob));
    for (size_t i = 0; i < n; ++i) {
        log_entry_t e;
        memset(&e, 0, sizeof(e));
        e.seq = base + (uint32_t)i;
        e.uptime_ms = 1000 + (uint32_t)i;
        e.level = (uint8_t)LOG_LVL_EVENT;
        snprintf(e.msg, sizeof(e.msg), "stored %u", (unsigned)e.seq);
        memcpy(g_blob + i * sizeof(log_entry_t), &e, sizeof(e));
    }
}

/* Re-run init and report what came back. `want_seq` is the sequence number the
 * next logged entry must carry, which is how the test observes that s_seq was
 * advanced past the restored entries. */
static void scenario(const char *label, size_t want_count, uint32_t want_seq)
{
    g_asked_cap = 0;
    g_get_calls = 0;
    CHECK(logger_init() == ESP_OK, "%s: logger_init failed", label);
    CHECK(logger_count() == want_count, "%s: restored %zu entries, want %zu",
          label, logger_count(), want_count);
    CHECK(g_asked_cap <= SANE_CAP, "%s: declared capacity %zu over the %zu limit",
          label, g_asked_cap, SANE_CAP);

    /* Whatever happened, the logger must be usable afterwards. */
    g_now += 10;
    logger_log(LOG_LVL_INFO, "after %s", label);
    log_entry_t newest;
    CHECK(logger_peek(logger_count() - 1, &newest), "%s: peek newest refused", label);
    CHECK(newest.seq == want_seq, "%s: next seq %u, want %u",
          label, newest.seq, want_seq);
    CHECK(strncmp(newest.msg, "after ", 6) == 0,
          "%s: newest entry is '%s', not the one just logged", label, newest.msg);

    printf("  %-26s restored %zu, asked for %zu bytes, next seq %u\n",
           label, want_count, g_asked_cap, newest.seq);
}

int main(void)
{
    /* Nothing persisted: the common first boot. get_blob must not even be
     * called, because storage_blob_size() already said there is nothing. */
    g_blob_len = 0;
    scenario("nothing stored", 0, 0);
    CHECK(g_get_calls == 0, "empty key still triggered a get_blob");

    /* A partial blob restores whole and pushes the sequence past it. */
    stage(10, 100);
    scenario("10 entries stored", 10, 110);

    /* Exactly full. */
    stage(NEXUS_LOG_CAPACITY, 200);
    scenario("exactly capacity", NEXUS_LOG_CAPACITY, 200 + NEXUS_LOG_CAPACITY);

    /* More than fits. The count is clamped, so the declared capacity is smaller
     * than the stored blob and NVS refuses the read: the oversized blob is
     * dropped rather than partly restored. Dropping it is the point. The bug this
     * replaces would have accepted the read and written past the allocation. */
    uint32_t seq_before = 200 + NEXUS_LOG_CAPACITY + 1;  /* set by the last scenario */
    stage(NEXUS_LOG_CAPACITY + 24, 900);
    scenario("more than capacity", 0, seq_before);
    CHECK(g_get_calls == 1, "oversized blob: get_blob called %d times", g_get_calls);

    /* A blob that is not a whole number of entries is a corrupt or
     * older-layout blob. The size check rejects it before any read. */
    stage(5, 700);
    g_blob_len -= 3;
    scenario("ragged blob", 0, seq_before + 1);
    CHECK(g_get_calls == 0, "ragged blob still triggered a get_blob");

    printf("%s (%d failure%s)\n", fails ? "FAILED" : "all checks passed",
           fails, fails == 1 ? "" : "s");
    return fails != 0;
}
