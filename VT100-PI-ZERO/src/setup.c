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
#include "autospeed.h"
#include "service.h"
#include "update.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

enum { F_SHELL, F_SERIAL, F_BAUD, F_FORMAT, F_XON, F_SSH, F_SPORT, F_TELNET, F_TPORT, F_AUTOREC, F_LINK,
       F_THEME, F_FG, F_BG, F_CURSOR, F_ECHO, F_SMOOTH, F_SPEED, F_AUTOSPD,
       F_AVG, F_DEPTH, F_DUMP, F_FONT, F_BOOT, F_UPDATE, NFIELDS };

// Layout: title, address, buffer line, the field list, one help line on the
// last row. There are more fields than rows now, so the list scrolls to keep the
// selection visible rather than being paginated -- it stays one flat list, and
// it keeps working however many settings get added.
#define ROW_TITLE  0
#define ROW_STATUS 1
#define ROW_BUFFER 2
#define ROW_FIELD0 3
#define FIELD_ROWS (TERM_ROWS - ROW_FIELD0 - 1)
static int first_vis;   // index of the field drawn on ROW_FIELD0

static int is_text_field(int i) {
    return i == F_SERIAL || i == F_FG || i == F_BG || i == F_SSH || i == F_SPORT ||
           i == F_TELNET;
}

// Action rows do something when Enter lands on them, instead of the usual
// save-and-close: the manual reconnect, and the update check/install.
static int is_action_field(int i) { return i == F_LINK || i == F_UPDATE; }


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

