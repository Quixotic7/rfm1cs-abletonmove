/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RemoteFM1 contributors */
/* rfm1cs_tcp: a small non-blocking TCP client for a session's transport on POSIX hosts (macOS, iOS, Android, Linux).
 * Optional: the session never calls it. A native host's network thread does
 *
 *   fd = rfm1cs_tcp_connect("move.local", 0)           (0: Move Remote's port, 7720; resolving may block a moment)
 *   rfm1cs_tcp_wait(fd, ms) == 1                         -> rfm1cs_session_opened()
 *   n = rfm1cs_tcp_read(fd, buf, cap)                    -> rfm1cs_session_rx() (n > 0), _closed() (n < 0)
 *   RFM1CS_EV_SEND: rfm1cs_tcp_write(fd, data, len)      (a failure: _closed())
 *   RFM1CS_EV_CLOSE: rfm1cs_tcp_close(fd)
 *
 * and passes the bytes to the thread that owns the session. */
#ifndef RFM1CS_POSIX_TCP_H
#define RFM1CS_POSIX_TCP_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* a socket with its connect started (host: a name or an address; port 0: 7720): the fd, or -1 (errno; no address) */
int rfm1cs_tcp_connect(const char *host, int port);
/* wait up to ms for the connect: 1 connected, 0 not yet, -1 failed */
int rfm1cs_tcp_wait(int fd, int ms);
/* what has arrived (waiting up to ms for something): > 0 bytes, 0 nothing yet, -1 closed or an error */
int rfm1cs_tcp_read(int fd, uint8_t *buf, int cap, int ms);
/* all of it (waiting up to ~1 s for room): 0 sent, -1 an error */
int rfm1cs_tcp_write(int fd, const uint8_t *b, int n);
void rfm1cs_tcp_close(int fd);

#ifdef __cplusplus
}
#endif
#endif
