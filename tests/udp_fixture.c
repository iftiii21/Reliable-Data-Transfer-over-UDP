/* Test traffic only: echo/probe and a retrying roundtrip byte-integrity check.
 * This is not an implementation or benchmark of the team's three ARQ protocols. */
#include "net_portable.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static int readable(net_socket socket_fd, uint64_t wait_us)
{
    fd_set set;
    struct timeval timeout;
    FD_ZERO(&set);
    FD_SET(socket_fd, &set);
    timeout.tv_sec = (long)(wait_us / 1000000);
    timeout.tv_usec = (long)(wait_us % 1000000);
    return select((int)socket_fd + 1, &set, NULL, NULL, &timeout);
}

static struct sockaddr_in loopback(unsigned port)
{
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return address;
}

static int echo_server(net_socket socket_fd)
{
    char bytes[65507];
    uint64_t last = net_now_us();
    fprintf(stderr, "READY echo\n");
    fflush(stderr);
    while (net_now_us() - last < 700000) {
        struct sockaddr_in from;
        net_socklen length = sizeof(from);
        int size, ready = readable(socket_fd, 20000);
        if (ready < 0) return 1;
        if (!ready) continue;
        size = recvfrom(socket_fd, bytes, sizeof(bytes), 0, (struct sockaddr *)&from, &length);
        if (size < 0 || sendto(socket_fd, bytes, size, 0, (struct sockaddr *)&from, length) != size) return 1;
        last = net_now_us();
    }
    return 0;
}

static void make_probe(unsigned char *bytes, unsigned sequence, size_t length)
{
    uint32_t network = htonl(sequence);
    memcpy(bytes, &network, sizeof(network));
    for (size_t i = 4; i < length; i++) bytes[i] = (unsigned char)(sequence * 31u + i);
}

static int boundaries(net_socket socket_fd, unsigned port)
{
    struct sockaddr_in peer = loopback(port), from;
    unsigned char sent[65507], received[65507];
    const int lengths[] = {0, 65507};
    for (size_t i = 0; i < sizeof(sent); i++) sent[i] = (unsigned char)(i * 17u);
    for (unsigned i = 0; i < 2; i++) {
        net_socklen from_size = sizeof(from);
        int count;
        if (sendto(socket_fd, (char *)sent, lengths[i], 0,
                   (struct sockaddr *)&peer, sizeof(peer)) != lengths[i]) return 1;
        if (readable(socket_fd, 500000) <= 0) return 1;
        count = recvfrom(socket_fd, (char *)received, sizeof(received), 0,
                         (struct sockaddr *)&from, &from_size);
        if (count != lengths[i] || !net_same_peer(&from, &peer) ||
            memcmp(sent, received, (size_t)lengths[i])) return 1;
    }
    puts("zero-byte and maximum IPv4 UDP payloads passed");
    return 0;
}

static int probe(net_socket socket_fd, unsigned port, const char *mode)
{
    enum { COUNT = 64, SIZE = 128 };
    struct sockaddr_in peer = loopback(port);
    unsigned char bytes[SIZE], expected[SIZE], seen[COUNT] = {0};
    unsigned unique = 0, duplicates = 0, corrupt = 0, inversion = 0, max_seen = 0;
    uint64_t start = net_now_us();
    for (unsigned i = 0; i < COUNT; i++) {
        make_probe(bytes, i, SIZE);
        if (sendto(socket_fd, (const char *)bytes, SIZE, 0, (struct sockaddr *)&peer, sizeof(peer)) != SIZE) return 1;
    }
    while (net_now_us() - start < 650000) {
        struct sockaddr_in from;
        net_socklen from_size = sizeof(from);
        uint32_t sequence;
        int size, ready = readable(socket_fd, 20000);
        if (ready < 0) return 1;
        if (!ready) continue;
        size = recvfrom(socket_fd, (char *)bytes, SIZE, 0, (struct sockaddr *)&from, &from_size);
        if (size != SIZE || !net_same_peer(&from, &peer)) { corrupt++; continue; }
        memcpy(&sequence, bytes, sizeof(sequence));
        sequence = ntohl(sequence);
        if (sequence >= COUNT) { corrupt++; continue; }
        make_probe(expected, sequence, SIZE);
        if (memcmp(expected, bytes, SIZE)) { corrupt++; continue; }
        if (seen[sequence]) duplicates++;
        else {
            if (unique && sequence < max_seen) inversion++;
            if (sequence > max_seen) max_seen = sequence;
            seen[sequence] = 1;
            unique++;
        }
    }
    printf("mode=%s unique=%u duplicates=%u corrupt=%u inversions=%u\n",
           mode, unique, duplicates, corrupt, inversion);
    if (!strcmp(mode, "clean") || !strcmp(mode, "delay") || !strcmp(mode, "jitter"))
        return unique != COUNT || duplicates || corrupt;
    if (!strcmp(mode, "loss")) return unique != 0;
    if (!strcmp(mode, "duplicate")) return unique != COUNT || !duplicates || corrupt;
    if (!strcmp(mode, "corrupt")) return unique != 0 || !corrupt;
    if (!strcmp(mode, "reorder")) return unique != COUNT || !inversion || corrupt;
    if (!strcmp(mode, "overflow")) return unique == COUNT;
    return 1;
}

