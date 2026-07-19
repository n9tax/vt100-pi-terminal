// See netlink.h. Thin TCP client wrapped around the ported Telnet filter.
#include "net/netlink.h"
#include "net/telnet.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

static int sock_fd = -1;
static void (*emit_cb)(uint8_t) = 0;

void netlink_set_emit(void (*emit)(uint8_t byte)) { emit_cb = emit; }

int netlink_fd(void)        { return sock_fd; }
int netlink_connected(void) { return sock_fd >= 0; }

// Telnet filter callbacks: clean data -> emit; negotiation replies -> socket.
static void tn_emit(uint8_t b) { if (emit_cb) emit_cb(b); }
static void tn_reply(const uint8_t *buf, uint32_t len) {
    if (sock_fd < 0) return;
    for (uint32_t off = 0; off < len; ) {
        ssize_t n = send(sock_fd, buf + off, len - off, MSG_NOSIGNAL);
        if (n > 0) { off += (uint32_t)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        break;
    }
}

void netlink_close(void) {
    if (sock_fd >= 0) close(sock_fd);
    sock_fd = -1;
}

int netlink_connect_telnet(const char *host, int port) {
    netlink_close();
    if (!host || !host[0]) return -1;

    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port > 0 ? port : 23);

    struct addrinfo hints, *res = NULL, *ai;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
        fprintf(stderr, "netlink: cannot resolve %s\n", host);
        return -1;
    }

    int fd = -1;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        fcntl(fd, F_SETFL, O_NONBLOCK);                  // non-blocking connect w/ timeout
        int r = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (r == 0) break;                               // connected immediately
        if (r < 0 && errno == EINPROGRESS) {
            struct pollfd pf = { fd, POLLOUT, 0 };
            if (poll(&pf, 1, 5000) > 0 && (pf.revents & POLLOUT)) {
                int err = 0; socklen_t el = sizeof err;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
                if (err == 0) break;                     // connected
            }
        }
        close(fd);                                       // failed / timed out
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        fprintf(stderr, "netlink: connect to %s:%s failed: %s\n", host, portstr, strerror(errno));
        return -1;
    }

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    fcntl(fd, F_SETFL, O_NONBLOCK);   // non-blocking reads in netlink_poll()

    sock_fd = fd;
    telnet_init(tn_emit, tn_reply);
    fprintf(stderr, "netlink: connected to %s:%s\n", host, portstr);
    return 0;
}

void netlink_poll(void) {
    if (sock_fd < 0) return;
    uint8_t buf[1024];
    for (;;) {
        ssize_t n = read(sock_fd, buf, sizeof buf);
        if (n > 0) { telnet_rx(buf, (uint32_t)n); continue; }
        if (n == 0) { netlink_close(); return; }              // server closed
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        netlink_close();                                       // real error
        return;
    }
}

void netlink_write(const uint8_t *buf, uint32_t len) {
    if (sock_fd < 0) return;
    // Telnet: a literal 0xFF byte must be doubled (IAC IAC).
    uint8_t out[128];
    uint32_t o = 0;
    for (uint32_t i = 0; i < len; ++i) {
        if (o + 2 > sizeof out) { tn_reply(out, o); o = 0; }
        out[o++] = buf[i];
        if (buf[i] == 0xFF) out[o++] = 0xFF;
    }
    if (o) tn_reply(out, o);
}