static long long now_ms_setup(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

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
        case F_SERIAL:
            // Flag a port that isn't actually open, so a typo or an unenabled
            // UART is visible here instead of only in the journal.
            if (!serial_connected() && strcmp(work.serial_dev, g_settings.serial_dev) == 0)
                snprintf(out, n, "%s  (not open)", work.serial_dev);
            else
                snprintf(out, n, "%s", work.serial_dev);
            break;
        case F_BAUD:   snprintf(out, n, "%d", work.baud); break;
        case F_FORMAT: snprintf(out, n, "%s", settings_serial_format_name(work.serial_format)); break;
        case F_XON:    snprintf(out, n, "%s", work.xon_xoff ? "on" : "off"); break;
        case F_SSH:    snprintf(out, n, "%s", work.ssh_host); break;
        case F_SPORT:  if (work.ssh_port > 0) snprintf(out, n, "%d", work.ssh_port);
                       else out[0] = '\0';   // cleared mid-edit; saves as 22
                       break;
        case F_TELNET: snprintf(out, n, "%s", work.telnet_host); break;
        case F_TPORT:  snprintf(out, n, "%d", work.telnet_port); break;
        case F_AUTOREC: snprintf(out, n, "%s", work.auto_reconnect ? "on" : "off"); break;
        case F_LINK:   snprintf(out, n, "%s", hostconn_status()); break;
        case F_UPDATE: update_describe(out, n); break;
        case F_DEPTH:  snprintf(out, n, "%d line%s in flight",
                                work.scroll_depth, work.scroll_depth == 1 ? "" : "s"); break;
        case F_DUMP:   snprintf(out, n, "%d lines", work.scroll_dump); break;
        case F_THEME:  snprintf(out, n, "%s", settings_theme_name(work.theme)); break;
        case F_FG:     snprintf(out, n, "%s", work.fg_hex); break;
        case F_BG:     snprintf(out, n, "%s", work.bg_hex); break;
        case F_CURSOR: snprintf(out, n, "%s", work.cursor_style ? "underline" : "block"); break;
        case F_ECHO:   snprintf(out, n, "%s", work.local_echo ? "on" : "off"); break;
        case F_SMOOTH: snprintf(out, n, "%s", work.smooth_scroll ? "on" : "off"); break;
        case F_SPEED:
            // Under auto speed the configured value is only the starting point,
            // so show what the controller has actually settled on.
            if (work.auto_speed && autospeed_enabled())
                snprintf(out, n, "%d px/s  (auto: %d)", work.scroll_speed, autospeed_pps());
            else
                snprintf(out, n, "%d px/s", work.scroll_speed);
            break;
        case F_AUTOSPD: snprintf(out, n, "%s", work.auto_speed ? "on" : "off"); break;
        case F_AVG:    snprintf(out, n, "%d ms", work.speed_avg_ms); break;
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
        "Local shell", "Serial device", "Baud rate", "Serial format", "XON/XOFF", "SSH host", "SSH port", "Telnet host",
        "Telnet port", "Auto reconnect", "Host link", "Theme", "Custom FG",
        "Custom BG", "Cursor", "Local echo", "Smooth scroll", "Scroll speed",
        "Auto speed", "Speed averaging", "Scroll depth", "Overrun limit", "Font",
        "Start at boot", "Updates",
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

    // Scroll the list so the selection is always on screen.
    if (sel < first_vis) first_vis = sel;
    if (sel >= first_vis + FIELD_ROWS) first_vis = sel - FIELD_ROWS + 1;
    if (first_vis > NFIELDS - FIELD_ROWS) first_vis = NFIELDS - FIELD_ROWS;
    if (first_vis < 0) first_vis = 0;

    for (int i = first_vis; i < first_vis + FIELD_ROWS && i < NFIELDS; ++i) {
        int r = ROW_FIELD0 + (i - first_vis);
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

    // Show that the list continues past the visible rows.
    if (first_vis > 0)
        put(ROW_FIELD0, 2, "\x18", 7, 0, 0);                       // up arrow
    if (first_vis + FIELD_ROWS < NFIELDS)
        put(ROW_FIELD0 + FIELD_ROWS - 1, 2, "\x19", 7, 0, 0);      // down arrow

    const char *help;
    char uhelp[TERM_COLS + 1];
    if (sel == F_UPDATE && update_available()) {
        snprintf(uhelp, sizeof uhelp, "New: %s", update_subject());
        help = uhelp;
    } else if (sel == F_UPDATE)
        help = "Enter: check GitHub for updates               Ctrl+F3: cancel";
    else if (is_action_field(sel))
        help = "Enter: reconnect now                          Ctrl+F3: cancel";
    else if (sel == F_SPEED || sel == F_DEPTH || sel == F_DUMP || sel == F_AUTOSPD
             || sel == F_AVG)
        help = "over: raise speed/limit  under: raise depth  R: reset  Enter: save";
    else
        help = "Up/Down: field   Left/Right: change   Enter: save   Ctrl+F3: cancel";
    put(TERM_ROWS - 1, 4, help, 7, 0, ATTR_BOLD);

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
        case F_FORMAT:
            work.serial_format = (work.serial_format + d + SERIAL_FMT_COUNT) % SERIAL_FMT_COUNT;
            break;
        case F_XON:    work.xon_xoff ^= 1; break;
        case F_SHELL:  work.local_shell ^= 1; break;
        case F_AUTOREC: work.auto_reconnect ^= 1; break;
        case F_AUTOSPD: work.auto_speed ^= 1; break;
        case F_AVG: {
            int v = work.speed_avg_ms + d * 250;
            if (v < AUTO_SPEED_AVG_MIN) v = AUTO_SPEED_AVG_MIN;
            if (v > AUTO_SPEED_AVG_MAX) v = AUTO_SPEED_AVG_MAX;
            work.speed_avg_ms = v;
            break;
        }
        case F_LINK:   break;   // action rows: Enter drives them, not Left/Right
        case F_UPDATE: break;
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
            int v = work.scroll_speed + d * SCROLL_SPEED_STEP;
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
    if (sel == F_SPORT) {   // numeric: digits append, backspace drops the last one
        if (b == 0x7f || b == 0x08) work.ssh_port /= 10;
        else if (b >= '0' && b <= '9' && work.ssh_port * 10 + (b - '0') <= 65535)
            work.ssh_port = work.ssh_port * 10 + (b - '0');
        return;
    }
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
    if (work.ssh_port <= 0) work.ssh_port = SSH_PORT_DEFAULT;   // left blank
    g_settings = work;
    settings_save();

    // Apply live. Theme/cursor are trivial; serial reopen and font rebuild only
    // when they actually changed.
    textmode_set_custom_colors(settings_parse_hex(work.fg_hex), settings_parse_hex(work.bg_hex));
    textmode_set_theme(work.theme);
    textmode_set_cursor_style(work.cursor_style);
    textmode_set_smooth(work.smooth_scroll, work.scroll_speed);
    textmode_set_scroll_depth(work.scroll_depth);
    autospeed_set(work.auto_speed, work.scroll_speed, now_ms_setup());
    // Retry when the port is currently down even if nothing changed: otherwise a
    // serial device that was missing at boot can only be reopened by editing the
    // path and editing it back -- the same trap the host link used to have.
    if (strcmp(work.serial_dev, orig.serial_dev) != 0 || work.baud != orig.baud
        || work.serial_format != orig.serial_format
        || !serial_connected())
        serial_reconfigure(work.serial_dev, work.baud, work.serial_format);
    if (strcmp(work.font_path, orig.font_path) != 0)
        textmode_reload_font();

    // (Re)connect the host link when asked to, when the destination changed, or
    // when auto-reconnect was just switched on over a link that is already down.
    // hostconn owns the priority chain (local shell > ssh > telnet > serial) and
    // the retry backoff. Prompts appear once the menu closes.
    int dest_changed = work.local_shell != orig.local_shell ||
                       strcmp(work.ssh_host, orig.ssh_host) != 0 ||
                       work.ssh_port != orig.ssh_port ||
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
    if ((b == '\r' || b == '\n') && sel == F_UPDATE) {
        // Stays open: the row shows the check (or the build) as it runs, and an
        // install restarts the terminal anyway. Edits made so far are kept in
        // the menu, not saved.
        update_start();
        draw();
        return;
    }
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
