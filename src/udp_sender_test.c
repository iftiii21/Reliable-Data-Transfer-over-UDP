#include "udp.h"

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

int main(void)
{
    int sockfd = udp_create_socket();

    if (sockfd < 0)
    {
        return 1;
    }

    struct sockaddr_in receiver;

    memset(&receiver, 0, sizeof(receiver));

    receiver.sin_family = AF_INET;
    receiver.sin_port = htons(DEFAULT_PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &receiver.sin_addr) <= 0)
    {
        printf("Invalid receiver address\n");
        udp_close_socket(sockfd);
        return 1;
    }

    const char *message = "Hello from Utkarsh!";

    Packet packet;

    create_data_packet(
        &packet,
        0,
        message,
        strlen(message)
    );

    int bytes_sent = udp_send_packet(
        sockfd,
        &packet,
        &receiver
    );

    if (bytes_sent < 0)
    {
        udp_close_socket(sockfd);
        return 1;
    }

    printf("Sent %d bytes\n", bytes_sent);

    udp_close_socket(sockfd);

    return 0;
}