/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm1cs_tcp (posix_tcp.h): "host:port" as a user types it, and a connect to a listener on this machine by it. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "check.h"
#include "rfm1cs/posix_tcp.h"

static void parse(const char *in, int def, int ok, const char *host, int port)
{
    char h[64] = "?";
    int p = -9;
    const int r = rfm1cs_tcp_parse(in, def, h, (int)sizeof h, &p);
    if (ok)
        CHECKF(r == 0 && !strcmp(h, host) && p == port, "\"%s\" (%d): \"%s\" %d, want \"%s\" %d", in ? in : "(null)", def, h,
               p, host, port);
    else
        CHECKF(r == -1, "\"%s\": refused (%d, \"%s\" %d)", in, r, h, p);
}

/* a listener on loopback (v4 or v6), its port; -1 when this machine has none */
static int listener(int v6, int *port)
{
    int fd = socket(v6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0), one = 1;
    struct sockaddr_storage ss;
    socklen_t len = sizeof ss;
    if (fd < 0)
        return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&ss, 0, sizeof ss);
    if (v6) {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)&ss;
        a->sin6_family = AF_INET6, a->sin6_addr = in6addr_loopback;
        len = sizeof *a;
    } else {
        struct sockaddr_in *a = (struct sockaddr_in *)&ss;
        a->sin_family = AF_INET, a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        len = sizeof *a;
    }
    if (bind(fd, (struct sockaddr *)&ss, len) != 0 || listen(fd, 4) != 0 || getsockname(fd, (struct sockaddr *)&ss, &len) != 0) {
        close(fd);
        return -1;
    }
    *port = ntohs(v6 ? ((struct sockaddr_in6 *)&ss)->sin6_port : ((struct sockaddr_in *)&ss)->sin_port);
    return fd;
}
static void connects(const char *fmt, int v6)
{
    char addr[64];
    int port = 0, l = listener(v6, &port), fd, r = 0, i;
    if (l < 0) {
        printf("tcp: no %s loopback here, skipped\n", v6 ? "IPv6" : "IPv4");
        return;
    }
    snprintf(addr, sizeof addr, fmt, port);
    fd = rfm1cs_tcp_connect(addr, 1);                  /* (port 1: the address's own wins) */
    for (i = 0; i < 100 && fd >= 0 && r == 0; i++)
        r = rfm1cs_tcp_wait(fd, 20);
    CHECKF(fd >= 0 && r == 1, "connect to %s: connected (%d, %d)", addr, fd, r);
    rfm1cs_tcp_close(fd);
    close(l);
}

int main(void)
{
    parse("move.local", 0, 1, "move.local", 7720);
    parse("", 0, 1, "move.local", 7720);
    parse(NULL, 0, 1, "move.local", 7720);
    parse("move.local", 7721, 1, "move.local", 7721);
    parse("move.local:7800", 7721, 1, "move.local", 7800);
    parse("172.16.254.1:7720", 0, 1, "172.16.254.1", 7720);
    parse("172.16.254.1", 0, 1, "172.16.254.1", 7720);
    parse("[fe80::1]:9000", 0, 1, "fe80::1", 9000);
    parse("[::1]", 0, 1, "::1", 7720);
    parse("[fe80::1%en0]:7720", 0, 1, "fe80::1%en0", 7720);
    parse("fe80::1", 0, 1, "fe80::1", 7720);
    parse("::1", 4000, 1, "::1", 4000);
    parse("move.local:65535", 0, 1, "move.local", 65535);
    parse("move.local:", 0, 0, NULL, 0);
    parse("move.local:0", 0, 0, NULL, 0);
    parse("move.local:65536", 0, 0, NULL, 0);
    parse("move.local:77x", 0, 0, NULL, 0);
    parse(":7720", 0, 0, NULL, 0);
    parse("[::1", 0, 0, NULL, 0);
    parse("[]:7720", 0, 0, NULL, 0);
    parse("[::1]7720", 0, 0, NULL, 0);
    parse("[::1]:", 0, 0, NULL, 0);
    {
        char h[4];
        int p;
        CHECK(rfm1cs_tcp_parse("move.local", 0, h, (int)sizeof h, &p) == -1);   /* (too long for host[]) */
    }
    CHECK(rfm1cs_tcp_connect("move.local:nope", 0) == -1);
    connects("127.0.0.1:%d", 0);
    connects("[::1]:%d", 1);
    return check_done("tcp");
}
