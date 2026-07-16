#include "terminal/vt100.h"
#include "terminal/vtparse.h"
#include "terminal/screen.h"
#include "video/textmode.h"
#include "io/host_link.h"

#include <stdio.h>
#include <string.h>

static vtparse_t parser;

static struct {
    int cursor_keys_app;   // DECCKM
    int keypad_app;        // DECKPAM/PNM
    int newline_mode;      // LNM
} vt;

static int bell_pending = 0;
int vt100_take_bell(void) { int b = bell_pending; bell_pending = 0; return b; }

int vt100_cursor_keys_app(void) { return vt.cursor_keys_app; }
int vt100_keypad_app(void)      { return vt.keypad_app; }
int vt100_newline_mode(void)    { return vt.newline_mode; }

// ---- helpers -------------------------------------------------------------
static int pget(vtparse_t *p, int i, int def) {
    if (i >= p->num_params) return def;
    int v = p->params[i];
    return (v < 0) ? def : v;
}

// Replies (DSR/DA) go back over whichever transport is currently active
// (serial / telnet / ssh) — see io/host_link.h.
static void respond(const char *s) {
    host_write((const uint8_t *)s, (uint32_t)strlen(s));
}

// Map an xterm 256-colour index down to our 16-colour palette.
static uint8_t map256(int n) {
    if (n < 16) return (uint8_t)n;
    if (n >= 232) { int g = n - 232; return (uint8_t)(g < 4 ? 0 : (g < 16 ? 8 : 7)); }
    n -= 16; int r = n / 36, g = (n / 6) % 6, b = n % 6;
    int idx = (r > 2 ? 1 : 0) | (g > 2 ? 2 : 0) | (b > 2 ? 4 : 0);
    int bright = (r > 3 || g > 3 || b > 3);
    return (uint8_t)(bright ? idx + 8 : idx);
}
static uint8_t maprgb(int r, int g, int b) {
    int idx = (r > 127 ? 1 : 0) | (g > 127 ? 2 : 0) | (b > 127 ? 4 : 0);
    int bright = (r > 170 || g > 170 || b > 170);
    return (uint8_t)(bright ? idx + 8 : idx);
}

// ---- SGR (Select Graphic Rendition) -------------------------------------
static void do_sgr(vtparse_t *p) {
    if (p->num_params == 0 || (p->num_params == 1 && p->params[0] <= 0)) {
        screen_set_sgr(7, 0, 0);
        return;
    }
    for (int i = 0; i < p->num_params; ++i) {
        int v = p->params[i]; if (v < 0) v = 0;
        if (v == 0)                    { scr.fg = 7; scr.bg = 0; scr.attr = 0; }
        else if (v == 1)               scr.attr |= ATTR_BOLD;
        else if (v == 4)               scr.attr |= ATTR_UNDERLINE;
        else if (v == 5)               scr.attr |= ATTR_BLINK;
        else if (v == 7)               scr.attr |= ATTR_REVERSE;
        else if (v == 22)              scr.attr &= ~ATTR_BOLD;
        else if (v == 24)              scr.attr &= ~ATTR_UNDERLINE;
        else if (v == 25)              scr.attr &= ~ATTR_BLINK;
        else if (v == 27)              scr.attr &= ~ATTR_REVERSE;
        else if (v >= 30 && v <= 37)   scr.fg = v - 30;
        else if (v == 39)              scr.fg = 7;
        else if (v >= 40 && v <= 47)   scr.bg = v - 40;
        else if (v == 49)              scr.bg = 0;
        else if (v >= 90 && v <= 97)   scr.fg = v - 90 + 8;
        else if (v >= 100 && v <= 107) scr.bg = v - 100 + 8;
        else if (v == 38 || v == 48) {
            uint8_t col = 7;
            if (i + 2 < p->num_params && p->params[i + 1] == 5) {
                col = map256(pget(p, i + 2, 7)); i += 2;
            } else if (i + 4 < p->num_params && p->params[i + 1] == 2) {
                col = maprgb(pget(p, i + 2, 0), pget(p, i + 3, 0), pget(p, i + 4, 0)); i += 4;
            }
            if (v == 38) scr.fg = col; else scr.bg = col;
        }
    }
}

// ---- DEC private (CSI ? Pm h/l) and ANSI (CSI Pm h/l) modes --------------
static void do_mode(vtparse_t *p, int on) {
    for (int i = 0; i < p->num_params; ++i) {
        int v = p->params[i]; if (v < 0) continue;
        if (p->prefix == '?') {
            switch (v) {
                case 1:  vt.cursor_keys_app = on; break;   // DECCKM
                case 5:  screen_set_reverse(on); break;    // DECSCNM
                case 6:  screen_set_origin_mode(on); break; // DECOM
                case 7:  screen_set_autowrap(on); break;   // DECAWM
                case 25: screen_set_cursor_visible(on); break; // DECTCEM
                default: break;
            }
        } else {
            switch (v) {
                case 4:  screen_set_insert_mode(on); break; // IRM
                case 20: vt.newline_mode = on; break;       // LNM
                default: break;
            }
        }
    }
}

// ---- device reports ------------------------------------------------------
static void do_dsr(vtparse_t *p) {
    int req = pget(p, 0, 0);
    char buf[32];
    if (req == 6) {                       // cursor position report
        int row = scr.row + 1, col = scr.col + 1;
        if (scr.origin_mode) row = scr.row - scr.scroll_top + 1;
        snprintf(buf, sizeof buf, "\x1b[%d;%dR", row, col);
        respond(buf);
    } else if (req == 5) {
        respond("\x1b[0n");               // terminal OK
    }
}

