/**
 * rtt.c — Implementation of Jacobson/Karels RTT estimation + Karn's algorithm
 *
 * See rtt.h for algorithm details and references.
 */

#include "rtt.h"
#include <stdlib.h>  /* labs() */

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

/** Clamp a long value to [lo, hi]. */
static long clamp_long(long v, long lo, long hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void rtt_init(RttEstimator *rtt) {
    if (!rtt) return;

    rtt->srtt_us    = 0L;
    rtt->rttvar_us  = 0L;
    rtt->rto_us     = RTT_INITIAL_US; /* RFC 6298 §2.1: start at 1 s */
    rtt->initialized = 0;
}

void rtt_update(RttEstimator *rtt, long sample_us) {
    if (!rtt || sample_us <= 0) return;

    if (!rtt->initialized) {
        /*
         * RFC 6298 §2.2 — first measurement:
         *   SRTT   = R
         *   RTTVAR = R / 2
         *   RTO    = SRTT + K * RTTVAR
         */
        rtt->srtt_us   = sample_us;
        rtt->rttvar_us = sample_us / 2;
        rtt->initialized = 1;
    } else {
        /*
         * RFC 6298 §2.3 — subsequent measurements:
         *
         * Using integer arithmetic to avoid floating-point dependency.
         *
         *   RTTVAR = (1 - β) * RTTVAR + β * |SRTT - R|
         *          = RTTVAR - RTTVAR/8 + |SRTT - R|/4
         *
         *   SRTT   = (1 - α) * SRTT + α * R
         *          = SRTT - SRTT/8 + R/8
         *
         * Note: RTTVAR must be updated BEFORE SRTT (RFC 6298 §2.3 step order).
         */
        long diff = labs(rtt->srtt_us - sample_us);

        /* RTTVAR update (β = 1/4) */
        rtt->rttvar_us = rtt->rttvar_us
                         - rtt->rttvar_us / RTT_BETA_DEN
                         + diff          / RTT_BETA_DEN;

        /* SRTT update (α = 1/8) */
        rtt->srtt_us = rtt->srtt_us
                       - rtt->srtt_us  / RTT_ALPHA_DEN
                       + sample_us     / RTT_ALPHA_DEN;
    }

    /* RTO = SRTT + K * RTTVAR, clamped */
    long rto = rtt->srtt_us + RTT_K * rtt->rttvar_us;
    rtt->rto_us = clamp_long(rto, RTO_MIN_US, RTO_MAX_US);
}

long rtt_get_rto_us(const RttEstimator *rtt) {
    if (!rtt) return RTT_INITIAL_US;
    return rtt->rto_us;
}

void rtt_backoff(RttEstimator *rtt) {
    if (!rtt) return;

    /* Exponential backoff: RTO *= 2, cap at RTO_MAX */
    rtt->rto_us = clamp_long(rtt->rto_us * 2, RTO_MIN_US, RTO_MAX_US);
}
