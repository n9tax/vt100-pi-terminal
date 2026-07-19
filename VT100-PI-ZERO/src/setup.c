// On-screen Setup menu. See setup.h. Renders directly into tm_cells and repaints
// via textmode; navigation is Up/Down between fields, Left/Right to change enum
// values, typing to edit text fields, Enter to save+apply, Ctrl+F3 to cancel.
#include "setup.h"
#include "config.h"
#include "settings.h"
#include "video/textmode.h"
#include "video/fonts.h"
#include "io/serial_linux.h"

#include <string.h>
#include <stdio.h>

enum { F_SERIAL, F_BAUD, F_THEME, F_CURSOR, F_ECHO, F_FONT, NFIELDS };

static int active;
static int sel;
static settings_t work;                     // edited copy
static settings_t orig;                     // snapshot at open (for change detection)
static cell_t saved[TERM_ROWS][TERM_COLS];  // terminal screen behind the menu
static int esc;                             // escape-sequence parser state

static const int  bauds[]  = { 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
#define NBAUDS ((int)(sizeof bauds / sizeof bauds[0]))

int setup_active(void) { return active; }

// ---- rendering -------------------------------------------------------------
static void put(int r, int c, const char *s, uint8_t fg, uint8_t bg, uint8_t attr) {
    for (; *s && c < TERM_COLS; ++c, ++s)
        if (r >= 0 && r < TERM_ROWS && c >= 0)
            tm_cells[r][c] = (cell_t){ (uint8_t)*s, fg, bg, attr };
}

static void field_value(int i, char *out, size_t n) {
    switch (i) {
        case F_SERIAL: snprintf(out, n, "%s", work.serial_dev); break;
        case F_BAUD:   snprintf(out, n, "%d", work.baud); break;
        case F_THEME:  snprintf(out, n, "%s", settings_theme_name(work.theme)); break;
        case F_CURSOR: snprintf(out, n, "%s", work.cursor_style ? "underline" : "block"); break;
        case F_ECHO:   snprintf(out, n, "%s", work.local_echo ? "on" : "off"); break;
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
        "Serial device", "Baud rate", "Theme", "Cursor", "Local echo", "Font",
    };
    for (int r = 0; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c)
            tm_cells[r][c] = (cell_t){ ' ', 7, 0, 0 };

    put(1, 4, "VT100-PI-ZERO  \xC4\xC4  SETUP", 7, 0, ATTR_BOLD);
    put(2, 4, "________________________________________", 7, 0, 0);

    for (int i = 0; i < NFIELDS; ++i) {
        int r = 4 + i * 2;
        int selrow = (i == sel);
        char val[300];
        field_value(i, val, sizeof val);
        if (selrow && i == F_SERIAL) {
            size_t l = strlen(val);
            if (l + 1 < sizeof val) { val[l] = '_'; val[l + 1] = '\0'; }   // edit caret
        }
        put(r, 4, selrow ? ">" : " ", 7, 0, 0);
        put(r, 6, labels[i], 7, 0, selrow ? ATTR_BOLD : 0);
        put(r, 22, val, selrow ? 0 : 7, selrow ? 7 : 0, 0);   // selected = reverse video
    }

    put(TERM_ROWS - 4, 4, "Up/Down: field    Left/Right: change value", 7, 0, 0);
    put(TERM_ROWS - 3, 4, "Serial device is typed (Backspace deletes)", 7, 0, 0);
    put(TERM_ROWS - 2, 4, "Enter: save & apply     Ctrl+F3: cancel", 7, 0, ATTR_BOLD);

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
        case F_THEME:  work.theme = (work.theme + d + THEME_COUNT) % THEME_COUNT; break;
        case F_CURSOR: work.cursor_style ^= 1; break;
        case F_ECHO:   work.local_echo ^= 1; break;
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

static void edit_text(uint8_t b) {   // only the Serial device field is typed
    char  *buf = work.serial_dev;
    size_t cap = sizeof work.serial_dev;
    size_t n = strlen(buf);
    if ((b == 0x7f || b == 0x08) && n > 0) buf[n - 1] = '\0';           // backspace
    else if (b >= 0x20 && b < 0x7f && n + 1 < cap) { buf[n] = (char)b; buf[n + 1] = '\0'; }
}

static void restore_screen(void) {
    memcpy(tm_cells, saved, sizeof saved);
    textmode_render_all();
}

static void do_save(void) {
    g_settings = work;
    settings_save();

    // Apply live. Theme/cursor are trivial; serial reopen and font rebuild only
    // when they actually changed.
    textmode_set_theme(work.theme);
    textmode_set_cursor_style(work.cursor_style);
    if (strcmp(work.serial_dev, orig.serial_dev) != 0 || work.baud != orig.baud)
        serial_reconfigure(work.serial_dev, work.baud);
    if (strcmp(work.font_path, orig.font_path) != 0)
        textmode_reload_font();

    active = 0;
    restore_screen();   // repaints with the now-current theme/font
}

// ---- public ----------------------------------------------------------------
void setup_toggle(void) {
    if (!active) {
        work = g_settings;
        orig = g_settings;
        memcpy(saved, tm_cells, sizeof saved);
        active = 1;
        sel = 0;
        esc = 0;
        draw();
    } else {
        active = 0;         // second Ctrl+F3 = cancel
        restore_screen();
    }
}

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
    if (b == '\r' || b == '\n') { do_save(); return; }
    if (b == '\t') { move_sel(+1); draw(); return; }

    if (sel == F_SERIAL) edit_text(b);
    draw();
}
