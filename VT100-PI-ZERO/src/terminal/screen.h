// Terminal screen model: the cursor, current rendition, scroll region, and the
// operations that mutate the cell grid. The VT parser (Phase 4) drives these;
// screen_putc() is the Phase-3 "glass TTY" that handles printable chars plus
// the C0 control codes directly.
#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>
#include "config.h"

typedef struct {
    int     row, col;            // cursor position (0-based)
    uint8_t fg, bg, attr;        // current SGR rendition
    int     scroll_top;          // scroll region (inclusive), 0..ROWS-1
    int     scroll_bot;
    int     wrap_pending;        // VT100 deferred-wrap at last column

    int     origin_mode;         // DECOM: cursor addressing relative to region
    int     autowrap;            // DECAWM
    int     insert_mode;         // IRM
    int     cursor_visible;      // DECTCEM

    // Tab stops: 1 = a stop is set at that column.
    uint8_t tab_stops[TERM_COLS];

    // Charsets: G0/G1 each 0=ASCII, 1=DEC Special Graphics. gl selects which.
    int     charset[2];          // [0]=G0 [1]=G1
    int     gl;                  // 0 -> use G0, 1 -> use G1 (SI/SO)

    // Saved cursor state (DECSC/DECRC) — includes charset per the VT100 spec.
    int     saved_row, saved_col;
    uint8_t saved_fg, saved_bg, saved_attr;
    int     saved_charset[2], saved_gl, saved_origin;
} screen_t;

void screen_init(void);

// Glass-TTY byte sink: printable -> glyph at cursor; C0 controls handled.
void screen_putc(uint8_t c);

// Granular operations (also used by the Phase-4 parser).
void screen_put_glyph(uint8_t glyph);
void screen_move_to(int row, int col);      // clamped, absolute
void screen_carriage_return(void);
void screen_line_feed(void);                 // down + scroll at region bottom
void screen_backspace(void);
void screen_tab(void);
void screen_scroll_up(int n);
void screen_scroll_down(int n);
void screen_erase_screen(void);              // clear all, home cursor
void screen_set_sgr(uint8_t fg, uint8_t bg, uint8_t attr);

// Cursor motion (relative, clamped to scroll region for up/down).
void screen_cursor_up(int n);
void screen_cursor_down(int n);
void screen_cursor_left(int n);
void screen_cursor_right(int n);

// Editing within the current line / scroll region.
void screen_erase_in_line(int mode);         // 0=to-eol 1=to-bol 2=whole line
void screen_erase_in_display(int mode);      // 0=below 1=above 2=all
void screen_erase_chars(int n);              // overwrite n cells from cursor
void screen_insert_chars(int n);             // ICH
void screen_delete_chars(int n);             // DCH
void screen_insert_lines(int n);             // IL (at cursor row, in region)
void screen_delete_lines(int n);             // DL

// ESC single-shifts and cursor save/restore.
void screen_index(void);                     // IND  (down, scroll)
void screen_reverse_index(void);             // RI   (up, reverse scroll)
void screen_next_line(void);                 // NEL  (CR + IND)
void screen_save_cursor(void);               // DECSC
void screen_restore_cursor(void);            // DECRC
void screen_set_scroll_region(int top, int bot); // DECSTBM (+home)

// Modes touched by the parser.
void screen_set_origin_mode(int on);         // DECOM
void screen_set_autowrap(int on);            // DECAWM
void screen_set_insert_mode(int on);         // IRM
void screen_set_cursor_visible(int on);      // DECTCEM
void screen_set_reverse(int on);             // DECSCNM (whole-screen reverse)
void screen_reset(void);                     // RIS

// Tab stops.
void screen_set_tab(void);                   // HTS: set stop at cursor column
void screen_clear_tab(int mode);             // TBC: 0=at cursor, 3=all

// Charset selection (VT100 G0/G1, DEC Special Graphics).
void screen_designate_charset(int which, int dec_graphics); // ESC ( / )
void screen_shift(int gl);                    // SI (gl=0) / SO (gl=1)

// Cursor visibility bookkeeping around edits.
void screen_hide_cursor(void);
void screen_show_cursor(void);

extern screen_t scr;

#endif // SCREEN_H
