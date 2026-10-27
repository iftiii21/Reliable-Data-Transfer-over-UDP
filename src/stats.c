/**
 * stats.c — Implementation of transfer statistics tracking.
 */

#include "stats.h"
#include <stdio.h>
#include <string.h>  /* memset */

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static long timespec_diff_ms(const struct timespec *a,
                              const struct timespec *b) {
    long sec_diff  = (long)(b->tv_sec  - a->tv_sec);
    long nsec_diff = (long)(b->tv_nsec - a->tv_nsec);
    return sec_diff * 1000L + nsec_diff / 1000000L;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void stats_init(TransferStats *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    clock_gettime(CLOCK_MONOTONIC, &s->start_time);
}

void stats_record_send(TransferStats *s, uint32_t payload_bytes) {
    if (!s) return;
    s->total_packets_sent++;
    s->total_bytes += payload_bytes;
}

void stats_record_retransmit(TransferStats *s, uint32_t payload_bytes) {
    if (!s) return;
    s->total_packets_sent++;
    s->total_retransmissions++;
    s->total_bytes += payload_bytes;
}

void stats_finish(TransferStats *s) {
    if (!s) return;
    clock_gettime(CLOCK_MONOTONIC, &s->end_time);
}

void stats_print(const TransferStats *s) {
    if (!s) return;

    long elapsed_ms = timespec_diff_ms(&s->start_time, &s->end_time);
    if (elapsed_ms < 1) elapsed_ms = 1; /* avoid division by zero */

    double throughput_kbps = ((double)s->total_bytes * 8.0) /
                             ((double)elapsed_ms / 1000.0) / 1024.0;

    printf("\n===== Transfer Statistics =====\n");
    printf("  Total packets sent    : %llu\n",
           (unsigned long long)s->total_packets_sent);
    printf("  Retransmissions       : %llu\n",
           (unsigned long long)s->total_retransmissions);
    printf("  Total bytes (payload) : %llu\n",
           (unsigned long long)s->total_bytes);
    printf("  Elapsed time          : %ld ms\n", elapsed_ms);
    printf("  Throughput            : %.2f kbps\n", throughput_kbps);
    if (s->total_packets_sent > 0) {
        double retx_pct = (double)s->total_retransmissions /
                          (double)s->total_packets_sent * 100.0;
        printf("  Retransmit rate       : %.2f%%\n", retx_pct);
    }
    printf("================================\n\n");
}
