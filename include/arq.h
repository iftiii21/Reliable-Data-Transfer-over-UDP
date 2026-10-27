#ifndef ARQ_H
#define ARQ_H

#include <stddef.h>
#include <stdint.h>
#include "packet.h"

/* State machines use host-order structures. Before passing a received ACK,
 * the caller MUST validate its serialized length, checksum, magic and version.
 * ACK handlers then validate non-NULL arguments and PACKET_ACK type. */
typedef struct {
    uint32_t sequence_number;
    uint8_t active;
    uint32_t retry_count;
    uint32_t retry_limit;
    Packet outstanding_packet;
} StopWaitState;

void stop_wait_init(StopWaitState *state, uint32_t first_sequence,
                    uint32_t retry_limit);
int stop_wait_make_data(StopWaitState *state, Packet *packet,
                        const uint8_t *data, uint16_t length);
/* Copy the retained packet for retransmission; 1=retry, 0=no active packet or
 * retries exhausted, -1=invalid argument. This is not network transmission. */
int stop_wait_on_timeout(StopWaitState *state, Packet *retransmission);
int stop_wait_on_ack(StopWaitState *state, const PacketHeader *ack);

typedef struct {
    uint32_t base;
    uint32_t next_sequence;
    uint16_t window_size;
    Packet *outstanding_packets;
} GoBackNState;

int go_back_n_init(GoBackNState *state, uint32_t first_sequence,
                   uint16_t window_size, Packet *packet_storage,
                   size_t packet_capacity);
int go_back_n_can_send(const GoBackNState *state);
/* packet must not point to a currently outstanding entry in packet_storage. */
int go_back_n_make_data(GoBackNState *state, Packet *packet,
                        const uint8_t *data, uint16_t length);
/* Cumulative ACK is the next sequence expected; returns newly acked count,
 * or -1 for invalid input. */
int go_back_n_on_ack(GoBackNState *state, const PacketHeader *ack);
/* Copy all outstanding packets in sequence order. Returns required count;
 * no copies are made if capacity is insufficient. */
size_t go_back_n_on_timeout(GoBackNState *state, Packet *retransmissions,
                            size_t capacity);

#endif
