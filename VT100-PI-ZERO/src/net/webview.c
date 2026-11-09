// See webview.h. A deliberately tiny HTTP/1.1 server: three GET routes, no
// keep-alive except the event stream, everything non-blocking in the main loop.
//
//   GET /         the viewer page (web/index.html, compiled in)
//   GET /atlas    the glyph atlas: every slot's coverage bitmap, back to back
//   GET /events   text/event-stream of screen updates (JSON, one per tick)
//
// Updates are diffs against a shadow of what was last broadcast: changed rows
// (base64 of the raw cell_t bytes), smooth slides and whole-screen shifts (so a
// scroll costs one small message, not 24 rows), and the renderer state from
// textmode_get_view(). A new browser, or one that fell behind, gets a full
// snapshot of the shadow and then the same diffs as everyone else.
#define _GNU_SOURCE   // MSG_NOSIGNAL, accept4
#include "net/webview.h"
#include "video/textmode.h"
#include "video/glyphs.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

extern const unsigned char web_index_html[];   // generated from web/index.html
extern const unsigned int  web_index_html_len;

#define REQ_MAX      2048
#define SSE_OUT_MAX  (512 * 1024)   // a viewer this far behind is resynced instead
#define TICK_MS      15             // at most ~60 updates/s
#define PING_MS      15000          // keep idle streams (and proxies) alive
#define REQ_TIMEOUT  10000          // a client that never finishes its request

enum { C_FREE, C_REQ, C_SSE, C_FILE };   // C_FILE: close once the response drains

typedef struct {
    int       kind;
    int       fd;
    char      req[REQ_MAX];
    int       reqlen;
    char     *out;
    size_t    len, off, cap;
    int       stale;                // SSE: updates dropped; resync once drained
    long long t0;
} client_t;

static client_t cl[WEB_MAX_CLIENTS];
static int lfd = -1;
static int want_on, want_port;      // as configured
static int bound_port;              // port the listener is on (0 = none)
static int listen_errno;            // why the last bind failed

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// ---- growable output -------------------------------------------------------
typedef struct { char *p; size_t len, cap; } sbuf_t;

