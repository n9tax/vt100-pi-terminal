// On-screen Setup menu. See setup.h. Renders directly into tm_cells and repaints
// via textmode; navigation is Up/Down between fields, Left/Right to change enum
// values, typing to edit text fields, Enter to save+apply, Ctrl+F3 to cancel.
#define _GNU_SOURCE   // getifaddrs
#include "setup.h"
#include "config.h"
#include "settings.h"
#include "video/textmode.h"
#include "video/fonts.h"
#include "video/themes.h"
#include "io/serial_linux.h"
#include "net/hostconn.h"
#include "flow.h"
#include "service.h"

#include <string.h>
#include <stdio.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

enum { F_SHELL, F_SERIAL, F_BAUD, F_SSH, F_TELNET, F_TPORT, F_AUTOREC, F_LINK,
       F_THEME, F_FG, F_BG, F_CURSOR, F_ECHO, F_SMOOTH, F_SPEED, F_DEPTH, F_DUMP,
       F_FONT, F_BOOT, NFIELDS };

// Layout. With 19 fields the menu fills the grid, so the rows are laid out
// explicitly rather than left to drift: title, a live status line, a blank,
// the fields, then two help lines at the bottom.
#define ROW_TITLE  0
#define ROW_STATUS 1
#define ROW_BUFFER 2
#define ROW_FIELD0 3

static int is_text_field(int i) {
    return i == F_SERIAL || i == F_FG || i == F_BG || i == F_SSH || i == F_TELNET;
}

// Action rows do something when Enter lands on them, instead of the usual
// save-and-close. Currently just the manual reconnect.
static int is_action_field(int i) { return i == F_LINK; }

// Scroll-speed increment at a given speed. 10 px/s is a meaningful trim at 150
// and imperceptible at 900, so the step widens with the value -- a constant fine
// step would make crossing the range take forever.
static int speed_step(int v) { return v < 200 ? 10 : (v < 600 ? 25 : 50); }

static int active;
static int sel;
static settings_t work;                     // edited copy
static settings_t orig;                     // snapshot at open (for change detection)
static cell_t saved[TERM_ROWS][TERM_COLS];  // terminal screen behind the menu
static int esc;                             // escape-sequence parser state
static char ip_str[64];                     // this Pi's IP (to ssh into it), shown in header
static int boot_flag;                        // start-at-boot toggle (mirrors systemd state)
static int boot_orig;                        // its value at open, for change detection

static const int  bauds[]  = { 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
#define NBAUDS ((int)(sizeof bauds / sizeof bauds[0]))
static const int  tports[] = { 23, 992, 2323 };
#define NTPORTS ((int)(sizeof tports / sizeof tports[0]))

int setup_active(void) { return active; }

// This Pi's first non-loopback IPv4 address, for ssh-ing into it.
static void find_ip(char *out, size_t n) {
    snprintf(out, n, "%s", "(no network)");
    struct ifaddrs *ifa = NULL, *p;
    if (getifaddrs(&ifa) != 0) return;
    for (p = ifa; p; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        if (p->ifa_flags & IFF_LOOPBACK) continue;
        char ip[INET_ADDRSTRLEN];
        struct sockaddr_in *sa = (struct sockaddr_in *)p->ifa_addr;
        if (inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof ip)) { snprintf(out, n, "%s", ip); break; }
    }
    freeifaddrs(ifa);
}

// ---- rendering -------------------------------------------------------------
static void put(int r, int c, const char *s, uint8_t fg, uint8_t bg, uint8_t attr) {
    for (; *s && c < TERM_COLS; ++c, ++s)
        if (r >= 0 && r < TERM_ROWS && c >= 0)
            tm_cells[r][c] = (cell_t){ (uint8_t)*s, fg, bg, attr };
}