// ---- CSI final dispatch --------------------------------------------------
static void do_csi(vtparse_t *p, uint8_t ch) {
    switch (ch) {
        case 'A': screen_cursor_up(pget(p, 0, 1)); break;
        case 'B': case 'e': screen_cursor_down(pget(p, 0, 1)); break;
        case 'C': case 'a': screen_cursor_right(pget(p, 0, 1)); break;
        case 'D': screen_cursor_left(pget(p, 0, 1)); break;
        case 'E': screen_cursor_down(pget(p, 0, 1)); screen_carriage_return(); break;
        case 'F': screen_cursor_up(pget(p, 0, 1)); screen_carriage_return(); break;
        case 'G': case '`': {                       // CHA / HPA (column only)
            int c = pget(p, 0, 1) - 1;
            if (c < 0) c = 0;
            if (c > TERM_COLS - 1) c = TERM_COLS - 1;
            scr.col = c; scr.wrap_pending = 0;
        } break;
        case 'd': {                                 // VPA (row only, absolute)
            int r = pget(p, 0, 1) - 1;
            if (r < 0) r = 0;
            if (r > TERM_ROWS - 1) r = TERM_ROWS - 1;
            scr.row = r; scr.wrap_pending = 0;
        } break;
        case 'H': case 'f':                         // CUP / HVP
            screen_move_to(pget(p, 0, 1) - 1, pget(p, 1, 1) - 1); break;
        case 'J': screen_erase_in_display(pget(p, 0, 0)); break;
        case 'K': screen_erase_in_line(pget(p, 0, 0)); break;
        case 'L': screen_insert_lines(pget(p, 0, 1)); break;
        case 'M': screen_delete_lines(pget(p, 0, 1)); break;
        case '@': screen_insert_chars(pget(p, 0, 1)); break;
        case 'P': screen_delete_chars(pget(p, 0, 1)); break;
        case 'X': screen_erase_chars(pget(p, 0, 1)); break;
        case 'S': screen_scroll_up(pget(p, 0, 1)); break;
        case 'T': screen_scroll_down(pget(p, 0, 1)); break;
        case 'm': do_sgr(p); break;
        case 'g': screen_clear_tab(pget(p, 0, 0)); break;   // TBC
        case 'r': screen_set_scroll_region(pget(p, 0, 1), pget(p, 1, TERM_ROWS)); break;
        case 'h': do_mode(p, 1); break;
        case 'l': do_mode(p, 0); break;
        case 'n': do_dsr(p); break;
        case 'c':                                   // DA
            if (p->prefix == 0 && pget(p, 0, 0) == 0) respond("\x1b[?1;2c");
            break;
        case 's': if (p->prefix == 0) screen_save_cursor(); break;
        case 'u': if (p->prefix == 0) screen_restore_cursor(); break;
        default: break;
    }
}

// ---- ESC final dispatch --------------------------------------------------
static void do_esc(vtparse_t *p, uint8_t ch) {
    if (p->num_intermediate == 0) {
        switch (ch) {
            case 'D': screen_index(); break;          // IND
            case 'M': screen_reverse_index(); break;  // RI
            case 'E': screen_next_line(); break;      // NEL
            case 'H': screen_set_tab(); break;        // HTS
            case '7': screen_save_cursor(); break;    // DECSC
            case '8': screen_restore_cursor(); break; // DECRC
            case '=': vt.keypad_app = 1; break;       // DECKPAM
            case '>': vt.keypad_app = 0; break;       // DECKPNM
            case 'c':                                  // RIS
                vt.cursor_keys_app = vt.keypad_app = vt.newline_mode = 0;
                screen_reset();
                break;
            default: break;
        }
    } else if (p->intermediate[0] == '(' || p->intermediate[0] == ')') {
        // Designate G0 (ESC () or G1 (ESC )): '0' = DEC graphics, else ASCII.
        int which = (p->intermediate[0] == '(') ? 0 : 1;
        screen_designate_charset(which, ch == '0');
    } else if (p->intermediate[0] == '#') {
        if (ch == '8') {                              // DECALN: fill with 'E'
            for (int r = 0; r < TERM_ROWS; ++r)
                for (int col = 0; col < TERM_COLS; ++col)
                    tm_cells[r][col] = (cell_t){ 'E', 7, 0, 0 };
            textmode_render_all();
            screen_move_to(0, 0);
        }
    }
}

// ---- parser callback -----------------------------------------------------
static void cb(vtparse_t *p, vt_action_t action, uint8_t ch) {
    switch (action) {
        case VT_ACTION_PRINT:
            screen_put_glyph(ch);
            break;
        case VT_ACTION_EXECUTE:
            switch (ch) {
                case 0x08: screen_backspace(); break;
                case 0x09: screen_tab(); break;
                case 0x0a: case 0x0b: case 0x0c:
                    if (vt.newline_mode) screen_next_line(); else screen_line_feed();
                    break;
                case 0x0d: screen_carriage_return(); break;
                case 0x0e: screen_shift(1); break;     // SO -> G1
                case 0x0f: screen_shift(0); break;     // SI -> G0
                case 0x07: bell_pending = 1; break;    // BEL
                default: break;
            }
            break;
        case VT_ACTION_CSI_DISPATCH: do_csi(p, ch); break;
        case VT_ACTION_ESC_DISPATCH: do_esc(p, ch); break;
        default: break;                                // OSC/DCS ignored for now
    }
}

void vt100_init(void) {
    vt.cursor_keys_app = vt.keypad_app = vt.newline_mode = 0;
    vtparse_init(&parser, cb, 0);
}

void vt100_feed(uint8_t c) {
    vtparse_byte(&parser, c);
}
