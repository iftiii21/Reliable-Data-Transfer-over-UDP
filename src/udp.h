#ifndef UDP_H
#define UDP_H

#include "packet.h"
#include <stdint.h>
#include <netinet/in.h>

#define DEFAULT_PORT 9000

/*
 * Create a UDP socket.
 *
 * Returns:
 *   socket file descriptor on success
 *   -1 on failure
 */
int udp_create_socket(void);

/*
 * Bind a UDP socket to a local port.
 *
 * Returns:
 *   0 on success
 *   -1 on failure
 */
int udp_bind_socket(int sockfd, uint16_t port);

/*
 * Send a packet to a destination.
 *
 * Returns:
 *   number of bytes sent on success
 *   -1 on failure
 */
int udp_send_packet(
    int sockfd,
    const Packet *packet,
    const struct sockaddr_in *destination
);

/*
 * Receive a packet.
 *
 * The sender's address is stored in `sender`.
 *
 * Returns:
 *   number of bytes received on success
 *   -1 on failure
 */
int udp_receive_packet(
    int sockfd,
    Packet *packet,
    struct sockaddr_in *sender
);

/*
 * Close the UDP socket.
 */
void udp_close_socket(int sockfd);

#endif