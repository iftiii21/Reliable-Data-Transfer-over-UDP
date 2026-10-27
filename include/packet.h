#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

#define MAX_PAYLOAD_SIZE 1024
#define PACKET_MAGIC 0xABCD
#define PROTOCOL_VERSION 1

// Packet types
typedef enum {
    PACKET_DATA = 1,
    PACKET_ACK = 2,
    PACKET_START = 3,
    PACKET_END = 4
} PacketType;

// Packet header
typedef struct {
    uint16_t magic;
    uint8_t version;
    uint8_t type;

    uint32_t sequence_number;
    uint32_t acknowledgement_number;

    uint16_t window_size;
    uint16_t payload_length;

    uint32_t checksum;
} PacketHeader;

// Complete packet
typedef struct {
    PacketHeader header;
    uint8_t payload[MAX_PAYLOAD_SIZE];
} Packet;

#endif