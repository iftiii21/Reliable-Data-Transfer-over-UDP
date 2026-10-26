/**
 * rtt.h — Adaptive RTT Estimator using Jacobson/Karels algorithm
 *
 * Implements:
 *   - Smoothed RTT (SRTT) and RTT variance (RTTVAR) tracking
 *   - Retransmission Timeout (RTO) calculation: RTO = SRTT + K * RTTVAR
 *   - Karn's algorithm: retransmitted packet samples are excluded
 *   - Exponential backoff on timeout
 *
 * Constants (RFC 6298):
 *   ALPHA  = 1/8  (SRTT smoothing factor)
 *   BETA   = 1/4  (RTTVAR smoothing factor)
 *   K      = 4    (RTTVAR gain multiplier)
 *   RTO_MIN = 200 ms
 *   RTO_MAX = 60000 ms
 */

#ifndef RTT_H
#define RTT_H

/* All time values are in microseconds (us) */

#define RTT_ALPHA_NUM   1     /* numerator of ALPHA = 1/8 */
#define RTT_ALPHA_DEN   8     /* denominator */
#define RTT_BETA_NUM    1     /* numerator of BETA = 1/4 */
#define RTT_BETA_DEN    4     /* denominator */
#define RTT_K           4     /* RTTVAR gain */

#define RTO_MIN_US      200000L    /* 200 ms in microseconds */
#define RTO_MAX_US      60000000L  /* 60 s  in microseconds */
#define RTT_INITIAL_US  1000000L   /* 1 s initial RTO (RFC 6298 §2.1) */

/**
 * RttEstimator — holds state for Jacobson/Karels RTT estimation.
 *
 * Fields:
 *   srtt_us    — Smoothed RTT in microseconds
 *   rttvar_us  — RTT variance in microseconds
 *   rto_us     — Current Retransmission Timeout in microseconds
 *   initialized — 0 before first sample, 1 after
 */
typedef struct {
    long srtt_us;     /* Smoothed RTT (μs) */
    long rttvar_us;   /* RTT variance (μs) */
    long rto_us;      /* Current RTO (μs)  */
    int  initialized; /* 0 = first measurement not yet done */
} RttEstimator;

/**
 * rtt_init - Initialize the estimator with RFC-recommended defaults.
 *
 * Before the first sample arrives the RTO is set to RTT_INITIAL_US (1 s).
 */
void rtt_init(RttEstimator *rtt);

/**
 * rtt_update - Feed a new RTT sample and recompute SRTT / RTTVAR / RTO.
 *
 * Implements RFC 6298 §2.3:
 *   First sample:
 *     SRTT   = sample
 *     RTTVAR = sample / 2
 *   Subsequent samples:
 *     RTTVAR = (1 - β) * RTTVAR + β * |SRTT - sample|
 *     SRTT   = (1 - α) * SRTT   + α * sample
 *   Always:
 *     RTO = SRTT + K * RTTVAR   (clamped to [RTO_MIN, RTO_MAX])
 *
 * Per Karn's algorithm the caller MUST NOT call this function for
 * round-trips that involved a retransmission.  The selective-repeat
 * layer enforces this via the `is_retransmit` flag on each slot.
 *
 * @param rtt       Estimator state.
 * @param sample_us Measured round-trip time in microseconds (must be > 0).
 */
void rtt_update(RttEstimator *rtt, long sample_us);

/**
 * rtt_get_rto_us - Return the current RTO in microseconds.
 */
long rtt_get_rto_us(const RttEstimator *rtt);

/**
 * rtt_backoff - Exponential backoff: double the RTO, cap at RTO_MAX.
 *
 * Called whenever a retransmission timeout fires.  After backoff,
 * SRTT and RTTVAR are NOT updated (Karn's algorithm).
 */
void rtt_backoff(RttEstimator *rtt);

#endif /* RTT_H */
