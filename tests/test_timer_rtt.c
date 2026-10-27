#define _POSIX_C_SOURCE 200809L

#include "rtt.h"
#include "timer.h"

#include <assert.h>
#include <stdio.h>

static void test_rtt_estimator(void)
{
    RttEstimator rtt;

    rtt_init(&rtt);
    assert(rtt_get_rto_us(&rtt) == RTT_INITIAL_US);

    rtt_update(&rtt, 100000L);
    assert(rtt.initialized);
    assert(rtt.srtt_us == 100000L);
    assert(rtt.rttvar_us == 50000L);
    assert(rtt_get_rto_us(&rtt) == 300000L);

    rtt_update(&rtt, 200000L);
    assert(rtt.srtt_us == 112500L);
    assert(rtt.rttvar_us == 62500L);
    assert(rtt_get_rto_us(&rtt) == 362500L);

    rtt_backoff(&rtt);
    assert(rtt_get_rto_us(&rtt) == 725000L);
    rtt.rto_us = RTO_MAX_US;
    rtt_backoff(&rtt);
    assert(rtt_get_rto_us(&rtt) == RTO_MAX_US);
}

static void test_timer(void)
{
    RetransTimer timer;

    timer_init(&timer);
    assert(!timer_expired(&timer));
    assert(timer_elapsed_us(&timer) == 0L);

    timer_arm(&timer, 1000L);
    assert(!timer_expired(&timer));
    while (!timer_expired(&timer)) {
        /* Wait for the short monotonic timer to expire. */
    }
    assert(timer_elapsed_us(&timer) >= 1000L);

    timer_disarm(&timer);
    assert(!timer_expired(&timer));
    assert(timer_elapsed_us(&timer) == 0L);
}

int main(void)
{
    test_rtt_estimator();
    test_timer();
    puts("Timer/RTT tests passed");
    return 0;
}