static void field_value(int i, char *out, size_t n) {
    switch (i) {
        case F_SHELL:  snprintf(out, n, "%s", work.local_shell ? "on" : "off"); break;
        case F_BOOT:   snprintf(out, n, "%s", boot_flag ? "on" : "off"); break;
        case F_SERIAL: snprintf(out, n, "%s", work.serial_dev); break;
        case F_BAUD:   snprintf(out, n, "%d", work.baud); break;
        case F_SSH:    snprintf(out, n, "%s", work.ssh_host); break;
        case F_TELNET: snprintf(out, n, "%s", work.telnet_host); break;
        case F_TPORT:  snprintf(out, n, "%d", work.telnet_port); break;
        case F_AUTOREC: snprintf(out, n, "%s", work.auto_reconnect ? "on" : "off"); break;
        case F_LINK:   snprintf(out, n, "%s", hostconn_status()); break;
        case F_DEPTH:  snprintf(out, n, "%d line%s in flight",
                                work.scroll_depth, work.scroll_depth == 1 ? "" : "s"); break;
        case F_DUMP:   snprintf(out, n, "%d lines", work.scroll_dump); break;
        case F_THEME:  snprintf(out, n, "%s", settings_theme_name(work.theme)); break;
        case F_FG:     snprintf(out, n, "%s", work.fg_hex); break;
        case F_BG:     snprintf(out, n, "%s", work.bg_hex); break;
        case F_CURSOR: snprintf(out, n, "%s", work.cursor_style ? "underline" : "block"); break;
        case F_ECHO:   snprintf(out, n, "%s", work.local_echo ? "on" : "off"); break;
        case F_SMOOTH: snprintf(out, n, "%s", work.smooth_scroll ? "on" : "off"); break;
        case F_SPEED:  snprintf(out, n, "%d px/s", work.scroll_speed); break;
        case F_FONT: {
            int idx = fonts_index_of(work.font_path);
            if (idx >= 0) snprintf(out, n, "%s", fonts_name(idx));
            else          snprintf(out, n, "%s (custom)", work.font_path);
            break;
        }
        default:       out[0] = '\0'; break;
    }
}

static void draw(void) {
    static const char *labels[NFIELDS] = {
        "Local shell", "Serial device", "Baud rate", "SSH host", "Telnet host",
        "Telnet port", "Auto reconnect", "Host link", "Theme", "Custom FG",
        "Custom BG", "Cursor", "Local echo", "Smooth scroll", "Scroll speed",
        "Scroll depth", "Overrun limit", "Font", "Start at boot",
    };
    for (int r = 0; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c)
            tm_cells[r][c] = (cell_t){ ' ', 7, 0, 0 };

    // The address and the buffer verdict get a row each: together they run past
    // 80 columns with a long IP and a full status, and put() would silently
    // truncate exactly the over/under counts the line exists to show.
    char hdr[160];
    snprintf(hdr, sizeof hdr, "This Pi: %s   (ssh in here)", ip_str);
    put(ROW_TITLE, 4, "VT100-PI-ZERO  \xC4\xC4  SETUP", 7, 0, ATTR_BOLD);
    put(ROW_STATUS, 4, hdr, 7, 0, 0);
    snprintf(hdr, sizeof hdr, "Buffer: %s", flow_status());
    put(ROW_BUFFER, 4, hdr, 7, 0, 0);

    for (int i = 0; i < NFIELDS; ++i) {
        int r = ROW_FIELD0 + i;
        int selrow = (i == sel);
        char val[300];
        field_value(i, val, sizeof val);
        if (selrow && is_text_field(i)) {
            size_t l = strlen(val);
            if (l + 1 < sizeof val) { val[l] = '_'; val[l + 1] = '\0'; }   // edit caret
        }
        put(r, 4, selrow ? ">" : " ", 7, 0, 0);
        put(r, 6, labels[i], 7, 0, selrow ? ATTR_BOLD : 0);
        put(r, 22, val, selrow ? 0 : 7, selrow ? 7 : 0, 0);   // selected = reverse video
    }

    if (is_action_field(sel))
        put(TERM_ROWS - 1, 4, "Enter: reconnect now            Ctrl+F3: cancel", 7, 0, ATTR_BOLD);
    else
        put(TERM_ROWS - 1, 4, "Enter: save & apply/connect     Ctrl+F3: cancel", 7, 0, ATTR_BOLD);
    if (sel == F_SPEED || sel == F_DEPTH || sel == F_DUMP)
        put(TERM_ROWS - 2, 4, "over = raise speed/limit;  under = raise depth;  R: reset counters", 7, 0, 0);
    else
        put(TERM_ROWS - 2, 4, "Up/Down: field   Left/Right: change   hosts are typed", 7, 0, 0);

    textmode_render_all();
}

