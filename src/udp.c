#include "udp.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>


int udp_create_socket(void)
{
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0)
    {
        perror("socket");
        return -1;
    }

    return sockfd;
}


int udp_bind_socket(int sockfd, uint16_t port)
{
    struct sockaddr_in address;

    memset(&address, 0, sizeof(address));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

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
    const struct sockaddr_in *destination)
{
    socklen_t address_length = sizeof(*destination);

    int bytes_sent = sendto(
        sockfd,
        packet,
        sizeof(Packet),
        0,
        (const struct sockaddr *)destination,
        address_length
    );

    if (bytes_sent < 0)
    {
        perror("sendto");
        return -1;
    }

    return bytes_sent;
}


int udp_receive_packet(
    int sockfd,
    Packet *packet,
    struct sockaddr_in *sender)
{
    socklen_t sender_length = sizeof(*sender);

    int bytes_received = recvfrom(
        sockfd,
        packet,
        sizeof(Packet),
        0,
        (struct sockaddr *)sender,
        &sender_length
    );

    if (bytes_received < 0)
    {
        perror("recvfrom");
        return -1;
    }

    return bytes_received;
}


void udp_close_socket(int sockfd)
{
    close(sockfd);
}