static void sb_reserve(sbuf_t *b, size_t more) {
    if (b->len + more <= b->cap) return;
    size_t cap = b->cap ? b->cap : 4096;
    while (cap < b->len + more) cap *= 2;
    char *np = realloc(b->p, cap);
    if (!np) { fprintf(stderr, "webview: out of memory\n"); exit(1); }
    b->p = np;
    b->cap = cap;
}
static void sb_add(sbuf_t *b, const void *s, size_t n) {
    sb_reserve(b, n);
    memcpy(b->p + b->len, s, n);
    b->len += n;
}
static void sb_str(sbuf_t *b, const char *s) { sb_add(b, s, strlen(s)); }
static void sb_printf(sbuf_t *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sb_printf(sbuf_t *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    sb_reserve(b, (size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(b->p + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}
static void sb_base64(sbuf_t *b, const uint8_t *src, size_t n) {
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    sb_reserve(b, (n + 2) / 3 * 4);
    char *o = b->p + b->len;
    size_t i = 0;
    for (; i + 2 < n; i += 3) {
        uint32_t v = (uint32_t)src[i] << 16 | (uint32_t)src[i + 1] << 8 | src[i + 2];
        *o++ = tab[v >> 18]; *o++ = tab[(v >> 12) & 63]; *o++ = tab[(v >> 6) & 63]; *o++ = tab[v & 63];
    }
    if (i < n) {
        uint32_t v = (uint32_t)src[i] << 16 | (i + 1 < n ? (uint32_t)src[i + 1] << 8 : 0);
        *o++ = tab[v >> 18]; *o++ = tab[(v >> 12) & 63];
        *o++ = i + 1 < n ? tab[(v >> 6) & 63] : '=';
        *o++ = '=';
    }
    b->len = (size_t)(o - b->p);
}

// ---- clients ---------------------------------------------------------------
static void client_close(client_t *c) {
    if (c->fd >= 0) close(c->fd);
    free(c->out);
    memset(c, 0, sizeof *c);
    c->fd = -1;
    c->kind = C_FREE;
}

static void client_flush(client_t *c) {
    while (c->off < c->len) {
        ssize_t n = send(c->fd, c->out + c->off, c->len - c->off, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) { c->off += (size_t)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
        client_close(c);   // peer gone
        return;
    }
    c->off = c->len = 0;
    if (c->kind == C_FILE) client_close(c);
}

// Queue bytes. An event stream that cannot keep up stops receiving diffs and is
// marked stale; it gets a fresh snapshot once its backlog has gone out.
static void client_queue(client_t *c, const void *s, size_t n) {
    if (c->kind == C_SSE && (c->stale || c->len - c->off + n > SSE_OUT_MAX)) {
        c->stale = 1;
        return;
    }
    if (c->off && c->off == c->len) c->off = c->len = 0;
    if (c->len + n > c->cap) {
        size_t cap = c->cap ? c->cap : 8192;
        while (cap < c->len + n) cap *= 2;
        char *np = realloc(c->out, cap);
        if (!np) { client_close(c); return; }
        c->out = np;
        c->cap = cap;
    }
    memcpy(c->out + c->len, s, n);
    c->len += n;
}

static int sse_clients(void) {
    int n = 0;
    for (int i = 0; i < WEB_MAX_CLIENTS; ++i) n += cl[i].kind == C_SSE;
    return n;
}

// ---- shadow (what the browsers have) --------------------------------------
static cell_t    sh_cells[TERM_ROWS][TERM_COLS];
static tm_view_t sh_view;
static int       have_shadow;
static long long last_tick, last_ping;

static void shadow_capture(void) {
    memcpy(sh_cells, tm_cells, sizeof sh_cells);
    textmode_get_view(&sh_view);
    have_shadow = 1;
}

static void put_state(sbuf_t *b, const tm_view_t *v) {
    sb_str(b, "\"st\":{\"pal\":[");
    for (int i = 0; i < 16; ++i) sb_printf(b, "%s%u", i ? "," : "", (unsigned)v->pal[i]);
    sb_str(b, "],\"ln\":[");
    for (int r = 0; r < TERM_ROWS; ++r) sb_printf(b, "%s%u", r ? "," : "", v->line[r]);
    sb_printf(b, "],\"rv\":%d,\"fl\":%d,\"bk\":%d,\"cs\":%d,\"cu\":[%d,%d],\"pps\":%d}",
              v->reverse, v->flash, v->blink, v->cursor_style, v->cur_row, v->cur_col, v->pps);
}

static int state_differs(const tm_view_t *a, const tm_view_t *b) {
    return memcmp(a->pal, b->pal, sizeof a->pal) || memcmp(a->line, b->line, sizeof a->line) ||
           a->reverse != b->reverse || a->flash != b->flash || a->blink != b->blink ||
           a->cursor_style != b->cursor_style || a->cur_row != b->cur_row ||
           (a->cur_row >= 0 && a->cur_col != b->cur_col) || a->pps != b->pps;
}

static void put_row(sbuf_t *b, int r, int *first) {
    sb_printf(b, "%s[%d,\"", *first ? "" : ",", r);
    sb_base64(b, (const uint8_t *)sh_cells[r], sizeof sh_cells[r]);
    sb_str(b, "\"]");
    *first = 0;
}

// Full picture for one browser: geometry (it fetches the atlas from that),
// then every row and the whole state. Built from the shadow, so the diffs that
// follow apply on top of it.
static void send_snapshot(client_t *c) {
    if (!have_shadow) shadow_capture();
    int w, h, lh;
    textmode_geometry(&w, &h, &lh);
    sbuf_t b = { 0 };
    sb_printf(&b, "data: {\"hello\":{\"w\":%d,\"h\":%d,\"lh\":%d,\"cols\":%d,\"rows\":%d,"
                  "\"aw\":%d,\"ah\":%d,\"ng\":%d,\"gen\":%u,\"exact\":%d},",
              w, h, lh, TERM_COLS, TERM_ROWS, glyph_atlas_w(), glyph_atlas_h(),
              GLYPH_DEC_BASE + GLYPH_DEC_COUNT, sh_view.atlas_gen, sh_view.exact_rows);
    sb_str(&b, "\"rows\":[");
    int first = 1;
    for (int r = 0; r < TERM_ROWS; ++r) put_row(&b, r, &first);
    sb_str(&b, "],");
    put_state(&b, &sh_view);
    sb_str(&b, "}\n\n");
    c->stale = 0;
    client_queue(c, b.p, b.len);
    free(b.p);
    client_flush(c);
}

static void broadcast(const sbuf_t *b) {
    for (int i = 0; i < WEB_MAX_CLIENTS; ++i) {
        client_t *c = &cl[i];
        if (c->kind != C_SSE) continue;
        client_queue(c, b->p, b->len);
        if (c->kind == C_SSE) client_flush(c);
    }
}

static int rows_equal(cell_t *a, cell_t *b) { return memcmp(a, b, sizeof(cell_t) * TERM_COLS) == 0; }

// Shift the shadow's rows up by k, as the screen just did, leaving the bottom k
// rows unmatchable so the diff resends them.
static void shadow_shift(int k) {
    if (k > TERM_ROWS) k = TERM_ROWS;
    memmove(sh_cells[0], sh_cells[k], sizeof(sh_cells[0]) * (size_t)(TERM_ROWS - k));
    memset(sh_cells[TERM_ROWS - k], 0xff, sizeof(sh_cells[0]) * (size_t)k);
}

// A jump scroll (smooth off, or a scroll region) shows up as rows reappearing k
// higher. Finding k turns "resend 24 rows" into "shift 1, resend the new one".
static int detect_shift(void) {
    int same = 0;
    for (int r = 0; r < TERM_ROWS; ++r) same += rows_equal(tm_cells[r], sh_cells[r]);
    if (TERM_ROWS - same < 3) return 0;
    int best = 0, best_n = same;
    for (int k = 1; k < TERM_ROWS - 1; ++k) {
        int n = 0;
        for (int r = 0; r + k < TERM_ROWS; ++r) n += rows_equal(tm_cells[r], sh_cells[r + k]);
        if (n > best_n + 2) { best = k; best_n = n; }
    }
    return best;
}

void webview_tick(void) {
    if (!sse_clients()) { have_shadow = 0; return; }
    long long t = now_ms();
    if (t - last_tick < TICK_MS) return;
    last_tick = t;
    if (!have_shadow) shadow_capture();

    tm_view_t v;
    textmode_get_view(&v);

    // A new font: every browser needs the new atlas, so start them over.
    if (v.atlas_gen != sh_view.atlas_gen) {
        shadow_capture();
        for (int i = 0; i < WEB_MAX_CLIENTS; ++i)
            if (cl[i].kind == C_SSE) { cl[i].stale = 1; if (cl[i].off == cl[i].len) send_snapshot(&cl[i]); }
        return;
    }

    sbuf_t b = { 0 };
    sb_str(&b, "data: {");
    int any = 0;

    // Smooth slides first (the browser animates these at the same speed), else
    // a plain jump shift if one explains the change.
    int k = (int)(v.slides - sh_view.slides);
    if (k > 0) {
        shadow_shift(k);
        sb_printf(&b, "\"k\":%d,\"sm\":1,\"pan\":%d,", k, v.pan);
        any = 1;
    } else if ((k = detect_shift()) > 0) {
        shadow_shift(k);
        sb_printf(&b, "\"k\":%d,\"sm\":0,", k);
        any = 1;
    } else if (v.pan != sh_view.pan && v.pan == 0) {
        sb_str(&b, "\"pan\":0,");   // the slide settled (or snapped): catch up
        any = 1;
    }

    int first = 1;
    for (int r = 0; r < TERM_ROWS; ++r) {
        if (rows_equal(tm_cells[r], sh_cells[r])) continue;
        memcpy(sh_cells[r], tm_cells[r], sizeof sh_cells[r]);
        if (first) sb_str(&b, "\"rows\":[");
        put_row(&b, r, &first);
    }
    if (!first) { sb_str(&b, "],"); any = 1; }

    if (state_differs(&v, &sh_view)) { put_state(&b, &v); sb_str(&b, ","); any = 1; }
    sh_view = v;

    if (any) {
        b.p[b.len - 1] = '}';   // replaces the trailing comma
        sb_str(&b, "\n\n");
        broadcast(&b);
        last_ping = t;
    } else if (t - last_ping > PING_MS) {
        sbuf_t p = { 0 };
        sb_str(&p, ": ping\n\n");
        broadcast(&p);
        free(p.p);
        last_ping = t;
    }
    free(b.p);
}

// ---- HTTP ------------------------------------------------------------------
static void respond(client_t *c, const char *status, const char *type,
                    const void *body, size_t n) {
    char hdr[256];
    int hn = snprintf(hdr, sizeof hdr,
                      "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                      "Cache-Control: no-cache\r\nConnection: close\r\n\r\n",
                      status, type, n);
    c->kind = C_FILE;
    client_queue(c, hdr, (size_t)hn);
    if (body && n) client_queue(c, body, n);
    client_flush(c);
}

static void send_atlas(client_t *c) {
    int slots = GLYPH_DEC_BASE + GLYPH_DEC_COUNT;
    size_t one = (size_t)glyph_atlas_w() * (size_t)glyph_atlas_h();
    char hdr[256];
    int hn = snprintf(hdr, sizeof hdr,
                      "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                      "Content-Length: %zu\r\nCache-Control: no-cache\r\n"
                      "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
                      one * (size_t)slots);
    c->kind = C_FILE;
    client_queue(c, hdr, (size_t)hn);
    for (int g = 0; g < slots; ++g) client_queue(c, glyph_alpha(g), one);
    client_flush(c);
}

static void handle_request(client_t *c) {
    char method[8], path[256];
    if (sscanf(c->req, "%7s %255s", method, path) != 2) {
        respond(c, "400 Bad Request", "text/plain", "bad request\n", 12);
        return;
    }
    char *q = strchr(path, '?');
    if (q) *q = 0;
    if (strcmp(method, "GET") != 0) {
        respond(c, "405 Method Not Allowed", "text/plain", "GET only\n", 9);
    } else if (!strcmp(path, "/") || !strcmp(path, "/index.html")) {
        respond(c, "200 OK", "text/html; charset=utf-8", web_index_html, web_index_html_len);
    } else if (!strcmp(path, "/atlas")) {
        send_atlas(c);
    } else if (!strcmp(path, "/events")) {
        static const char hdr[] =
            "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\nAccess-Control-Allow-Origin: *\r\n"
            "Connection: keep-alive\r\n\r\nretry: 2000\n\n";
        c->kind = C_SSE;
        client_queue(c, hdr, sizeof hdr - 1);
        send_snapshot(c);
    } else {
        respond(c, "404 Not Found", "text/plain", "not found\n", 10);
    }
}

static void client_read(client_t *c) {
    char buf[1024];
    for (;;) {
        ssize_t n = recv(c->fd, buf, sizeof buf, MSG_DONTWAIT);
        if (n == 0) { client_close(c); return; }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
            client_close(c);
            return;
        }
        if (c->kind != C_REQ) continue;   // nothing more is expected; drain it
        size_t room = sizeof c->req - 1 - (size_t)c->reqlen;
        if ((size_t)n > room) { respond(c, "431 Request Header Fields Too Large", "text/plain", "", 0); return; }
        memcpy(c->req + c->reqlen, buf, (size_t)n);
        c->reqlen += (int)n;
        c->req[c->reqlen] = 0;
        if (strstr(c->req, "\r\n\r\n") || strstr(c->req, "\n\n")) { handle_request(c); return; }
    }
}

static void accept_clients(void) {
    for (;;) {
        int fd = accept4(lfd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) return;
        client_t *c = NULL;
        for (int i = 0; i < WEB_MAX_CLIENTS; ++i) if (cl[i].kind == C_FREE) { c = &cl[i]; break; }
        if (!c) {
            static const char busy[] = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n"
                                       "Connection: close\r\n\r\n";
            ssize_t ignored = send(fd, busy, sizeof busy - 1, MSG_NOSIGNAL | MSG_DONTWAIT);
            (void)ignored;
            close(fd);
            continue;
        }
        memset(c, 0, sizeof *c);
        c->fd = fd;
        c->kind = C_REQ;
        c->t0 = now_ms();
    }
}

// ---- listener --------------------------------------------------------------
static void close_all(void) {
    for (int i = 0; i < WEB_MAX_CLIENTS; ++i) if (cl[i].kind != C_FREE) client_close(&cl[i]);
    if (lfd >= 0) close(lfd);
    lfd = -1;
    bound_port = 0;
    have_shadow = 0;
}

// Dual-stack (IPv6 socket that also takes IPv4) where the kernel has IPv6,
// plain IPv4 otherwise.
static int open_listener(int port) {
    int one = 1, zero = 0;
    int fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd >= 0) {
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof zero);
        struct sockaddr_in6 a6 = { .sin6_family = AF_INET6, .sin6_port = htons((uint16_t)port),
                                   .sin6_addr = IN6ADDR_ANY_INIT };
        if (bind(fd, (struct sockaddr *)&a6, sizeof a6) == 0 && listen(fd, 8) == 0) return fd;
        listen_errno = errno;
        close(fd);
        if (listen_errno == EADDRINUSE || listen_errno == EACCES) return -1;
    }
    fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) { listen_errno = errno; return -1; }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a4 = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port),
                              .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(fd, (struct sockaddr *)&a4, sizeof a4) == 0 && listen(fd, 8) == 0) return fd;
    listen_errno = errno;
    close(fd);
    return -1;
}

