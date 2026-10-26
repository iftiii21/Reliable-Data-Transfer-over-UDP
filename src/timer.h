/**
 * timer.h — Per-packet retransmission timers using CLOCK_MONOTONIC
 *
 * Each RetransTimer tracks whether it is armed, when it was armed, and
 * what its timeout duration is.  The caller checks timer_expired() in
 * the main poll loop and acts accordingly.
 *
 * Uses clock_gettime(CLOCK_MONOTONIC) for monotonic, NTP-immune timing.
 */

#ifndef TIMER_H
#define TIMER_H

#include <time.h>   /* struct timespec, clock_gettime */

/**
 * RetransTimer — state for a single per-packet retransmission timer.
 *
 * Fields:
 *   start       — Monotonic timestamp when the timer was last armed.
 *   timeout_us  — Duration of the current arm in microseconds.
 *   armed       — 1 if the timer is running, 0 if disarmed/never started.
 */
typedef struct {
    struct timespec start;      /* time when armed */
    long            timeout_us; /* timeout duration in microseconds */
    int             armed;      /* 1 = running, 0 = idle */
} RetransTimer;

/**
 * timer_init - Zero-initialise a timer (disarmed state).
 */
void timer_init(RetransTimer *t);

/**
 * timer_arm - Start (or restart) the timer with the given timeout.
 *
 * Records the current monotonic time and sets the armed flag.
 *
 * @param t          Timer to arm.
 * @param timeout_us Timeout in microseconds (must be > 0).
 */
void timer_arm(RetransTimer *t, long timeout_us);

/**
 * timer_disarm - Cancel the timer.
 *
 * Clears the armed flag; subsequent timer_expired() calls return 0.
 */
void timer_disarm(RetransTimer *t);

/**
 * timer_expired - Test whether the timer has fired.
 *
 * @return 1 if the timer is armed and the timeout has elapsed, 0 otherwise.
 */
int timer_expired(const RetransTimer *t);

/**
 * timer_elapsed_us - Microseconds elapsed since the timer was armed.
 *
 * Returns 0 if the timer is not armed.
 */
long timer_elapsed_us(const RetransTimer *t);

#endif /* TIMER_H */
