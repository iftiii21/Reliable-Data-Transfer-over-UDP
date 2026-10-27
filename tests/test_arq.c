#include "arq.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_stop_wait(void)
{
    StopWaitState s; Packet p, retry; PacketHeader a = {0};
    const uint8_t b[] = {1, 2, 3};
    uint8_t max_payload[MAX_PAYLOAD_SIZE];
    stop_wait_init(&s, 7, 2);
    assert(stop_wait_make_data(NULL, &p, b, sizeof b) == -1);
    assert(stop_wait_make_data(&s, NULL, b, sizeof b) == -1);
    assert(stop_wait_make_data(&s, &p, NULL, 1) == -1);
    assert(stop_wait_make_data(&s, &p, b, MAX_PAYLOAD_SIZE + 1u) == -1);
    assert(stop_wait_make_data(&s, &p, b, sizeof b) == 0);
    assert(p.header.sequence_number == 7 && p.header.payload_length == 3);
    assert(memcmp(p.payload, b, 3) == 0);
    assert(stop_wait_make_data(&s, &p, b, sizeof b) == -1);
    assert(stop_wait_on_timeout(NULL, &retry) == -1);
    assert(stop_wait_on_timeout(&s, NULL) == -1);
    assert(stop_wait_on_timeout(&s, &retry) == 1);
    assert(s.active && s.retry_count == 1 && s.sequence_number == 7);
    assert(retry.header.sequence_number == p.header.sequence_number);
    assert(memcmp(retry.payload, p.payload, sizeof p.payload) == 0);
    assert(stop_wait_on_timeout(&s, &retry) == 1);
    assert(s.retry_count == 2 && s.sequence_number == 7);
    assert(stop_wait_on_timeout(&s, &retry) == 0); /* exhausted */
    assert(s.active && s.retry_count == 2 && s.sequence_number == 7);
    assert(stop_wait_on_ack(NULL, &a) == -1);
    assert(stop_wait_on_ack(&s, NULL) == -1);
    a.type = PACKET_DATA;
    assert(stop_wait_on_ack(&s, &a) == -1);
    a.type = PACKET_ACK; a.acknowledgement_number = 7;
    assert(stop_wait_on_ack(&s, &a) == 0);
    a.acknowledgement_number = 9; /* future ACK cannot skip outstanding data */
    assert(stop_wait_on_ack(&s, &a) == 0 && s.active && s.sequence_number == 7);
    a.acknowledgement_number = 8;
    assert(stop_wait_on_ack(&s, &a) == 1 && s.sequence_number == 8);
    assert(stop_wait_on_ack(&s, &a) == 0); /* duplicate after advancement */
    memset(max_payload, 0x5a, sizeof max_payload);
    assert(stop_wait_make_data(&s, &p, max_payload, MAX_PAYLOAD_SIZE) == 0);
    assert(p.header.payload_length == MAX_PAYLOAD_SIZE && p.payload[MAX_PAYLOAD_SIZE - 1] == 0x5a);
    {
        StopWaitState no_retries;
        stop_wait_init(&no_retries, 40, 0);
        assert(stop_wait_make_data(&no_retries, &p, b, sizeof b) == 0);
        assert(no_retries.retry_count == 0);
        assert(stop_wait_on_timeout(&no_retries, &retry) == 0);
        assert(no_retries.active && no_retries.retry_count == 0);
    }
}

