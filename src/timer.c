/**
 * timer.c — Implementation of per-packet retransmission timers.
 *
 * All timing is done with clock_gettime(CLOCK_MONOTONIC) to avoid
 * wall-clock skew from NTP adjustments.
 */

#include "timer.h"
#include <string.h>  /* memset */

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/** Return current monotonic time as a struct timespec. */
static struct timespec now_monotonic(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts;
}

/**
 * Compute elapsed microseconds between two timespecs.
 * Returns (b - a) in microseconds.  Negative if a > b.
 */
static long timespec_diff_us(const struct timespec *a,
                              const struct timespec *b) {
    long sec_diff  = (long)(b->tv_sec  - a->tv_sec);
    long nsec_diff = (long)(b->tv_nsec - a->tv_nsec);
    return sec_diff * 1000000L + nsec_diff / 1000L;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void timer_init(RetransTimer *t) {
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->armed = 0;
}

void timer_arm(RetransTimer *t, long timeout_us) {
    if (!t || timeout_us <= 0) return;
    t->start      = now_monotonic();
    t->timeout_us = timeout_us;
    t->armed      = 1;
}

void timer_disarm(RetransTimer *t) {
    if (!t) return;
    t->armed = 0;
}

int timer_expired(const RetransTimer *t) {
    if (!t || !t->armed) return 0;

    struct timespec current = now_monotonic();
    long elapsed = timespec_diff_us(&t->start, &current);
    return (elapsed >= t->timeout_us) ? 1 : 0;
}

long timer_elapsed_us(const RetransTimer *t) {
    if (!t || !t->armed) return 0L;

    struct timespec current = now_monotonic();
    long elapsed = timespec_diff_us(&t->start, &current);
    return (elapsed > 0) ? elapsed : 0L;
}
