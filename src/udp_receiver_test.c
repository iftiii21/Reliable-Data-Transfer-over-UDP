#include "udp.h"

#include <stdio.h>

int main(void)
{
    int sockfd = udp_create_socket();

    if (sockfd < 0)
    {
        return 1;
    }

    if (udp_bind_socket(sockfd, DEFAULT_PORT) < 0)
    {
        udp_close_socket(sockfd);
        return 1;
    }

    printf("Receiver listening on port %d...\n", DEFAULT_PORT);

    Packet packet;
    struct sockaddr_in sender;

    int bytes_received = udp_receive_packet(
        sockfd,
        &packet,
        &sender
    );

    if (bytes_received < 0)
    {
        udp_close_socket(sockfd);
        return 1;
    }

    printf("Received %d bytes\n", bytes_received);

    if (!verify_packet(&packet))
    {
        printf("Packet checksum INVALID\n");
        udp_close_socket(sockfd);
        return 1;
    }

    printf("Packet checksum VALID\n");
    printf("Packet type: %d\n", packet.header.type);
    printf("Sequence number: %u\n",
           packet.header.sequence_number);

    printf("Payload: %.*s\n",
           packet.header.payload_length,
           packet.payload);

    udp_close_socket(sockfd);

    return 0;
}