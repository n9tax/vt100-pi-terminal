#include "setup.h"
#include "settings.h"
#include "config.h"
#include "video/textmode.h"
#include "io/serial.h"
#include "net/net.h"

#include <string.h>

static int active = 0;
static int sel = 0;                              // selected row on the main page
static int page = 0;                             // 0 = settings, 1 = telnet sites
static int bsel = 0;                             // selected speed-dial slot
static enum { S_GROUND, S_ESC, S_CSI } instate = S_GROUND;
static cell_t backup[TERM_ROWS][TERM_COLS];      // terminal screen saved on entry

// Pending network action for the main loop to execute at a shallow stack depth
// (cyw43 bring-up is heavy): 0 = none, 1 = connect, 2 = disconnect.
static int net_action = 0;

// Text-entry state. Editing buffers keystrokes here and commits on Enter to
// whichever field is being edited (a string dst, or a uint16 port dst).
static int         editing = 0;
static char        editbuf[64];
static int         editlen = 0;
static char       *edit_dst  = 0;    // string destination (NULL if editing a port)
static uint16_t   *edit_ndst = 0;    // port destination (NULL if editing a string)
static int         edit_max  = 0;    // capacity of the string destination
static int         edit_mask = 0;    // render as '*' (password)
static const char *edit_label = "";  // field name shown while editing on the sites page

enum {
    IT_THEME, IT_CURSOR, IT_ECHO, IT_BELL, IT_BAUD, IT_SCROLL, IT_SCROLLSPD,
    IT_SSID, IT_PASS, IT_HOST, IT_PORT, IT_CONNECT, IT_SITES,
    IT_EXIT, N_ITEMS
};

// Field kinds: how Enter/Left/Right behave on each row.
enum { K_CYCLE, K_TEXT, K_PASS, K_NUM, K_ACTION };
static int item_kind(int item) {
    switch (item) {
        case IT_SSID: case IT_HOST: return K_TEXT;
        case IT_PASS: return K_PASS;
        case IT_PORT: return K_NUM;
        case IT_CONNECT: case IT_SITES: case IT_EXIT: return K_ACTION;
        default: return K_CYCLE;
    }
}

static const char *theme_names[]  = { "Color", "Amber", "Green", "White",
                                      "Blue", "Red", "Yellow", "C64", "Borland" };