// ---- editing ---------------------------------------------------------------
static void move_sel(int d) { sel = (sel + d + NFIELDS) % NFIELDS; }

static void change(int d) {
    switch (sel) {
        case F_BAUD: {
            int idx = 0;
            for (int i = 0; i < NBAUDS; ++i) if (bauds[i] == work.baud) { idx = i; break; }
            idx = (idx + d + NBAUDS) % NBAUDS;
            work.baud = bauds[idx];
            break;
        }
        case F_TPORT: {
            int idx = 0, best = 1 << 30;
            for (int i = 0; i < NTPORTS; ++i) {
                int dd = tports[i] - work.telnet_port; if (dd < 0) dd = -dd;
                if (dd < best) { best = dd; idx = i; }
            }
            idx = (idx + d + NTPORTS) % NTPORTS;
            work.telnet_port = tports[idx];
            break;
        }
        case F_SHELL:  work.local_shell ^= 1; break;
        case F_AUTOREC: work.auto_reconnect ^= 1; break;
        case F_LINK:   break;   // an action row: Enter drives it, not Left/Right
        case F_BOOT:   boot_flag ^= 1; break;
        case F_THEME:  { int nt = themes_count(); work.theme = (work.theme + d + nt) % nt; break; }
        case F_CURSOR: work.cursor_style ^= 1; break;
        case F_ECHO:   work.local_echo ^= 1; break;
        case F_SMOOTH: work.smooth_scroll ^= 1; break;
        case F_SPEED: {
            // Arithmetic stepping rather than a fixed table, so the speed can be
            // trimmed to taste. The step widens with the value: 10 px/s matters
            // at 150 and is imperceptible at 900, and a constant fine step would
            // make crossing the range take forever.
            // Stepping down uses the band the value is leaving, so a step down
            // followed by a step up returns to where it started.
            int v = work.scroll_speed;
            v += d * (d > 0 ? speed_step(v) : speed_step(v - 1));
            if (v < SCROLL_SPEED_MIN) v = SCROLL_SPEED_MIN;
            if (v > SCROLL_SPEED_MAX) v = SCROLL_SPEED_MAX;
            work.scroll_speed = v;
            break;
        }
        case F_DEPTH: {
            int v = work.scroll_depth + d;
            if (v < SCROLL_DEPTH_MIN) v = SCROLL_DEPTH_MIN;
            if (v > SCROLL_DEPTH_MAX) v = SCROLL_DEPTH_MAX;
            work.scroll_depth = v;
            break;
        }
        case F_DUMP: {
            int v = work.scroll_dump + d * 10;
            if (v < SCROLL_DUMP_MIN) v = SCROLL_DUMP_MIN;
            if (v > SCROLL_DUMP_MAX) v = SCROLL_DUMP_MAX;
            work.scroll_dump = v;
            break;
        }
        case F_FONT: {
            int n = fonts_count();
            int idx = fonts_index_of(work.font_path);
            if (idx < 0) idx = 0;   // a custom path: cycling drops into the list
            idx = (idx + d + n) % n;
            snprintf(work.font_path, sizeof work.font_path, "%s", fonts_value(idx));
            break;
        }
        default: break;   // text fields: nothing to cycle
    }
}

static void edit_text(uint8_t b) {   // the typed fields: serial device, custom fg/bg
    char  *buf;
    size_t cap;
    int    hexonly = 0;
    switch (sel) {
        case F_FG:     buf = work.fg_hex; cap = sizeof work.fg_hex; hexonly = 1; break;
        case F_BG:     buf = work.bg_hex; cap = sizeof work.bg_hex; hexonly = 1; break;
        case F_SSH:    buf = work.ssh_host; cap = sizeof work.ssh_host; break;
        case F_TELNET: buf = work.telnet_host; cap = sizeof work.telnet_host; break;
        default:       buf = work.serial_dev; cap = sizeof work.serial_dev; break;
    }
    size_t n = strlen(buf);
    if (b == 0x7f || b == 0x08) { if (n > 0) buf[n - 1] = '\0'; return; }   // backspace
    if (hexonly) {   // colour fields accept only # and hex digits
        int ishex = (b >= '0' && b <= '9') || (b >= 'a' && b <= 'f') ||
                    (b >= 'A' && b <= 'F') || b == '#';
        if (!ishex) return;
    }
    if (b >= 0x20 && b < 0x7f && n + 1 < cap) { buf[n] = (char)b; buf[n + 1] = '\0'; }
}