void webview_configure(int enable, int port) {
    static int inited;
    if (!inited) {
        for (int i = 0; i < WEB_MAX_CLIENTS; ++i) { cl[i].fd = -1; cl[i].kind = C_FREE; }
        inited = 1;
    }
    want_on = enable;
    want_port = port;
    if (lfd >= 0 && enable && port == bound_port) return;   // already as asked
    close_all();
    if (!enable || port <= 0 || port > 65535) return;
    listen_errno = 0;
    lfd = open_listener(port);
    if (lfd >= 0) {
        bound_port = port;
        fprintf(stderr, "webview: serving the screen on port %d\n", port);
    } else {
        fprintf(stderr, "webview: cannot listen on port %d: %s\n", port, strerror(listen_errno));
    }
}

void webview_fill_pollfds(struct pollfd *p) {
    p[0].fd = lfd;
    p[0].events = POLLIN;
    for (int i = 0; i < WEB_MAX_CLIENTS; ++i) {
        client_t *c = &cl[i];
        p[1 + i].fd = c->kind == C_FREE ? -1 : c->fd;
        p[1 + i].events = POLLIN | (c->off < c->len ? POLLOUT : 0);
    }
}

void webview_service(const struct pollfd *p) {
    if (lfd >= 0 && (p[0].revents & POLLIN)) accept_clients();
    long long t = now_ms();
    for (int i = 0; i < WEB_MAX_CLIENTS; ++i) {
        client_t *c = &cl[i];
        if (c->kind == C_FREE) continue;
        // Only trust revents for the fd that was actually polled: a slot may
        // have been reused by accept_clients() just now.
        short ev = p[1 + i].fd == c->fd ? p[1 + i].revents : 0;
        if (ev & (POLLERR | POLLHUP | POLLNVAL)) { client_close(c); continue; }
        if (ev & POLLIN) client_read(c);
        if (c->kind == C_FREE) continue;
        if (ev & POLLOUT) client_flush(c);
        if (c->kind == C_FREE) continue;
        if (c->kind == C_SSE && c->stale && c->off == c->len) send_snapshot(c);
        if (c->kind == C_REQ && t - c->t0 > REQ_TIMEOUT) client_close(c);
    }
}

void webview_describe(char *out, unsigned n, const char *ip) {
    if (!want_on)                 snprintf(out, n, "off");
    else if (lfd < 0)             snprintf(out, n, "on, but port %d: %s", want_port,
                                           listen_errno == EADDRINUSE ? "in use" : strerror(listen_errno));
    else {
        int v = sse_clients();
        char who[24] = "";
        if (v) snprintf(who, sizeof who, "  (%d viewer%s)", v, v == 1 ? "" : "s");
        snprintf(out, n, "on  http://%s:%d/%s", ip, bound_port, who);
    }
}
