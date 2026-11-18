/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm1cs_tcp (posix_tcp.h): non-blocking TCP for a session's transport. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif
#include "rfm1cs/posix_tcp.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

int rfm1cs_tcp_connect(const char *host, int port)
{
    struct addrinfo hints, *res = NULL, *ai;
    char ps[8];
    int fd = -1;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(ps, sizeof ps, "%d", port > 0 ? port : 7720);
    if (getaddrinfo(host && *host ? host : "move.local", ps, &hints, &res) != 0 || !res)
        return -1;
    for (ai = res; ai; ai = ai->ai_next) {
        int one = 1;
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0 || errno == EINPROGRESS)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

int rfm1cs_tcp_wait(int fd, int ms)
{
    struct pollfd p;
    int err = 0, r;
    socklen_t len = sizeof err;
    p.fd = fd, p.events = POLLOUT, p.revents = 0;
    r = poll(&p, 1, ms);
    if (r == 0)
        return 0;
    if (r < 0)
        return errno == EINTR ? 0 : -1;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0)
        return -1;
    return 1;
}

int rfm1cs_tcp_read(int fd, uint8_t *buf, int cap, int ms)
{
    struct pollfd p;
    ssize_t n;
    p.fd = fd, p.events = POLLIN, p.revents = 0;
    if (ms > 0 && poll(&p, 1, ms) == 0)
        return 0;
    n = recv(fd, buf, (size_t)cap, 0);
    if (n > 0)
        return (int)n;
    if (n == 0)
        return -1;                                      /* (closed by the Move) */
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
}

int rfm1cs_tcp_write(int fd, const uint8_t *b, int n)
{
    int off = 0, waits = 0;
    while (off < n) {
        const ssize_t k = send(fd, b + off, (size_t)(n - off), MSG_NOSIGNAL);
        if (k > 0) {
            off += (int)k;
            continue;
        }
        if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) && waits++ < 100) {
            struct pollfd p;
            p.fd = fd, p.events = POLLOUT, p.revents = 0;
            poll(&p, 1, 10);
            continue;
        }
        return -1;
    }
    return 0;
}

void rfm1cs_tcp_close(int fd)
{
    if (fd >= 0)
        close(fd);
}
#endif
