#include "packet.h"
#include <string.h>

/*
 * Calculate checksum for a packet.
 *
 * The checksum field itself is ignored.
 */
uint16_t calculate_checksum(const Packet *packet)
{
    uint32_t sum = 0;

    sum += packet->header.sequence_number;
    sum += packet->header.acknowledgement_number;
    sum += packet->header.payload_length;
    sum += packet->header.type;

    for (uint16_t i = 0; i < packet->header.payload_length; i++)
    {
        sum += (unsigned char)packet->payload[i];
    }

    /*
     * Fold the 32-bit sum into 16 bits.
     */
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}


/*
 * Calculate and store the checksum.
 */
void prepare_packet(Packet *packet)
{
    packet->header.checksum = 0;

    packet->header.checksum = calculate_checksum(packet);
}


/*
 * Verify the checksum of a received packet.
 *
 * Returns:
 * 1 -> packet is valid
 * 0 -> packet is corrupted
 */
int verify_packet(const Packet *packet)
{
    uint16_t received_checksum = packet->header.checksum;

    Packet temp = *packet;

    temp.header.checksum = 0;

    uint16_t calculated_checksum = calculate_checksum(&temp);

    return received_checksum == calculated_checksum;
}


/*
 * Create a DATA packet.
 */
void create_data_packet(
    Packet *packet,
    uint32_t sequence_number,
    const char *data,
    uint16_t data_length
)
{
    memset(packet, 0, sizeof(Packet));

    /*
     * Never allow payload to exceed packet capacity.
     */
    if (data_length > MAX_PAYLOAD_SIZE)
    {
        data_length = MAX_PAYLOAD_SIZE;
    }

    packet->header.sequence_number = sequence_number;
    packet->header.acknowledgement_number = 0;
    packet->header.payload_length = data_length;
    packet->header.type = PACKET_DATA;

    memcpy(packet->payload, data, data_length);

    prepare_packet(packet);
}


/*
 * Create an ACK packet.
 */
void create_ack_packet(
    Packet *packet,
    uint32_t acknowledgement_number
)
{
    memset(packet, 0, sizeof(Packet));

    packet->header.sequence_number = 0;
    packet->header.acknowledgement_number = acknowledgement_number;
    packet->header.payload_length = 0;
    packet->header.type = PACKET_ACK;

    prepare_packet(packet);
}


/*
 * Create a FIN packet.
 *
 * FIN tells the receiver that the sender
 * has finished transmitting the file.
 */
void create_fin_packet(
    Packet *packet,
    uint32_t sequence_number
)
{
    memset(packet, 0, sizeof(Packet));

    packet->header.sequence_number = sequence_number;
    packet->header.acknowledgement_number = 0;
    packet->header.payload_length = 0;
    packet->header.type = PACKET_FIN;

    prepare_packet(packet);
}