/**
 * sr_sender_main.c — Standalone selective-repeat sender binary
 *
 * Usage:
 *   ./sr_sender <receiver_ip> <receiver_port> <filename> <window_size>
 *
 * Example:
 *   ./sr_sender 127.0.0.1 9000 bigfile.bin 32
 *
 * Exits 0 on success, 1 on error.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "udp.h"
#include "selective_repeat.h"

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s <receiver_ip> <receiver_port> <filename> <window_size>\n"
            "\n"
            "  receiver_ip    IP address of the receiver (e.g. 127.0.0.1)\n"
            "  receiver_port  UDP port the receiver is listening on\n"
            "  filename       Path to the file to transfer\n"
            "  window_size    Sliding window size [1, %d]\n",
            prog, SR_MAX_WINDOW);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        usage(argv[0]);
        return 1;
    }

    const char *receiver_ip   = argv[1];
    int         receiver_port = atoi(argv[2]);
    const char *filename      = argv[3];
    int         window_size   = atoi(argv[4]);

    /* Validate arguments */
    if (receiver_port <= 0 || receiver_port > 65535) {
        fprintf(stderr, "ERROR: Invalid port %d\n", receiver_port);
        return 1;
    }
    if (window_size <= 0 || window_size > SR_MAX_WINDOW) {
        fprintf(stderr, "ERROR: window_size must be in [1, %d]\n",
                SR_MAX_WINDOW);
        return 1;
    }

    /* Build receiver address */
    struct sockaddr_in peer_addr;
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port   = htons((uint16_t)receiver_port);
    if (inet_pton(AF_INET, receiver_ip, &peer_addr.sin_addr) != 1) {
        fprintf(stderr, "ERROR: Invalid IP address: %s\n", receiver_ip);
        return 1;
    }

    /* Create UDP socket */
    int sockfd = udp_create_socket();
    if (sockfd < 0) {
        fprintf(stderr, "ERROR: Failed to create UDP socket\n");
        return 1;
    }

    printf("[SR Sender] Connecting to %s:%d | file: %s | window: %d\n",
           receiver_ip, receiver_port, filename, window_size);

    /* Initialise selective-repeat sender */
    SRSender sr;
    if (sr_sender_init(&sr, sockfd, peer_addr,
                       (uint32_t)window_size, filename) != 0) {
        fprintf(stderr, "ERROR: Sender initialisation failed\n");
        udp_close_socket(sockfd);
        return 1;
    }

    /* Run the transfer */
    int run_status = sr_sender_run(&sr);

    /* Cleanup */
    sr_sender_cleanup(&sr);
    udp_close_socket(sockfd);

    return run_status == 0 ? 0 : 1;
}
