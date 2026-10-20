#include "packet.h"

#include <string.h>

static void write_u16_be(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value >> 8);
    buffer[1] = (uint8_t)(value & 0xFFu);
}

static void write_u32_be(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value >> 24);
    buffer[1] = (uint8_t)(value >> 16);
    buffer[2] = (uint8_t)(value >> 8);
    buffer[3] = (uint8_t)(value & 0xFFu);
}

static uint16_t read_u16_be(const uint8_t *buffer)
{
    return (uint16_t)(
        ((uint16_t)buffer[0] << 8) |
        buffer[1]
    );
}

static uint32_t read_u32_be(const uint8_t *buffer)
{
    return ((uint32_t)buffer[0] << 24) |
           ((uint32_t)buffer[1] << 16) |
           ((uint32_t)buffer[2] << 8) |
           (uint32_t)buffer[3];
}

/*
 * Internet-style 16-bit one's-complement checksum
 * over the canonical serialized packet.
 */
static uint16_t checksum_bytes(
    const uint8_t *buffer,
    size_t length
)
{
    uint32_t sum = 0;
    size_t i = 0;

    while (i + 1 < length)
    {
        uint16_t word = (uint16_t)(
            ((uint16_t)buffer[i] << 8) |
            buffer[i + 1]
        );

        sum += word;
        i += 2;
    }

    if (i < length)
    {
        sum += (uint16_t)((uint16_t)buffer[i] << 8);
    }

    while (sum >> 16)
    {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

static int packet_fields_valid(const Packet *packet)
{
    if (packet == NULL ||
        packet->header.payload_length > MAX_PAYLOAD_SIZE)
    {
        return 0;
    }

    switch (packet->header.type)
    {
        case PACKET_DATA:
            return 1;
        case PACKET_ACK:
        case PACKET_FIN:
            return packet->header.payload_length == 0;
        default:
            return 0;
    }
}

uint16_t calculate_checksum(const Packet *packet)
{
    uint8_t buffer[PACKET_MAX_SIZE];
    size_t payload_length;

    if (!packet_fields_valid(packet))
    {
        return 0;
    }

    payload_length = packet->header.payload_length;

    /*
     * Build the canonical wire representation.
     * Checksum field is zero while calculating.
     */
    buffer[0] = packet->header.type;

    write_u32_be(
        &buffer[1],
        packet->header.sequence_number
    );

    write_u32_be(
        &buffer[5],
        packet->header.acknowledgement_number
    );

    write_u16_be(
        &buffer[9],
        packet->header.payload_length
    );

    write_u16_be(
        &buffer[11],
        0
    );

    memcpy(
        &buffer[PACKET_HEADER_SIZE],
        packet->payload,
        payload_length
    );

    return checksum_bytes(
        buffer,
        PACKET_HEADER_SIZE + payload_length
    );
}

void prepare_packet(Packet *packet)
{
    if (packet == NULL)
    {
        return;
    }

    packet->header.checksum = 0;
    packet->header.checksum = calculate_checksum(packet);
}

int verify_packet(const Packet *packet)
{
    if (!packet_fields_valid(packet))
    {
        return 0;
    }

    return packet->header.checksum ==
           calculate_checksum(packet);
}

size_t packet_serialized_size(const Packet *packet)
{
    if (!packet_fields_valid(packet))
    {
        return 0;
    }

    return PACKET_HEADER_SIZE +
           packet->header.payload_length;
}

int serialize_packet(
    const Packet *packet,
    uint8_t *buffer,
    size_t buffer_size
)
{
    Packet temp;
    size_t total_size;

    if (!packet_fields_valid(packet) || buffer == NULL)
    {
        return -1;
    }

    total_size = packet_serialized_size(packet);

    if (buffer_size < total_size)
    {
        return -1;
    }

    /*
     * Work on a copy so serialization always uses
     * a correct checksum without modifying the caller's packet.
     */
    temp = *packet;
    prepare_packet(&temp);

    buffer[0] = temp.header.type;

    write_u32_be(
        &buffer[1],
        temp.header.sequence_number
    );

    write_u32_be(
        &buffer[5],
        temp.header.acknowledgement_number
    );

    write_u16_be(
        &buffer[9],
        temp.header.payload_length
    );

    write_u16_be(
        &buffer[11],
        temp.header.checksum
    );

    memcpy(
        &buffer[PACKET_HEADER_SIZE],
        temp.payload,
        temp.header.payload_length
    );

    return (int)total_size;
}

int deserialize_packet(
    Packet *packet,
    const uint8_t *buffer,
    size_t buffer_size
)
{
    uint16_t payload_length;
    size_t expected_size;

    if (packet == NULL ||
        buffer == NULL ||
        buffer_size < PACKET_HEADER_SIZE)
    {
        return -1;
    }

    if (buffer[0] != PACKET_DATA &&
        buffer[0] != PACKET_ACK &&
        buffer[0] != PACKET_FIN)
    {
        return -1;
    }

    payload_length = read_u16_be(&buffer[9]);

    if (payload_length > MAX_PAYLOAD_SIZE)
    {
        return -1;
    }

    expected_size =
        PACKET_HEADER_SIZE + payload_length;

    if (buffer_size != expected_size)
    {
        return -1;
    }

    memset(packet, 0, sizeof(*packet));

    packet->header.type = buffer[0];

    packet->header.sequence_number =
        read_u32_be(&buffer[1]);

    packet->header.acknowledgement_number =
        read_u32_be(&buffer[5]);

    packet->header.payload_length =
        payload_length;

    packet->header.checksum =
        read_u16_be(&buffer[11]);

    memcpy(
        packet->payload,
        &buffer[PACKET_HEADER_SIZE],
        payload_length
    );

    if (!verify_packet(packet))
    {
        return -1;
    }

    return 0;
}

void create_data_packet(
    Packet *packet,
    uint32_t sequence_number,
    const char *data,
    uint16_t data_length
)
{
    if (packet == NULL)
    {
        return;
    }

    memset(packet, 0, sizeof(*packet));

    if (data_length > MAX_PAYLOAD_SIZE)
    {
        data_length = MAX_PAYLOAD_SIZE;
    }

    packet->header.sequence_number =
        sequence_number;

    packet->header.acknowledgement_number = 0;

    packet->header.payload_length =
        data_length;

    packet->header.type = PACKET_DATA;

    if (data != NULL && data_length > 0)
    {
        memcpy(
            packet->payload,
            data,
            data_length
        );
    }

    prepare_packet(packet);
}

void create_ack_packet(
    Packet *packet,
    uint32_t acknowledgement_number
)
{
    if (packet == NULL)
    {
        return;
    }

    memset(packet, 0, sizeof(*packet));

    packet->header.sequence_number = 0;

    packet->header.acknowledgement_number =
        acknowledgement_number;

    packet->header.payload_length = 0;

    packet->header.type = PACKET_ACK;

    prepare_packet(packet);
}

void create_fin_packet(
    Packet *packet,
    uint32_t sequence_number
)
{
    if (packet == NULL)
    {
        return;
    }

    memset(packet, 0, sizeof(*packet));

    packet->header.sequence_number =
        sequence_number;

    packet->header.acknowledgement_number = 0;

    packet->header.payload_length = 0;

    packet->header.type = PACKET_FIN;

    prepare_packet(packet);
}
