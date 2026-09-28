#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

#define MAX_PAYLOAD_SIZE 1024

typedef enum {
    PACKET_DATA = 1,
    PACKET_ACK = 2,
    PACKET_FIN = 3
} PacketType;

typedef struct {
    uint32_t sequence_number;
    uint32_t acknowledgement_number;
    uint16_t payload_length;
    uint16_t checksum;
    uint8_t type;
} PacketHeader;

typedef struct {
    PacketHeader header;
    char payload[MAX_PAYLOAD_SIZE];
} Packet;
uint16_t calculate_checksum(const Packet *packet);

void prepare_packet(Packet *packet);

int verify_packet(const Packet *packet);

void create_data_packet(
    Packet *packet,
    uint32_t sequence_number,
    const char *data,
    uint16_t data_length
);

void create_ack_packet(
    Packet *packet,
    uint32_t acknowledgement_number
);

void create_fin_packet(
    Packet *packet,
    uint32_t sequence_number
);
#endif