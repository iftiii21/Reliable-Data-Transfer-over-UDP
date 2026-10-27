/**
 * sr_receiver_main.c — Standalone selective-repeat receiver binary
 *
 * Usage:
 *   ./sr_receiver <port> <output_filename> <window_size>
 *
 * Example:
 *   ./sr_receiver 9000 received_file.bin 32
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
            "Usage: %s <port> <output_filename> <window_size>\n"
            "\n"
            "  port             UDP port to listen on\n"
            "  output_filename  Path to write the received file\n"
            "  window_size      Receiver window size [1, %d]\n",
            prog, SR_MAX_WINDOW);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        usage(argv[0]);
        return 1;
    }

    int         port            = atoi(argv[1]);
    const char *output_filename = argv[2];
    int         window_size     = atoi(argv[3]);

    /* Validate arguments */
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "ERROR: Invalid port %d\n", port);
        return 1;
    }
    if (window_size <= 0 || window_size > SR_MAX_WINDOW) {
        fprintf(stderr, "ERROR: window_size must be in [1, %d]\n",
                SR_MAX_WINDOW);
        return 1;
    }

    /* Create and bind UDP socket */
    int sockfd = udp_create_socket();
    if (sockfd < 0) {
        fprintf(stderr, "ERROR: Failed to create UDP socket\n");
        return 1;
    }
    if (udp_bind_socket(sockfd, (uint16_t)port) != 0) {
        fprintf(stderr, "ERROR: Failed to bind socket to port %d\n", port);
        udp_close_socket(sockfd);
        return 1;
    }

    printf("[SR Receiver] Listening on port %d | output: %s | window: %d\n",
           port, output_filename, window_size);

    /*
     * Peer address: we accept the sender's address dynamically from the first
     * packet.  Pass a zeroed-out address; sr_receiver_run() will fill it in
     * from the incoming packet's source address.
     */
    struct sockaddr_in peer_addr;
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;

    /* Initialise selective-repeat receiver */
    SRReceiver sr;
    if (sr_receiver_init(&sr, sockfd, peer_addr,
                         (uint32_t)window_size, output_filename) != 0) {
        fprintf(stderr, "ERROR: Receiver initialisation failed\n");
        udp_close_socket(sockfd);
        return 1;
    }

    /* Run the receive loop */
    int run_status = sr_receiver_run(&sr);

    /* Cleanup */
    sr_receiver_cleanup(&sr);
    udp_close_socket(sockfd);

    if (run_status != 0) {
        fprintf(stderr, "ERROR: Receiver failed before completing the transfer\n");
        return 1;
    }

    printf("[SR Receiver] Done. File written to: %s\n", output_filename);
    return 0;
}
