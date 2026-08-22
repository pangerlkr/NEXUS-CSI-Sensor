/* Host test for ringbuffer.c: exercises peek and snapshot against a reference
 * model in the empty, partial, exactly-full and wrapped states. */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "ringbuffer.h"

int g_lock_fail = 0;

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { \
    printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* Reference model: the last CAP values pushed, oldest first. */
#define CAP 5
static int ref[CAP];
static size_t ref_n;
static void ref_push(int v) {
    if (ref_n < CAP) { ref[ref_n++] = v; }
    else { memmove(ref, ref + 1, (CAP - 1) * sizeof(int)); ref[CAP - 1] = v; }
}

static void compare(ringbuffer_t *rb, const char *where)
{
    CHECK(ringbuffer_count(rb) == ref_n, "%s: count %zu want %zu",
          where, ringbuffer_count(rb), ref_n);

    /* peek, element by element */
    for (size_t i = 0; i < ref_n; ++i) {
        int got = -999;
        CHECK(ringbuffer_peek(rb, i, &got), "%s: peek(%zu) refused", where, i);
        CHECK(got == ref[i], "%s: peek(%zu) = %d want %d", where, i, got, ref[i]);
    }
    /* one past the end must be refused, and must not touch the destination */
    int sentinel = 12345;
    CHECK(!ringbuffer_peek(rb, ref_n, &sentinel), "%s: peek past end accepted", where);
    CHECK(sentinel == 12345, "%s: peek past end wrote to out", where);

    /* peek must agree with snapshot, which is the API it was modelled on */
    int snap[CAP + 3];
    for (size_t i = 0; i < CAP + 3; ++i) snap[i] = -1;
    size_t got_n = ringbuffer_snapshot(rb, snap, CAP + 3);
    CHECK(got_n == ref_n, "%s: snapshot n %zu want %zu", where, got_n, ref_n);
    for (size_t i = 0; i < got_n; ++i) {
        CHECK(snap[i] == ref[i], "%s: snapshot[%zu] = %d want %d",
              where, i, snap[i], ref[i]);
    }
}

int main(void)
{
    ringbuffer_t rb;
    assert(ringbuffer_init(&rb, sizeof(int), CAP) == ESP_OK);

    compare(&rb, "empty");

    /* fill part way, then exactly full, then wrap several times */
    for (int v = 1; v <= 13; ++v) {
        bool overwrote = ringbuffer_push(&rb, &v);
        ref_push(v);
        CHECK(overwrote == (v > CAP), "push %d reported overwrote=%d", v, overwrote);
        char where[32];
        snprintf(where, sizeof(where), "after push %d", v);
        compare(&rb, where);
    }

    /* the mutex must be released on every path, including the refused peek */
    CHECK(rb.lock->held == 0, "mutex still held: %d", rb.lock->held);

    /* a peek that cannot get the lock reports failure and writes nothing */
    int before = 777;
    g_lock_fail = 1;
    CHECK(!ringbuffer_peek(&rb, 0, &before), "peek succeeded despite lock failure");
    CHECK(before == 777, "peek wrote to out after lock failure");

    /* push_timeout reports stored, not overwrote, which is the opposite of push */
    int v = 99;
    CHECK(ringbuffer_push_timeout(&rb, &v, 2) == true, "push_timeout said not stored");
    ref_push(v);
    compare(&rb, "after push_timeout");
    g_lock_fail = 1;
    CHECK(ringbuffer_push_timeout(&rb, &v, 2) == false, "push_timeout ignored timeout");
    compare(&rb, "after dropped push_timeout");

    /* clear empties it without freeing */
    ringbuffer_clear(&rb);
    ref_n = 0;
    compare(&rb, "after clear");

    /* argument guards */
    CHECK(!ringbuffer_peek(NULL, 0, &v), "peek accepted NULL rb");
    CHECK(!ringbuffer_peek(&rb, 0, NULL), "peek accepted NULL out");

    CHECK(rb.lock->held == 0, "mutex left held at exit: %d", rb.lock->held);
    printf("%s (%d failure%s)\n", fails ? "FAILED" : "all checks passed",
           fails, fails == 1 ? "" : "s");
    return fails != 0;
}