static const char *cursor_names[] = { "Block", "Underline" };
static const char *onoff[]        = { "Off", "On" };
static const char *bell_names[]   = { "Off", "Visual" };
static const char *scroll_names[] = { "Jump", "Smooth" };
static const char *speed_names[]  = { "Slow", "Fast" };
#define N_SPEEDS 2
static const uint32_t bauds[]     = { 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
#define N_BAUDS (int)(sizeof(bauds) / sizeof(bauds[0]))

int setup_active(void) { return active; }
int setup_take_net_action(void) { int a = net_action; net_action = 0; return a; }

// Low-stack string helpers (no snprintf: it is stack-heavy and this runs deep
// in the call chain where a video IRQ landing on top could overflow the stack).
static char *put_s(char *d, const char *s) { while (*s) *d++ = *s++; return d; }
static char *put_u(char *d, uint32_t n) {
    char t[12]; int i = 0;
    if (n == 0) { *d++ = '0'; return d; }
    while (n) { t[i++] = (char)('0' + n % 10); n /= 10; }
    while (i) *d++ = t[--i];
    return d;
}

// Write a string into the cell grid (no immediate render).
static void put(int row, int col, const char *s, uint8_t attr) {
    if (row < 0 || row >= TERM_ROWS) return;
    for (; *s && col < TERM_COLS; ++s, ++col)
        if (col >= 0) tm_cells[row][col] = (cell_t){ (uint8_t)*s, 7, 0, attr };
}

static int baud_index(void) {
    for (int i = 0; i < N_BAUDS; ++i) if (bauds[i] == settings.baud) return i;
    return 4; // 9600
}

static void item_value(int item, char *out) {
    char *d = out;
    switch (item) {
        case IT_THEME:  d = put_s(d, theme_names[settings.theme % THEME_COUNT]); break;
        case IT_CURSOR: d = put_s(d, cursor_names[settings.cursor_style & 1]); break;
        case IT_ECHO:   d = put_s(d, onoff[settings.local_echo & 1]); break;
        case IT_BELL:   d = put_s(d, bell_names[settings.bell_visual & 1]); break;
        case IT_BAUD:   d = put_u(d, settings.baud); break;
        case IT_SCROLL: d = put_s(d, scroll_names[settings.scroll_smooth & 1]); break;
        case IT_SCROLLSPD: d = put_s(d, speed_names[settings.scroll_speed % N_SPEEDS]); break;
        case IT_SSID:   d = put_s(d, settings.wifi_ssid[0] ? settings.wifi_ssid : "<none>"); break;
        case IT_PASS: {
            int n = (int)strlen(settings.wifi_pass);
            if (n == 0) d = put_s(d, "<none>");
            else for (int i = 0; i < n && i < 32; ++i) *d++ = '*';
        } break;
        case IT_HOST:   d = put_s(d, settings.telnet_host[0] ? settings.telnet_host : "<none>"); break;
        case IT_PORT:   d = put_u(d, settings.telnet_port); break;
        case IT_CONNECT:
            d = put_s(d, net_active() ? "[Disconnect]  " : "[Connect]  ");
            d = put_s(d, net_status());
            break;
        default: break;
    }
    *d = 0;
}

static const char *item_label(int item) {
    switch (item) {
        case IT_THEME:   return "Theme";
        case IT_CURSOR:  return "Cursor";
        case IT_ECHO:    return "Local echo";
        case IT_BELL:    return "Bell";
        case IT_BAUD:    return "Baud rate";
        case IT_SCROLL:  return "Scroll";
        case IT_SCROLLSPD: return "Scroll speed";
        case IT_SSID:    return "WiFi SSID";
        case IT_PASS:    return "WiFi pass";
        case IT_HOST:    return "Telnet host";
        case IT_PORT:    return "Telnet port";
        case IT_CONNECT: return "Telnet";
        case IT_SITES:   return "Telnet sites  >";
        default:         return "Save & Exit";
    }
}

// Rows: small gaps separate the display group, network group and Save & Exit.
static int item_row(int item) {
    int base = 5 + item;
    if (item >= IT_SSID) base += 1;    // blank line before "-- Network --"
    if (item == IT_EXIT) base += 1;    // blank line before Save & Exit
    return base;
}

static void clear_screen(void) {
    cell_t blank = { ' ', 7, 0, 0 };
    for (int r = 0; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c) tm_cells[r][c] = blank;
}

static void draw_main(void) {
    clear_screen();
    int col = 22;
    put(2, col, "======== VT100-PI  SET-UP ========", ATTR_BOLD);
    put(item_row(IT_SSID) - 1, col, "-- Network --", ATTR_BOLD);

    for (int i = 0; i < N_ITEMS; ++i) {
        int row = item_row(i);
        uint8_t attr = (i == sel) ? ATTR_REVERSE : 0;
        char line[128], *d = line;
        d = put_s(d, "  ");
        d = put_s(d, item_label(i));
        if (i != IT_EXIT && i != IT_SITES) {
            while (d - line < 15) *d++ = ' ';
            d = put_s(d, ": ");
            if (editing && i == sel) {              // live edit buffer + caret
                for (int k = 0; k < editlen; ++k) *d++ = edit_mask ? '*' : editbuf[k];
                *d++ = '_';
            } else {
                item_value(i, d);
                d += strlen(d);
            }
        }
        *d = 0;
        put(row, col, line, attr);
    }

    if (editing)
        put(TERM_ROWS - 3, 8, "Type value   Enter: accept   ESC: cancel", 0);
    else
        put(TERM_ROWS - 3, 8, "Up/Down: move   Left/Right/Enter: change ", 0);
    put(TERM_ROWS - 2, 8, "Enter on Save&Exit or Ctrl-F3: close     ", 0);
    textmode_render_all();
}

static void draw_sites(void) {
    clear_screen();
    int col = 16;
    put(2, col, "========= TELNET SPEED DIAL =========", ATTR_BOLD);

    for (int i = 0; i < NUM_BOOKMARKS; ++i) {
        int row = 5 + i;
        uint8_t attr = (i == bsel) ? ATTR_REVERSE : 0;
        bookmark_t *bm = &settings.bookmarks[i];
        char line[128], *d = line;
        *d++ = ' ';
        *d++ = (char)('1' + i);
        d = put_s(d, "  ");
        if (editing && i == bsel) {
            d = put_s(d, edit_label);
            d = put_s(d, ": ");
            for (int k = 0; k < editlen; ++k) *d++ = editbuf[k];
            *d++ = '_';
        } else if (bm->host[0] == 0 && bm->name[0] == 0) {
            d = put_s(d, "<empty>");
        } else {
            char *n0 = d;
            d = put_s(d, bm->name[0] ? bm->name : "(unnamed)");
            while (d - n0 < 14) *d++ = ' ';
            d = put_s(d, bm->host[0] ? bm->host : "<no host>");
            *d++ = ':';
            d = put_u(d, bm->port ? bm->port : 23);
        }
        *d = 0;
        put(row, col, line, attr);
    }

    if (editing)
        put(TERM_ROWS - 3, 6, "Type value   Enter: accept   ESC: cancel", 0);
    else {
        put(TERM_ROWS - 3, 6, "Up/Down: pick   Enter: connect   n/h/p: edit", 0);
        put(TERM_ROWS - 2, 6, "x: clear slot     q or ESC: back to Setup", 0);
    }
    textmode_render_all();
}

static void draw(void) { if (page == 1) draw_sites(); else draw_main(); }

// Redraw if on screen (used by the main loop to reflect live net status changes).
void setup_refresh(void) { if (active) draw(); }

static void change_value(int item, int delta) {
    switch (item) {
        case IT_THEME:
            settings.theme = (uint8_t)((settings.theme + delta + THEME_COUNT) % THEME_COUNT);
            textmode_set_theme(settings.theme);
            break;
        case IT_CURSOR:
            settings.cursor_style ^= 1;
            textmode_set_cursor_style(settings.cursor_style);
            break;
        case IT_ECHO:  settings.local_echo ^= 1; break;
        case IT_BELL:  settings.bell_visual ^= 1; break;
        case IT_SCROLL:
            settings.scroll_smooth ^= 1;
            textmode_set_smooth(settings.scroll_smooth, settings_scroll_pps());
            break;
        case IT_SCROLLSPD: {
            int v = settings.scroll_speed + delta;
            if (v < 0) v = 0;
            if (v > N_SPEEDS - 1) v = N_SPEEDS - 1;
            settings.scroll_speed = (uint8_t)v;
            textmode_set_smooth(settings.scroll_smooth, settings_scroll_pps());
        } break;
        case IT_BAUD: {
            int idx = baud_index() + delta;
            if (idx < 0) idx = 0;
            if (idx >= N_BAUDS) idx = N_BAUDS - 1;
            settings.baud = bauds[idx];
            serial_set_baud(settings.baud);
        } break;
        default: break;
    }
}

// ---- Generic text entry --------------------------------------------------
static void edit_begin_str(char *dst, int max, int mask, const char *label) {
    editing = 1; edit_dst = dst; edit_ndst = 0; edit_max = max; edit_mask = mask;
    edit_label = label;
    editlen = 0;
    int lim = max - 1; if (lim > (int)sizeof editbuf - 1) lim = sizeof editbuf - 1;
    while (dst[editlen] && editlen < lim) { editbuf[editlen] = dst[editlen]; editlen++; }
    editbuf[editlen] = 0;
}
static void edit_begin_port(uint16_t *dst, const char *label) {
    editing = 1; edit_dst = 0; edit_ndst = dst; edit_max = 6; edit_mask = 0;
    edit_label = label;
    char tmp[8]; char *e = put_u(tmp, *dst); *e = 0;
    editlen = 0; while (tmp[editlen]) { editbuf[editlen] = tmp[editlen]; editlen++; }
    editbuf[editlen] = 0;
}
static void edit_commit(void) {
    editbuf[editlen] = 0;
    if (edit_dst) {
        strncpy(edit_dst, editbuf, edit_max - 1);
        edit_dst[edit_max - 1] = 0;
    } else if (edit_ndst) {
        uint32_t v = 0;
        for (int i = 0; i < editlen; ++i)
            if (editbuf[i] >= '0' && editbuf[i] <= '9') v = v * 10 + (uint32_t)(editbuf[i] - '0');
        if (v == 0 || v > 65535) v = 23;
        *edit_ndst = (uint16_t)v;
    }
    editing = 0;
}

static void edit_feed(uint8_t b) {
    if (b == '\r' || b == '\n')      { edit_commit(); draw(); }
    else if (b == 0x1b)              { editing = 0; draw(); }              // ESC cancels
    else if (b == 0x08 || b == 0x7f) { if (editlen) editlen--; draw(); }  // backspace
    else if (b >= 0x20 && b < 0x7f) {
        int numeric = (edit_ndst != 0);
        int max = numeric ? 5 : edit_max - 1;
        if (max > (int)sizeof editbuf - 1) max = (int)sizeof editbuf - 1;
        int ok = !numeric || (b >= '0' && b <= '9');
        if (ok && editlen < max) editbuf[editlen++] = (char)b;
        draw();
    }
}

// ---- Main page actions ---------------------------------------------------
static void activate(int item) {
    switch (item) {
        case IT_SSID: edit_begin_str(settings.wifi_ssid,   sizeof settings.wifi_ssid,   0, "ssid"); break;
        case IT_PASS: edit_begin_str(settings.wifi_pass,   sizeof settings.wifi_pass,   1, "pass"); break;
        case IT_HOST: edit_begin_str(settings.telnet_host, sizeof settings.telnet_host, 0, "host"); break;
        case IT_PORT: edit_begin_port(&settings.telnet_port, "port"); break;
        case IT_SITES:   page = 1; bsel = 0; break;
        case IT_CONNECT: net_action = net_active() ? 2 : 1; break;
        case IT_EXIT:    setup_toggle(); break;
        default:         change_value(item, +1); break;
    }
}

// ---- Speed-dial page actions ---------------------------------------------
static void sites_connect(void) {
    bookmark_t *bm = &settings.bookmarks[bsel];
    if (bm->host[0] == 0) return;                        // empty slot: nothing to dial
    strncpy(settings.telnet_host, bm->host, sizeof settings.telnet_host - 1);
    settings.telnet_host[sizeof settings.telnet_host - 1] = 0;
    settings.telnet_port = bm->port ? bm->port : 23;
    net_action = 1;                                      // main loop runs the connect
    setup_toggle();                                      // save settings + close menu
}

void setup_toggle(void) {
    if (!active) {
        memcpy(backup, tm_cells, sizeof tm_cells);   // save terminal screen
        active = 1; sel = 0; page = 0; bsel = 0; instate = S_GROUND; editing = 0;
        draw();
    } else {
        active = 0; editing = 0;
        settings_save();                             // persist everything to flash
        memcpy(tm_cells, backup, sizeof tm_cells);   // restore terminal screen
        textmode_render_all();
    }
}

static void sites_feed(uint8_t b) {
    switch (instate) {
        case S_GROUND:
            if (b == 0x1b) { instate = S_ESC; }
            else if (b == '\r' || b == '\n') { sites_connect(); }   // may close the menu
            else if (b == 'n' || b == 'N') { edit_begin_str(settings.bookmarks[bsel].name, sizeof settings.bookmarks[bsel].name, 0, "name"); draw(); }
            else if (b == 'h' || b == 'H') { edit_begin_str(settings.bookmarks[bsel].host, sizeof settings.bookmarks[bsel].host, 0, "host"); draw(); }
            else if (b == 'p' || b == 'P') { edit_begin_port(&settings.bookmarks[bsel].port, "port"); draw(); }
            else if (b == 'x' || b == 'X' || b == 'd' || b == 'D') { memset(&settings.bookmarks[bsel], 0, sizeof settings.bookmarks[bsel]); draw(); }
            else if (b == 'q' || b == 'Q') { page = 0; sel = IT_SITES; draw(); }
            break;
        case S_ESC:
            if (b == '[' || b == 'O') instate = S_CSI;
            else { instate = S_GROUND; page = 0; sel = IT_SITES; draw(); }   // lone ESC = back
            break;
        case S_CSI:
            instate = S_GROUND;
            if      (b == 'A') { bsel = (bsel + NUM_BOOKMARKS - 1) % NUM_BOOKMARKS; draw(); }
            else if (b == 'B') { bsel = (bsel + 1) % NUM_BOOKMARKS; draw(); }
            break;
    }
}

void setup_feed(uint8_t b) {
    if (!active) return;
    if (editing) { edit_feed(b); return; }
    if (page == 1) { sites_feed(b); return; }
    switch (instate) {
        case S_GROUND:
            if (b == 0x1b) instate = S_ESC;
            else if (b == '\r' || b == '\n') { activate(sel); if (active) draw(); }
            break;
        case S_ESC:
            instate = (b == '[' || b == 'O') ? S_CSI : S_GROUND;
            break;
        case S_CSI:
            instate = S_GROUND;
            if      (b == 'A') { sel = (sel + N_ITEMS - 1) % N_ITEMS; draw(); }
            else if (b == 'B') { sel = (sel + 1) % N_ITEMS; draw(); }
            else if (b == 'D') { if (item_kind(sel) == K_CYCLE) { change_value(sel, -1); draw(); } }
            else if (b == 'C') { activate(sel); if (active) draw(); }
            break;
    }
}