static int roundtrip(net_socket socket_fd, unsigned port, const char *source,
                     const char *output, const char *metrics)
{
    struct sockaddr_in peer = loopback(port);
    unsigned char bytes[1028], reply[65507];
    uint32_t sequence = 0;
    uint64_t retransmissions = 0, total = 0, start = net_now_us();
    FILE *input = fopen(source, "rb"), *destination = NULL, *report = NULL;
    int status = 1;
    if (!input) goto done;
    destination = fopen(output, "wb");
    if (!destination) goto done;
    for (;;) {
        size_t payload = fread(bytes + 4, 1, sizeof(bytes) - 4, input);
        uint32_t network = htonl(sequence++);
        int accepted = 0;
        if (!payload) { if (ferror(input)) goto done; break; }
        memcpy(bytes, &network, sizeof(network));
        for (unsigned retry = 0; retry < 100 && !accepted; retry++) {
            uint64_t deadline = net_now_us() + 120000;
            if (retry) retransmissions++;
            if (sendto(socket_fd, (char *)bytes, (int)(payload + 4), 0,
                       (struct sockaddr *)&peer, sizeof(peer)) != (int)(payload + 4)) goto done;
            while (net_now_us() < deadline) {
                struct sockaddr_in from;
                net_socklen from_size = sizeof(from);
                int size, ready = readable(socket_fd, 10000);
                if (ready < 0) goto done;
                if (!ready) continue;
                size = recvfrom(socket_fd, (char *)reply, sizeof(reply), 0,
                                (struct sockaddr *)&from, &from_size);
                if (size == (int)(payload + 4) && net_same_peer(&from, &peer) &&
                    !memcmp(bytes, reply, payload + 4)) { accepted = 1; break; }
            }
        }
        if (!accepted || fwrite(bytes + 4, 1, payload, destination) != payload) goto done;
        total += payload;
    }
    if (fclose(destination)) { destination = NULL; goto done; }
    destination = NULL;
    report = fopen(metrics, "w");
    if (!report) goto done;
    fprintf(report, "elapsed_us=%" PRIu64 "\nretransmissions=%" PRIu64 "\nbytes=%" PRIu64 "\n",
            net_now_us() - start, retransmissions, total);
    status = 0;
done:
    if (report && fclose(report)) status = 1;
    if (destination) fclose(destination);
    if (input) fclose(input);
    return status;
}

int main(int argc, char **argv)
{
    unsigned port;
    net_socket socket_fd;
    int status = 2;
    if (argc < 3) return 2;
    port = (unsigned)strtoul(argv[2], NULL, 10);
    if (!port || port > 65535 || net_start()) return 2;
    socket_fd = net_bind(!strcmp(argv[1], "echo") ? port : 0);
    if (socket_fd == NET_INVALID) { net_stop(); return 1; }
    if (argc == 3 && !strcmp(argv[1], "echo")) status = echo_server(socket_fd);
    if (argc == 3 && !strcmp(argv[1], "boundaries")) status = boundaries(socket_fd, port);
    if (argc == 4 && !strcmp(argv[1], "probe")) status = probe(socket_fd, port, argv[3]);
    if (argc == 6 && !strcmp(argv[1], "roundtrip"))
        status = roundtrip(socket_fd, port, argv[3], argv[4], argv[5]);
    net_close(socket_fd);
    net_stop();
    return status;
}
