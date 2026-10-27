#ifndef NET_PORTABLE_H
#define NET_PORTABLE_H

/* POSIX is the assignment target; Winsock permits local Windows verification. */
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif
#include <stdint.h>
#include <stdio.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET net_socket;
typedef int net_socklen;
#define NET_INVALID INVALID_SOCKET
#define net_close closesocket
static inline int net_start(void) { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w); }
static inline void net_stop(void) { WSACleanup(); }
static inline uint64_t net_now_us(void)
{
    LARGE_INTEGER count, frequency;
    QueryPerformanceCounter(&count);
    QueryPerformanceFrequency(&frequency);
    return (uint64_t)(count.QuadPart / frequency.QuadPart) * 1000000u +
        (uint64_t)(count.QuadPart % frequency.QuadPart) * 1000000u /
        (uint64_t)frequency.QuadPart;
}
static inline int net_interrupted(void) { return WSAGetLastError() == WSAEINTR; }
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
typedef int net_socket;
typedef socklen_t net_socklen;
#define NET_INVALID (-1)
#define net_close close
static inline int net_start(void) { return 0; }
static inline void net_stop(void) { }
static inline uint64_t net_now_us(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) { perror("clock_gettime"); _exit(1); }
    return (uint64_t)time.tv_sec * 1000000u + (uint64_t)time.tv_nsec / 1000u;
}
static inline int net_interrupted(void) { return errno == EINTR; }
#endif

static inline int net_same_peer(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static inline net_socket net_bind(unsigned port)
{
    struct sockaddr_in address = {0};
    net_socket socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd == NET_INVALID) return NET_INVALID;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((uint16_t)port);
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        net_close(socket_fd);
        return NET_INVALID;
    }
    return socket_fd;
}

#endif