static void test_gbn(void)
{
    GoBackNState s; Packet storage[3], p, retransmit[3], sentinel;
    PacketHeader a = {0};
    assert(go_back_n_init(NULL, 20, 3, storage, 3) == -1);
    assert(go_back_n_init(&s, 20, 0, storage, 3) == -1);
    assert(go_back_n_init(&s, 20, 3, NULL, 3) == -1);
    assert(go_back_n_init(&s, 20, 3, storage, 2) == -1);
    assert(go_back_n_init(&s, 20, 3, storage, 3) == 0);
    assert(go_back_n_make_data(&s, NULL, NULL, 0) == -1);
    assert(go_back_n_make_data(&s, &p, NULL, 1) == -1);
    assert(go_back_n_make_data(&s, &p, NULL, MAX_PAYLOAD_SIZE + 1u) == -1);
    {
        const uint8_t v20[] = {20}, v21[] = {21}, v22[] = {22}, v23[] = {23};
        assert(go_back_n_make_data(&s, &p, v20, sizeof v20) == 0 && p.header.sequence_number == 20);
        assert(go_back_n_make_data(&s, &storage[0], v21, sizeof v21) == -1);
        assert(storage[0].header.sequence_number == 20 && storage[0].payload[0] == 20);
        assert(go_back_n_make_data(&s, &p, v21, sizeof v21) == 0 && p.header.sequence_number == 21);
        assert(go_back_n_make_data(&s, &p, v22, sizeof v22) == 0 && p.header.sequence_number == 22);
        assert(storage[0].payload[0] == 20 && storage[1].payload[0] == 21 && storage[2].payload[0] == 22);
    assert(!go_back_n_can_send(&s));
    assert(go_back_n_on_timeout(&s, retransmit, 3) == 3);
    assert(retransmit[0].header.sequence_number == 20 &&
           retransmit[2].header.sequence_number == 22);
    memset(&sentinel, 0xa5, sizeof sentinel);
    retransmit[0] = sentinel; retransmit[1] = sentinel; retransmit[2] = sentinel;
    assert(go_back_n_on_timeout(&s, retransmit, 2) == 3); /* no partial copy */
    assert(memcmp(retransmit, (Packet[]){sentinel, sentinel, sentinel}, sizeof retransmit) == 0);
    assert(go_back_n_on_timeout(&s, NULL, 3) == 3);
    assert(s.base == 20 && s.next_sequence == 23);
    assert(storage[0].header.sequence_number == 20 && storage[2].header.sequence_number == 22);
    assert(go_back_n_on_ack(NULL, &a) == -1);
    assert(go_back_n_on_ack(&s, NULL) == -1);
    a.type = PACKET_DATA;
    assert(go_back_n_on_ack(&s, &a) == -1);
    a.type = PACKET_ACK; a.acknowledgement_number = 20;
    assert(go_back_n_on_ack(&s, &a) == 0); /* duplicate */
    a.acknowledgement_number = 21;
    assert(go_back_n_on_ack(&s, &a) == 1 && s.base == 21);
    assert(storage[0].header.sequence_number == 21 && storage[0].payload[0] == 21);
    assert(storage[1].header.sequence_number == 22 && storage[1].payload[0] == 22);
    assert(s.next_sequence == 23 && go_back_n_can_send(&s));
    assert(go_back_n_make_data(&s, &p, v23, sizeof v23) == 0);
    assert(p.header.sequence_number == 23 && storage[2].header.sequence_number == 23);
    assert(storage[2].payload[0] == 23 && s.next_sequence == 24);
    a.acknowledgement_number = 22;
    assert(go_back_n_on_ack(&s, &a) == 1 && s.base == 22);
    assert(storage[0].header.sequence_number == 22 && storage[0].payload[0] == 22);
    assert(storage[1].header.sequence_number == 23 && storage[1].payload[0] == 23);
    assert(go_back_n_on_timeout(&s, retransmit, 3) == 2);
    assert(retransmit[0].header.sequence_number == 22 && retransmit[0].payload[0] == 22);
    assert(retransmit[1].header.sequence_number == 23 && retransmit[1].payload[0] == 23);
    assert(go_back_n_can_send(&s));
    a.acknowledgement_number = 99;
    assert(go_back_n_on_ack(&s, &a) == 0);
    }
}

static void test_wraparound(void)
{
    StopWaitState sw; Packet p, retry; PacketHeader a = {0};
    GoBackNState gbn; Packet storage[3], copy[3];
    stop_wait_init(&sw, UINT32_MAX, 1);
    assert(stop_wait_make_data(&sw, &p, NULL, 0) == 0);
    assert(p.header.sequence_number == UINT32_MAX);
    assert(stop_wait_on_timeout(&sw, &retry) == 1);
    a.type = PACKET_ACK; a.acknowledgement_number = 0;
    assert(stop_wait_on_ack(&sw, &a) == 1 && sw.sequence_number == 0);

    assert(go_back_n_init(&gbn, UINT32_MAX, 3, storage, 3) == 0);
    assert(go_back_n_make_data(&gbn, &p, NULL, 0) == 0);
    assert(go_back_n_make_data(&gbn, &p, NULL, 0) == 0);
    assert(go_back_n_make_data(&gbn, &p, NULL, 0) == 0);
    assert(gbn.next_sequence == 2);
    a.acknowledgement_number = 0; /* cumulative ACK across wrap */
    assert(go_back_n_on_ack(&gbn, &a) == 1 && gbn.base == 0);
    assert(go_back_n_on_timeout(&gbn, copy, 3) == 2);
    assert(copy[0].header.sequence_number == 0 && copy[1].header.sequence_number == 1);
    a.acknowledgement_number = UINT32_MAX; /* stale ACK behind wrapped base */
    assert(go_back_n_on_ack(&gbn, &a) == 0 && gbn.base == 0);
}

int main(void)
{
    test_stop_wait(); test_gbn(); test_wraparound();
    puts("ARQ tests passed"); return 0;
}
