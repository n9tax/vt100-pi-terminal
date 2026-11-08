// See netlink.h. Two modes: a TCP client wrapped around the ported Telnet
// filter, or the system `ssh` client run over a PTY (its master fd is the host
// link, carrying raw terminal bytes). Both present the same read/write surface.
#define _GNU_SOURCE   // posix_openpt/grantpt/unlockpt/ptsname, MSG_NOSIGNAL, TIOCSCTTY
#include "net/netlink.h"
#include "net/telnet.h"
#include "config.h"     // TERM_COLS / TERM_ROWS for the PTY window size

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <pwd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

static int   fd = -1;                                 // socket or PTY master
static pid_t child_pid = -1;                          // ssh child (M_SSH only)
static enum { M_NONE, M_TELNET, M_PTY } mode = M_NONE;   // M_PTY: raw pty (ssh or shell)
static void (*emit_cb)(uint8_t) = 0;

void netlink_set_emit(void (*emit)(uint8_t byte)) { emit_cb = emit; }

int netlink_fd(void)        { return fd; }
int netlink_connected(void) { return fd >= 0; }

// Non-blocking-tolerant raw write to the active fd (socket or PTY master).
static void raw_send(const uint8_t *buf, uint32_t len) {
    if (fd < 0) return;
    for (uint32_t off = 0; off < len; ) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n > 0) { off += (uint32_t)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
        break;
    }
}

// Telnet filter callbacks: clean data -> emit; negotiation replies -> fd.
static void tn_emit(uint8_t b) { if (emit_cb) emit_cb(b); }
static void tn_reply(const uint8_t *buf, uint32_t len) { raw_send(buf, len); }

void netlink_close(void) {
    if (fd >= 0) close(fd);   // closing the PTY master hangs up ssh's tty
    fd = -1;
    if (child_pid > 0) {
        kill(child_pid, SIGHUP);
        for (int i = 0; i < 20; ++i) {
            if (waitpid(child_pid, NULL, WNOHANG) == child_pid) { child_pid = -1; break; }
            usleep(10000);
        }
        if (child_pid > 0) { kill(child_pid, SIGKILL); waitpid(child_pid, NULL, 0); child_pid = -1; }
    }
    mode = M_NONE;
}

// ---- Telnet (TCP) ----------------------------------------------------------
int netlink_connect_telnet(const char *host, int port) {
    netlink_close();
    if (!host || !host[0]) return -1;
    signal(SIGPIPE, SIG_IGN);

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

    int s = -1;
    int last_err = 0;                                    // why the last address failed
    for (ai = res; ai; ai = ai->ai_next) {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s < 0) { last_err = errno; continue; }
        fcntl(s, F_SETFL, O_NONBLOCK);                   // non-blocking connect w/ timeout
        int r = connect(s, ai->ai_addr, ai->ai_addrlen);
        if (r == 0) break;
        if (r < 0 && errno == EINPROGRESS) {
            struct pollfd pf = { s, POLLOUT, 0 };
            int pr = poll(&pf, 1, 5000);
            if (pr > 0 && (pf.revents & POLLOUT)) {
                int err = 0; socklen_t el = sizeof err;
                getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &el);
                if (err == 0) break;
                last_err = err;                          // the real refusal/unreachable
            } else {
                last_err = (pr == 0) ? ETIMEDOUT : errno;
            }
        } else {
            last_err = errno;
        }
        close(s);
        s = -1;
    }
    freeaddrinfo(res);
    if (s < 0) {
        // Report the error the connect actually failed with. errno at this point
        // is a stale EINPROGRESS from the non-blocking connect, so it would
        // otherwise log "Operation now in progress" for every failure.
        fprintf(stderr, "netlink: connect to %s:%s failed: %s\n",
                host, portstr, strerror(last_err ? last_err : ECONNREFUSED));
        return -1;
    }

    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

    // Keepalive probes: without them a server that disappears without closing
    // the connection (unplugged cable, crashed host, NAT timeout) leaves us
    // reading a socket that never returns EOF, so the drop is never noticed and
    // the reconnect logic never runs. TCP_KEEP* are Linux-specific; if the
    // kernel rejects any of them the connection still works, just with the
    // system-default (2 hour) idle time.
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);
    int ka_idle = TCP_KEEPALIVE_IDLE_S, ka_intvl = TCP_KEEPALIVE_INTVL_S, ka_cnt = TCP_KEEPALIVE_CNT;
    setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE,  &ka_idle,  sizeof ka_idle);
    setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &ka_intvl, sizeof ka_intvl);
    setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT,   &ka_cnt,   sizeof ka_cnt);

    fcntl(s, F_SETFL, O_NONBLOCK);

    fd = s;
    mode = M_TELNET;
    telnet_init(tn_emit, tn_reply);
    fprintf(stderr, "netlink: telnet %s:%s\n", host, portstr);
    return 0;
}

