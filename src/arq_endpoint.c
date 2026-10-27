#define _POSIX_C_SOURCE 200809L

#include "arq.h"
#include "file_transfer.h"
#include "udp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RETRANSMIT_TIMEOUT_MS 250
#define MAX_RETRIES 8u
#define MAX_WINDOW_SIZE 1024u
#define RECEIVER_IDLE_TIMEOUT_MS 120000u
#define FIN_LINGER_MS ((MAX_RETRIES + 1u) * RETRANSMIT_TIMEOUT_MS + 500u)

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static int parse_port(const char *text, uint16_t *port)
{
    char *end = NULL;
    unsigned long value;
    if (text == NULL || port == NULL || *text == '\0') return -1;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0 || value > 65535u)
        return -1;
    *port = (uint16_t)value;
    return 0;
}

static int parse_window(const char *text, uint16_t *window)
{
    char *end = NULL;
    unsigned long value;
    if (text == NULL || window == NULL || *text == '\0') return -1;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0 ||
        value > MAX_WINDOW_SIZE)
        return -1;
    *window = (uint16_t)value;
    return 0;
}

static int same_peer(const struct sockaddr_in *left,
                     const struct sockaddr_in *right)
{
    return left->sin_family == right->sin_family &&
           left->sin_port == right->sin_port &&
           left->sin_addr.s_addr == right->sin_addr.s_addr;
}

/* 1=valid datagram received, 0=timeout, -1=poll/socket error. */
static int receive_with_timeout(int socket_fd, Packet *packet,
                                struct sockaddr_in *sender, int timeout_ms)
{
    struct pollfd descriptor;
    int result;
    descriptor.fd = socket_fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    do {
        result = poll(&descriptor, 1, timeout_ms);
    } while (result < 0 && errno == EINTR);
    if (result == 0) return 0;
    if (result < 0 || (descriptor.revents & (POLLERR | POLLNVAL)) != 0) return -1;
    if ((descriptor.revents & POLLIN) == 0) return -1;
    /* Invalid length/checksum datagrams are ignored by the caller. */
    if (udp_receive_packet(socket_fd, packet, sender) < 0) return 2;
    return 1;
}

static int send_ack(int socket_fd, const struct sockaddr_in *peer,
                    uint32_t next_expected)
{
    Packet ack;
    create_ack_packet(&ack, next_expected);
    return udp_send_packet(socket_fd, &ack, peer) < 0 ? -1 : 0;
}

static int wait_for_final_ack(int socket_fd, const struct sockaddr_in *peer,
                              uint32_t fin_sequence, uint32_t *retries)
{
    Packet fin;
    uint32_t retry_count = 0;
    create_fin_packet(&fin, fin_sequence);
    if (udp_send_packet(socket_fd, &fin, peer) < 0) return -1;

    for (;;) {
        Packet received;
        struct sockaddr_in source;
        int result = receive_with_timeout(socket_fd, &received, &source,
                                          RETRANSMIT_TIMEOUT_MS);
        if (result < 0) return -1;
        if (result == 1 && same_peer(peer, &source) &&
            received.header.type == PACKET_ACK &&
            received.header.acknowledgement_number == fin_sequence + 1u) {
            *retries = retry_count;
            return 0;
        }
        if (result == 0) {
            if (retry_count >= MAX_RETRIES) return -1;
            retry_count++;
            if (udp_send_packet(socket_fd, &fin, peer) < 0) return -1;
        }
    }
}

static int send_stop_wait(int socket_fd, const struct sockaddr_in *peer,
                          const char *input_path, const char *metrics_path)
{
    FileSender file;
    StopWaitState state;
    uint32_t retransmissions = 0;
    int status = -1;
    if (file_sender_open(&file, input_path) != 0) return -1;
    stop_wait_init(&state, 0, MAX_RETRIES);

    for (;;) {
        Packet source_packet;
        Packet outgoing;
        int read_result = file_sender_next_packet(&file, &source_packet);
        if (read_result < 0) goto done;
        if (read_result == 0) break;
        if (stop_wait_make_data(&state, &outgoing,
                                (const uint8_t *)source_packet.payload,
                                source_packet.header.payload_length) != 0)
            goto done;
        if (udp_send_packet(socket_fd, &outgoing, peer) < 0) goto done;

        while (state.active) {
            Packet received;
            struct sockaddr_in source;
            int result = receive_with_timeout(socket_fd, &received, &source,
                                              RETRANSMIT_TIMEOUT_MS);
            if (result < 0) goto done;
            if (result == 1 && same_peer(peer, &source) &&
                stop_wait_on_ack(&state, &received.header) == 1)
                break;
            if (result == 0) {
                int retry = stop_wait_on_timeout(&state, &outgoing);
                if (retry != 1) goto done;
                retransmissions++;
                if (udp_send_packet(socket_fd, &outgoing, peer) < 0) goto done;
            }
        }
    }

    {
        uint32_t fin_retries = 0;
        if (wait_for_final_ack(socket_fd, peer, state.sequence_number,
                               &fin_retries) != 0)
            goto done;
        retransmissions += fin_retries;
    }
    {
        FILE *metrics = fopen(metrics_path, "w");
        if (metrics == NULL) goto done;
        {
            int write_failed = fprintf(metrics, "retransmissions=%u\n",
                                       retransmissions) < 0;
            if (fclose(metrics) != 0) write_failed = 1;
            if (write_failed) {
                status = -1;
                goto done;
            }
        }
    }
    printf("FIN ACK received. Transfer complete.\n");
    printf("Retransmissions: %u\n", retransmissions);
    status = 0;

done:
    file_sender_close(&file);
    return status;
}

