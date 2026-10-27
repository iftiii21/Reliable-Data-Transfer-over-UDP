#include "udp.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int udp_create_socket(void)
{
    int sockfd = socket(
        AF_INET,
        SOCK_DGRAM,
        0
    );

    if (sockfd < 0)
    {
        perror("socket");
        return -1;
    }

    return sockfd;
}

int udp_bind_socket(
    int sockfd,
    uint16_t port
)
{
    struct sockaddr_in address;

    memset(
        &address,
        0,
        sizeof(address)
    );

    address.sin_family = AF_INET;

    address.sin_addr.s_addr =
        INADDR_ANY;

    address.sin_port =
        htons(port);

    if (bind(
            sockfd,
            (struct sockaddr *)&address,
            sizeof(address)) < 0)
    {
        perror("bind");
        return -1;
    }

    return 0;
}

int udp_send_packet(
    int sockfd,
    const Packet *packet,
    const struct sockaddr_in *destination
)
{
    uint8_t buffer[PACKET_MAX_SIZE];
    int packet_size;
    ssize_t bytes_sent;

    if (packet == NULL ||
        destination == NULL)
    {
        return -1;
    }

    packet_size = serialize_packet(
        packet,
        buffer,
        sizeof(buffer)
    );

    if (packet_size < 0)
    {
        return -1;
    }

    bytes_sent = sendto(
        sockfd,
        buffer,
        (size_t)packet_size,
        0,
        (const struct sockaddr *)destination,
        sizeof(*destination)
    );

    if (bytes_sent < 0)
    {
        perror("sendto");
        return -1;
    }

    if (bytes_sent != packet_size)
    {
        return -1;
    }

    return (int)bytes_sent;
}

int udp_receive_packet(
    int sockfd,
    Packet *packet,
    struct sockaddr_in *sender
)
{
    /* One extra byte lets us detect oversized, truncated datagrams. */
    uint8_t buffer[PACKET_MAX_SIZE + 1];
    socklen_t sender_length =
        sizeof(*sender);

    ssize_t bytes_received;

    if (packet == NULL ||
        sender == NULL)
    {
        return -1;
    }

    bytes_received = recvfrom(
        sockfd,
        buffer,
        sizeof(buffer),
        0,
        (struct sockaddr *)sender,
        &sender_length
    );

    if (bytes_received < 0)
    {
        perror("recvfrom");
        return -1;
    }

    if (deserialize_packet(
            packet,
            buffer,
            (size_t)bytes_received) != 0)
    {
        return -1;
    }

    return (int)bytes_received;
}

void udp_close_socket(int sockfd)
{
    if (sockfd >= 0)
    {
        close(sockfd);
    }
}