// ---- PTY-backed links (ssh, local shell) -----------------------------------
// Spawn argv on a fresh PTY; the master fd becomes the raw host link. Returns 0
// on success. Used by both the ssh and local-shell connectors.
static int spawn_pty(char *const argv[]) {
    signal(SIGPIPE, SIG_IGN);
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) || unlockpt(master)) {
        fprintf(stderr, "netlink: posix_openpt failed: %s\n", strerror(errno));
        if (master >= 0) close(master);
        return -1;
    }
    const char *sn = ptsname(master);
    if (!sn) { close(master); return -1; }
    char slave_name[128];
    snprintf(slave_name, sizeof slave_name, "%s", sn);

    struct winsize ws = { .ws_row = TERM_ROWS, .ws_col = TERM_COLS };
    ioctl(master, TIOCSWINSZ, &ws);

    pid_t pid = fork();
    if (pid < 0) { close(master); return -1; }
    if (pid == 0) {
        // Child: give it the slave as its controlling terminal, then exec.
        setsid();
        int slave = open(slave_name, O_RDWR);
        if (slave < 0) _exit(127);
        ioctl(slave, TIOCSCTTY, 0);
        dup2(slave, 0); dup2(slave, 1); dup2(slave, 2);
        if (slave > 2) close(slave);
        close(master);
        setenv("TERM", "xterm-16color", 1);
        execvp(argv[0], argv);
        _exit(127);   // exec failed
    }

    fcntl(master, F_SETFL, O_NONBLOCK);
    fd = master;
    child_pid = pid;
    mode = M_PTY;
    return 0;
}

int netlink_connect_ssh(const char *dest, int port) {
    netlink_close();
    if (!dest || !dest[0]) return -1;
    // ServerAlive* makes ssh give up (and exit, hanging up the PTY) on a host
    // that has gone away, which is what turns a wedged session into a detected
    // drop the reconnect logic can act on. Without it ssh waits forever.
    char alive_int[48], alive_cnt[48];
    snprintf(alive_int, sizeof alive_int, "ServerAliveInterval=%d", SSH_ALIVE_INTERVAL_S);
    snprintf(alive_cnt, sizeof alive_cnt, "ServerAliveCountMax=%d", SSH_ALIVE_COUNT_MAX);
    // -p only for a non-default port: passing -p 22 would override a Port the
    // user set for this host alias in ~/.ssh/config.
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port);
    char *argv[12];
    int a = 0;
    argv[a++] = "ssh";
    argv[a++] = "-o"; argv[a++] = "StrictHostKeyChecking=accept-new";
    argv[a++] = "-o"; argv[a++] = alive_int;
    argv[a++] = "-o"; argv[a++] = alive_cnt;
    if (port > 0 && port != 22) { argv[a++] = "-p"; argv[a++] = portstr; }
    argv[a++] = (char *)dest;
    argv[a] = NULL;
    if (spawn_pty(argv) != 0) return -1;
    fprintf(stderr, "netlink: ssh %s port %s (pid %d)\n", dest, portstr, (int)child_pid);
    return 0;
}