static int send_go_back_n(int socket_fd, const struct sockaddr_in *peer,
                          const char *input_path, const char *metrics_path,
                          uint16_t window_size)
{
    FileSender file;
    GoBackNState state;
    Packet *storage = NULL;
    Packet *retransmissions_buffer = NULL;
    uint32_t retries_since_progress = 0;
    uint32_t retransmissions = 0;
    int eof = 0;
    int timer_active = 0;
    uint64_t timer_started = 0;
    int status = -1;

    if (file_sender_open(&file, input_path) != 0) return -1;
    storage = calloc(window_size, sizeof(*storage));
    retransmissions_buffer = calloc(window_size, sizeof(*retransmissions_buffer));
    if (storage == NULL || retransmissions_buffer == NULL ||
        go_back_n_init(&state, 0, window_size, storage, window_size) != 0)
        goto done;

    while (!eof || state.base != state.next_sequence) {
        while (!eof && go_back_n_can_send(&state)) {
            Packet source_packet;
            Packet outgoing;
            uint32_t outstanding_before = state.next_sequence - state.base;
            int read_result = file_sender_next_packet(&file, &source_packet);
            if (read_result < 0) goto done;
            if (read_result == 0) {
                eof = 1;
                break;
            }
            if (go_back_n_make_data(&state, &outgoing,
                                    (const uint8_t *)source_packet.payload,
                                    source_packet.header.payload_length) != 0)
                goto done;
            if (udp_send_packet(socket_fd, &outgoing, peer) < 0) goto done;
            if (outstanding_before == 0) {
                timer_started = monotonic_ms();
                timer_active = 1;
            }
        }

        if (eof && state.base == state.next_sequence) break;
        if (!timer_active) {
            timer_started = monotonic_ms();
            timer_active = 1;
        }

        {
            uint64_t elapsed = monotonic_ms() - timer_started;
            int remaining = elapsed >= RETRANSMIT_TIMEOUT_MS ? 0 :
                RETRANSMIT_TIMEOUT_MS - (int)elapsed;
            Packet received;
            struct sockaddr_in source;
            int result = receive_with_timeout(socket_fd, &received, &source,
                                              remaining);
            if (result < 0) goto done;
            if (result == 1 && same_peer(peer, &source)) {
                int newly_acked = go_back_n_on_ack(&state, &received.header);
                if (newly_acked > 0) {
                    retries_since_progress = 0;
                    timer_active = state.base != state.next_sequence;
                    timer_started = monotonic_ms();
                }
            } else if (result == 0) {
                size_t count = go_back_n_on_timeout(&state,
                    retransmissions_buffer, window_size);
                size_t i;
                if (count > window_size) goto done;
                if (count == 0) {
                    timer_active = 0;
                    continue;
                }
                if (retries_since_progress >= MAX_RETRIES) goto done;
                retries_since_progress++;
                for (i = 0; i < count; ++i) {
                    if (udp_send_packet(socket_fd, &retransmissions_buffer[i],
                                        peer) < 0)
                        goto done;
                }
                retransmissions += (uint32_t)count;
                timer_started = monotonic_ms();
                timer_active = 1;
            }
        }
    }

    {
        uint32_t fin_retries = 0;
        if (wait_for_final_ack(socket_fd, peer, state.next_sequence,
                               &fin_retries) != 0)
            goto done;
        retransmissions += fin_retries;
    }
    {
        FILE *metrics = fopen(metrics_path, "w");
        if (metrics == NULL) goto done;
        {
            int write_failed = fprintf(metrics, "retransmissions=%u\n",
                                       retransmissions) < 0;
            if (fclose(metrics) != 0) write_failed = 1;
            if (write_failed) {
                status = -1;
                goto done;
            }
        }
    }
    printf("FIN ACK received. Transfer complete.\n");
    printf("Retransmissions: %u\n", retransmissions);
    status = 0;

done:
    file_sender_close(&file);
    free(retransmissions_buffer);
    free(storage);
    return status;
}

