/**
 * stats.h — Transfer statistics tracking
 *
 * Collects per-transfer metrics:
 *   - Total packets sent (including retransmissions)
 *   - Total retransmissions
 *   - Total payload bytes transferred
 *   - Wall-clock start and end times
 *
 * Call stats_print() at the end of a transfer to dump a summary.
 */

#ifndef STATS_H
#define STATS_H

#include <time.h>     /* struct timespec */
#include <stdint.h>   /* uint64_t */

/**
 * TransferStats — accumulated statistics for one file transfer.
 */
typedef struct {
    uint64_t        total_packets_sent;    /* includes retransmissions */
    uint64_t        total_retransmissions; /* retransmits only */
    uint64_t        total_bytes;           /* payload bytes sent/received */
    struct timespec start_time;            /* CLOCK_MONOTONIC start */
    struct timespec end_time;              /* CLOCK_MONOTONIC end   */
} TransferStats;

/**
 * stats_init - Zero-initialise stats and record start time.
 */
void stats_init(TransferStats *s);

/**
 * stats_record_send - Record a packet send (not a retransmit).
 *
 * @param payload_bytes Number of payload bytes in this packet.
 */
void stats_record_send(TransferStats *s, uint32_t payload_bytes);

/**
 * stats_record_retransmit - Record a retransmission event.
 *
 * Also increments total_packets_sent.
 *
 * @param payload_bytes Number of payload bytes in the retransmitted packet.
 */
void stats_record_retransmit(TransferStats *s, uint32_t payload_bytes);

/**
 * stats_finish - Record end time.  Call before stats_print().
 */
void stats_finish(TransferStats *s);

/**
 * stats_print - Print a human-readable statistics summary to stdout.
 */
void stats_print(const TransferStats *s);

#endif /* STATS_H */
