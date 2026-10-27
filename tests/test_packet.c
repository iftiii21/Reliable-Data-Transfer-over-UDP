#include "packet.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_data_round_trip(void)
{
    const char payload[] = {0x00, (char)0xff, 0x12, 0x00, 0x7f};
    Packet packet;
    Packet decoded;
    uint8_t bytes[PACKET_MAX_SIZE];

    create_data_packet(&packet, 0x12345678u, payload, sizeof(payload));
    assert(verify_packet(&packet));
    int length = serialize_packet(&packet, bytes, sizeof(bytes));
    assert(length == PACKET_HEADER_SIZE + (int)sizeof(payload));
    assert(deserialize_packet(&decoded, bytes, (size_t)length) == 0);
    assert(decoded.header.type == PACKET_DATA);
    assert(decoded.header.sequence_number == 0x12345678u);
    assert(decoded.header.payload_length == sizeof(payload));
    assert(memcmp(decoded.payload, payload, sizeof(payload)) == 0);

    bytes[PACKET_HEADER_SIZE] ^= 0x01u;
    assert(deserialize_packet(&decoded, bytes, (size_t)length) != 0);
}

static void test_ack_round_trip(void)
{
    Packet ack;
    Packet decoded;
    uint8_t bytes[PACKET_MAX_SIZE];

    create_ack_packet(&ack, 42u);
    assert(verify_packet(&ack));
    int length = serialize_packet(&ack, bytes, sizeof(bytes));
    assert(length == PACKET_HEADER_SIZE);
    assert(deserialize_packet(&decoded, bytes, (size_t)length) == 0);
    assert(decoded.header.type == PACKET_ACK);
    assert(decoded.header.acknowledgement_number == 42u);
}

int main(void)
{
    test_data_round_trip();
    test_ack_round_trip();
    puts("Packet tests passed");
    return 0;
}