static int run_sender(const char *mode, const char *host, const char *port_text,
                      const char *input_path, const char *window_text,
                      const char *metrics_path)
{
    struct sockaddr_in peer;
    uint16_t port, window_size;
    int socket_fd;
    int result;
    if (parse_port(port_text, &port) != 0 ||
        parse_window(window_text, &window_size) != 0)
        return -1;
    memset(&peer, 0, sizeof(peer));
    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &peer.sin_addr) != 1) return -1;
    socket_fd = udp_create_socket();
    if (socket_fd < 0) return -1;
    if (strcmp(mode, "sw") == 0)
        result = send_stop_wait(socket_fd, &peer, input_path, metrics_path);
    else
        result = send_go_back_n(socket_fd, &peer, input_path, metrics_path,
                                window_size);
    udp_close_socket(socket_fd);
    return result;
}

static int run_receiver(const char *mode, const char *port_text,
                        const char *output_path, const char *window_text)
{
    FileReceiver file;
    uint16_t port, window_size;
    struct sockaddr_in peer;
    uint32_t next_expected = 0;
    uint32_t final_sequence = 0;
    int peer_known = 0;
    int wrapped = 0;
    int fin_seen = 0;
    int socket_fd = -1;
    uint64_t last_activity;
    uint64_t fin_started = 0;
    int status = -1;

    if (parse_port(port_text, &port) != 0 ||
        parse_window(window_text, &window_size) != 0)
        return -1;
    (void)window_size;
    socket_fd = udp_create_socket();
    if (socket_fd < 0 || udp_bind_socket(socket_fd, port) != 0) goto done;
    if (file_receiver_open(&file, output_path) != 0) goto done;
    printf("READY %s\n", mode);
    fflush(stdout);
    last_activity = monotonic_ms();

    for (;;) {
        Packet received;
        struct sockaddr_in source;
        int result = receive_with_timeout(socket_fd, &received, &source, 1000);
        if (result < 0) goto close_file;
        if (result == 0) {
            if (fin_seen && monotonic_ms() - fin_started >= FIN_LINGER_MS) {
                status = 0;
                goto done;
            }
            if (monotonic_ms() - last_activity >= RECEIVER_IDLE_TIMEOUT_MS)
                goto close_file;
            continue;
        }
        if (result != 1) continue;
        if (received.header.type != PACKET_DATA &&
            received.header.type != PACKET_FIN)
            continue;
        if (!peer_known) {
            peer = source;
            peer_known = 1;
        } else if (!same_peer(&peer, &source)) {
            continue;
        }
        last_activity = monotonic_ms();

        if (received.header.type == PACKET_DATA) {
            if (!fin_seen && !wrapped &&
                received.header.sequence_number == next_expected) {
                if (file_receiver_write_packet(&file, &received) != 0)
                    goto close_file;
                next_expected++;
                if (next_expected == 0) wrapped = 1;
            }
            if (send_ack(socket_fd, &peer, next_expected) != 0)
                goto close_file;
        } else if (!fin_seen &&
                   received.header.sequence_number == next_expected) {
            final_sequence = received.header.sequence_number;
            if (fflush(file.file) != 0) goto close_file;
            file_receiver_close(&file);
            if (send_ack(socket_fd, &peer, final_sequence + 1u) != 0)
                goto done;
            fin_seen = 1;
            fin_started = monotonic_ms();
        } else if (fin_seen &&
                   received.header.sequence_number == final_sequence) {
            if (send_ack(socket_fd, &peer, final_sequence + 1u) != 0)
                goto done;
        } else if (!fin_seen) {
            if (send_ack(socket_fd, &peer, next_expected) != 0)
                goto close_file;
        }

        if (fin_seen && monotonic_ms() - fin_started >= FIN_LINGER_MS) {
            status = 0;
            goto done;
        }
        if (fin_seen) {
            /* The next poll is bounded; answer retransmitted FINs during linger. */
            continue;
        }
    }

close_file:
    file_receiver_close(&file);
done:
    udp_close_socket(socket_fd);
    return status;
}

static void usage(const char *program)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s sw|gbn sender HOST PORT INPUT WINDOW METRICS_FILE\n"
        "  %s sw|gbn receiver PORT OUTPUT WINDOW\n",
        program, program);
}

int main(int argc, char **argv)
{
    int result;
    if (argc >= 2 && (strcmp(argv[1], "sw") == 0 ||
                      strcmp(argv[1], "gbn") == 0)) {
        if (argc == 8 && strcmp(argv[2], "sender") == 0) {
            result = run_sender(argv[1], argv[3], argv[4], argv[5],
                                argv[6], argv[7]);
        } else if (argc == 6 && strcmp(argv[2], "receiver") == 0) {
            result = run_receiver(argv[1], argv[3], argv[4], argv[5]);
        } else {
            usage(argv[0]);
            return 2;
        }
        if (result != 0) {
            fprintf(stderr, "%s transfer endpoint failed\n", argv[1]);
            return 1;
        }
        return 0;
    }
    usage(argv[0]);
    return 2;
}
