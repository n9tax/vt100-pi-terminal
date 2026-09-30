// See hostconn.h.
#include "net/hostconn.h"
#include "net/netlink.h"
#include "io/host_link.h"
#include "io/serial_linux.h"
#include "settings.h"
#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void (*emit_cb)(uint8_t) = 0;

static int       linkup;            // last observed state, for edge detection
static long long retry_at = -1;     // when to re-dial; -1 = no retry pending
static int       retry_delay;       // current backoff, ms (0 = not backed off yet)
static long long up_since;          // when the current link came up
static char      status[256] = "serial";   // room for a long ssh dest plus the prefix

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void hostconn_set_emit(void (*emit)(uint8_t byte)) {
    emit_cb = emit;
    netlink_set_emit(emit);
}

int hostconn_connected(void) { return netlink_connected(); }

int hostconn_fd(void) { return netlink_fd(); }

int hostconn_have_target(void) {
    return g_settings.local_shell || g_settings.ssh_host[0] || g_settings.telnet_host[0];
}

// A status/error line, printed into the terminal the way a telnet client does.
// CRLF-terminated because the host link is gone and nothing else will supply
// the carriage return.
static void note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void note(const char *fmt, ...) {
    if (!emit_cb) return;
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    emit_cb('\r'); emit_cb('\n');
    for (const char *p = line; *p; ++p) emit_cb((uint8_t)*p);
    emit_cb('\r'); emit_cb('\n');
}

// What the settings currently point at, in words.
static void describe(char *out, size_t n) {
    if (g_settings.local_shell)         snprintf(out, n, "local shell");
    else if (g_settings.ssh_host[0] && g_settings.ssh_port != 22)
                                        snprintf(out, n, "ssh %s:%d", g_settings.ssh_host,
                                                 g_settings.ssh_port);
    else if (g_settings.ssh_host[0])    snprintf(out, n, "ssh %s", g_settings.ssh_host);
    else if (g_settings.telnet_host[0]) snprintf(out, n, "telnet %s:%d",
                                                 g_settings.telnet_host, g_settings.telnet_port);
    else                                snprintf(out, n, "serial %s @%d",
                                                 g_settings.serial_dev, g_settings.baud);
}

static void update_status(void) {
    char tgt[160];
    describe(tgt, sizeof tgt);
    if (netlink_connected())
        snprintf(status, sizeof status, "connected: %s", tgt);
    else if (!hostconn_have_target())
        snprintf(status, sizeof status, "%s", tgt);
    else if (retry_at >= 0) {
        long long left = retry_at - now_ms();
        if (left < 0) left = 0;
        snprintf(status, sizeof status, "closed - retry in %llds: %s", left / 1000 + 1, tgt);
    } else
        snprintf(status, sizeof status, "closed: %s", tgt);
}

// Grow the backoff and arm the retry timer. No-op when auto-reconnect is off or
// nothing is configured, which leaves the link down for the manual button.
static void schedule_retry(void) {
    if (!g_settings.auto_reconnect || !hostconn_have_target()) { retry_at = -1; return; }
    retry_delay = retry_delay ? retry_delay * 2 : RECONNECT_DELAY_MIN_MS;
    if (retry_delay > RECONNECT_DELAY_MAX_MS) retry_delay = RECONNECT_DELAY_MAX_MS;
    retry_at = now_ms() + retry_delay;
}

// One dial attempt against the configured destination. Writes go to the new
// link on success and stay on serial on failure, so the RS232 host link is
// always the fallback.
static int dial(void) {
    netlink_close();
    int ok = 0;
    if (g_settings.local_shell)              ok = (netlink_connect_shell() == 0);
    else if (g_settings.ssh_host[0])         ok = (netlink_connect_ssh(g_settings.ssh_host,
                                                                       g_settings.ssh_port) == 0);
    else if (g_settings.telnet_host[0])      ok = (netlink_connect_telnet(g_settings.telnet_host,
                                                                          g_settings.telnet_port) == 0);
    host_link_set_write_fn(ok ? netlink_write : serial_write);
    linkup = ok;
    if (ok) { up_since = now_ms(); retry_at = -1; }
    return ok ? 0 : -1;
}

void hostconn_start(void) {
    retry_at = -1;
    retry_delay = 0;

    if (!hostconn_have_target()) {          // serial-only: tear any link down
        netlink_close();
        linkup = 0;
        host_link_set_write_fn(serial_write);
        update_status();
        return;
    }

    char tgt[160];
    describe(tgt, sizeof tgt);
    if (dial() != 0) {
        note("[vt100-pi] %s: connect failed.", tgt);
        schedule_retry();
        if (retry_at >= 0) note("[vt100-pi] retrying in %ds...", retry_delay / 1000);
    }
    update_status();
}

void hostconn_reconnect_now(void) {
    if (!hostconn_have_target()) { hostconn_start(); return; }
    retry_delay = 0;                       // a manual dial starts the backoff over
    retry_at = -1;
    linkup = 0;                            // this teardown is deliberate: no "[link closed]"
    char tgt[160];
    describe(tgt, sizeof tgt);
    note("[vt100-pi] connecting to %s...", tgt);
    if (dial() != 0) {
        note("[vt100-pi] connect failed.");
        schedule_retry();
    }
    update_status();
}

void hostconn_poll(void) {
    if (netlink_connected()) {
        netlink_poll();                    // may close the link on EOF/error
        if (netlink_connected()) { update_status(); return; }
    }

    if (linkup) {                          // falling edge: the session just died
        linkup = 0;
        host_link_set_write_fn(serial_write);
        // Only a link that stayed up counts as healthy; one that dies on contact
        // (refused login, wrong port) keeps its backoff so we don't hammer.
        if (now_ms() - up_since >= RECONNECT_STABLE_MS) retry_delay = 0;
        note("[vt100-pi] connection closed.");
        schedule_retry();
        if (retry_at >= 0)
            note("[vt100-pi] reconnecting in %ds...", retry_delay / 1000);
        else if (hostconn_have_target())
            note("[vt100-pi] Ctrl+F3 -> Host link -> Enter to reconnect.");
    }

    if (retry_at >= 0 && now_ms() >= retry_at) {
        char tgt[160];
        describe(tgt, sizeof tgt);
        retry_at = -1;
        if (dial() == 0) {
            note("[vt100-pi] connected: %s", tgt);
        } else {
            schedule_retry();
            if (retry_at >= 0) note("[vt100-pi] %s unreachable, retrying in %ds...",
                                    tgt, retry_delay / 1000);
        }
    }

    update_status();
}

const char *hostconn_status(void) { return status; }
