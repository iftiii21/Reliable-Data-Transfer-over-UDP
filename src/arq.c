#include "arq.h"

#include <string.h>

static int valid_length(uint16_t length, const uint8_t *data)
{
    return length <= MAX_PAYLOAD_SIZE && (length == 0 || data != NULL);
}

void stop_wait_init(StopWaitState *state, uint32_t first_sequence,
                    uint32_t retry_limit)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
        state->sequence_number = first_sequence;
        state->retry_limit = retry_limit;
    }
}

int stop_wait_make_data(StopWaitState *state, Packet *packet,
                        const uint8_t *data, uint16_t length)
{
    if (state == NULL || packet == NULL || state->active ||
        !valid_length(length, data)) return -1;
    create_data_packet(packet, state->sequence_number,
                       (const char *)data, length);
    state->outstanding_packet = *packet;
    state->retry_count = 0;
    state->active = 1;
    return 0;
}

int stop_wait_on_timeout(StopWaitState *state, Packet *retransmission)
{
    if (state == NULL || retransmission == NULL) return -1;
    if (!state->active || state->retry_count >= state->retry_limit) return 0;
    *retransmission = state->outstanding_packet;
    state->retry_count++;
    return 1;
}

int stop_wait_on_ack(StopWaitState *state, const PacketHeader *ack)
{
    if (state == NULL || ack == NULL || ack->type != PACKET_ACK) return -1;
    if (!state->active || ack->acknowledgement_number != state->sequence_number + 1u) return 0;
    state->sequence_number++;
    state->active = 0;
    state->retry_count = 0;
    memset(&state->outstanding_packet, 0, sizeof(state->outstanding_packet));
    return 1;
}

int go_back_n_init(GoBackNState *state, uint32_t first_sequence,
                   uint16_t window_size, Packet *packet_storage,
                   size_t packet_capacity)
{
    if (state == NULL || window_size == 0 || packet_storage == NULL ||
        packet_capacity < window_size) return -1;
    memset(state, 0, sizeof(*state));
    state->base = first_sequence;
    state->next_sequence = first_sequence;
    state->window_size = window_size;
    state->outstanding_packets = packet_storage;
    memset(state->outstanding_packets, 0, window_size * sizeof(*packet_storage));
    return 0;
}

int go_back_n_can_send(const GoBackNState *state)
{
    return state != NULL && state->outstanding_packets != NULL &&
           (uint32_t)(state->next_sequence - state->base) < state->window_size;
}

int go_back_n_make_data(GoBackNState *state, Packet *packet,
                        const uint8_t *data, uint16_t length)
{
    uint32_t offset, i, outstanding;
    if (state == NULL || state->outstanding_packets == NULL || packet == NULL || !valid_length(length, data) ||
        !go_back_n_can_send(state)) return -1;
    outstanding = state->next_sequence - state->base;
    for (i = 0; i < outstanding; ++i) {
        if (packet == &state->outstanding_packets[i]) return -1;
    }
    offset = outstanding;
    create_data_packet(packet, state->next_sequence,
                       (const char *)data, length);
    state->outstanding_packets[offset] = *packet;
    state->next_sequence++;
    return 0;
}

int go_back_n_on_ack(GoBackNState *state, const PacketHeader *ack)
{
    uint32_t delta, outstanding;
    if (state == NULL || state->outstanding_packets == NULL || ack == NULL || ack->type != PACKET_ACK) return -1;
    outstanding = state->next_sequence - state->base;
    delta = ack->acknowledgement_number - state->base;
    if (delta == 0 || delta > outstanding) return 0;
    state->base = ack->acknowledgement_number;
    if (outstanding > delta) memmove(state->outstanding_packets,
                                     state->outstanding_packets + delta,
                                     (outstanding - delta) * sizeof(Packet));
    memset(state->outstanding_packets + outstanding - delta, 0,
           delta * sizeof(Packet));
    return (int)delta;
}

size_t go_back_n_on_timeout(GoBackNState *state, Packet *retransmissions,
                            size_t capacity)
{
    uint32_t count;
    if (state == NULL || state->outstanding_packets == NULL) return 0;
    count = state->next_sequence - state->base;
    if (count == 0 || retransmissions == NULL || capacity < count) return count;
    memmove(retransmissions, state->outstanding_packets, count * sizeof(Packet));
    return count;
}