// Who the local shell logs in as. The terminal runs as root (DRM master, raw
// input), but handing root to whoever is at the keyboard is how a `git pull`
// in the clone leaves root-owned objects that break the updater. So: the user
// named in LOCAL_SHELL_USER, else the clone owner install.sh recorded for the
// updater, else the first ordinary account. NULL -> no such user; run the
// shell as ourselves.
static const char *shell_user(char *buf, size_t n) {
    if (LOCAL_SHELL_USER[0]) { snprintf(buf, n, "%s", LOCAL_SHELL_USER); return buf; }

    FILE *f = fopen(UPDATE_CONF_PATH, "r");
    if (f) {
        char line[256];
        buf[0] = 0;
        while (fgets(line, sizeof line, f)) {
            if (strncmp(line, "OWNER=", 6)) continue;
            char *v = line + 6;
            v[strcspn(v, "\r\n")] = 0;
            if (*v == '\'' || *v == '"') { ++v; v[strcspn(v, "'\"")] = 0; }
            snprintf(buf, n, "%s", v);
        }
        fclose(f);
        struct passwd *pw = buf[0] ? getpwnam(buf) : NULL;
        if (pw && pw->pw_uid != 0) return buf;
    }

    // Lowest uid in the normal-user range (1000 on Raspberry Pi OS).
    struct passwd *pw;
    uid_t best = (uid_t)-1;
    setpwent();
    while ((pw = getpwent()))
        if (pw->pw_uid >= 1000 && pw->pw_uid < 65534 && pw->pw_uid < best) {
            best = pw->pw_uid;
            snprintf(buf, n, "%s", pw->pw_name);
        }
    endpwent();
    return best != (uid_t)-1 ? buf : NULL;
}

int netlink_connect_shell(void) {
    netlink_close();
    char user[256];
    const char *who = getuid() == 0 ? shell_user(user, sizeof user) : NULL;
    if (who) {
        // runuser -l: a full login as that user (their shell, HOME, PATH),
        // keeping TERM. No password: we are root.
        char *argv[] = { "runuser", "-l", (char *)who, NULL };
        if (spawn_pty(argv) != 0) return -1;
        fprintf(stderr, "netlink: local shell as %s (pid %d)\n", who, (int)child_pid);
        return 0;
    }
    const char *sh = getenv("SHELL");
    if (!sh || !sh[0]) sh = "/bin/bash";
    char *argv[] = { (char *)sh, "-l", NULL };   // login shell on the Pi
    if (spawn_pty(argv) != 0) return -1;
    fprintf(stderr, "netlink: local shell %s (pid %d)\n", sh, (int)child_pid);
    return 0;
}

void netlink_poll(void) {
    if (fd < 0) return;
    uint8_t buf[1024];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n > 0) {
            if (mode == M_TELNET) telnet_rx(buf, (uint32_t)n);
            else if (emit_cb) for (ssize_t i = 0; i < n; ++i) emit_cb(buf[i]);
            continue;
        }
        if (n == 0) { netlink_close(); return; }                 // EOF / hangup
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (n < 0 && errno == EINTR) continue;
        netlink_close();                                          // EIO (ssh gone) / error
        return;
    }
}

void netlink_write(const uint8_t *buf, uint32_t len) {
    if (fd < 0) return;
    if (mode != M_TELNET) { raw_send(buf, len); return; }   // ssh: raw
    // Telnet: a literal 0xFF byte must be doubled (IAC IAC).
    uint8_t out[128];
    uint32_t o = 0;
    for (uint32_t i = 0; i < len; ++i) {
        if (o + 2 > sizeof out) { raw_send(out, o); o = 0; }
        out[o++] = buf[i];
        if (buf[i] == 0xFF) out[o++] = 0xFF;
    }
    if (o) raw_send(out, o);
}