static void restore_screen(void) {
    memcpy(tm_cells, saved, sizeof saved);
    textmode_render_all();
}

// Save and apply. force_reconnect re-dials the host link even when nothing
// about the destination changed -- that is the manual "Host link" action, and
// the reason a dropped session used to need the destination edited and edited
// back before Setup would reconnect it.
static void do_save(int force_reconnect) {
    g_settings = work;
    settings_save();

    // Apply live. Theme/cursor are trivial; serial reopen and font rebuild only
    // when they actually changed.
    textmode_set_custom_colors(settings_parse_hex(work.fg_hex), settings_parse_hex(work.bg_hex));
    textmode_set_theme(work.theme);
    textmode_set_cursor_style(work.cursor_style);
    textmode_set_smooth(work.smooth_scroll, work.scroll_speed);
    textmode_set_scroll_depth(work.scroll_depth);
    if (strcmp(work.serial_dev, orig.serial_dev) != 0 || work.baud != orig.baud)
        serial_reconfigure(work.serial_dev, work.baud);
    if (strcmp(work.font_path, orig.font_path) != 0)
        textmode_reload_font();

    // (Re)connect the host link when asked to, when the destination changed, or
    // when auto-reconnect was just switched on over a link that is already down.
    // hostconn owns the priority chain (local shell > ssh > telnet > serial) and
    // the retry backoff. Prompts appear once the menu closes.
    int dest_changed = work.local_shell != orig.local_shell ||
                       strcmp(work.ssh_host, orig.ssh_host) != 0 ||
                       strcmp(work.telnet_host, orig.telnet_host) != 0 ||
                       work.telnet_port != orig.telnet_port;
    int auto_just_on = work.auto_reconnect && !orig.auto_reconnect && !hostconn_connected();
    if (force_reconnect)
        hostconn_reconnect_now();   // announces itself, so the manual dial is visible
    else if (dest_changed || auto_just_on)
        hostconn_start();

    // Start-at-boot: install/enable or disable the systemd unit when toggled.
    if (boot_flag != boot_orig) {
        if (service_set_boot(boot_flag) != 0)
            boot_flag = boot_orig;   // failed (not root?): revert the shown state
    }

    active = 0;
    textmode_set_chrome(0);
    restore_screen();   // repaints with the now-current theme/font
}

// ---- public ----------------------------------------------------------------
void setup_toggle(void) {
    if (!active) {
        work = g_settings;
        orig = g_settings;
        memcpy(saved, tm_cells, sizeof saved);
        find_ip(ip_str, sizeof ip_str);   // refresh the Pi's IP for the header
        boot_flag = boot_orig = service_boot_enabled();   // reflect real systemd state
        active = 1;
        sel = 0;
        esc = 0;
        textmode_scroll_snap();   // finish any in-flight slide first, or the menu
                                  // renders shifted down by the pending scroll offset
        textmode_set_chrome(1);   // Setup always readable, whatever the theme
        draw();
    } else {
        active = 0;         // second Ctrl+F3 = cancel
        textmode_set_chrome(0);
        restore_screen();
    }
}

void setup_refresh(void) { if (active) draw(); }

void setup_feed(uint8_t b) {
    if (!active) return;

    if (esc == 2) {         // after ESC [
        switch (b) {
            case 'A': move_sel(-1); break;   // up
            case 'B': move_sel(+1); break;   // down
            case 'C': change(+1);   break;   // right
            case 'D': change(-1);   break;   // left
            default: break;
        }
        esc = 0;
        draw();
        return;
    }
    if (esc == 1) {                          // after ESC
        if (b == '[') { esc = 2; return; }
        esc = 0;                             // stray ESC; fall through
    }
    if (b == 0x1b) { esc = 1; return; }
    if (b == '\r' || b == '\n') { do_save(is_action_field(sel)); return; }
    if (b == '\t') { move_sel(+1); draw(); return; }
    if ((b == 'r' || b == 'R') && !is_text_field(sel)) {   // reset the flow counters
        flow_reset();
        draw();
        return;
    }

    if (is_text_field(sel)) edit_text(b);
    draw();
}
