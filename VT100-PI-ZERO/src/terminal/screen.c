#include "terminal/screen.h"
#include "video/textmode.h"

#include <string.h>

screen_t scr;

static void init_tab_stops(void) {
    for (int c = 0; c < TERM_COLS; ++c) scr.tab_stops[c] = (c % 8 == 0) ? 1 : 0;
}

static cell_t blank_cell(void) {
    return (cell_t){ ' ', scr.fg, scr.bg, 0 };
}

// Last usable column of the cursor row: a double-width or double-height row
// holds only half as many characters.
static int last_col(void) {
    return tm_line[scr.row] != LINE_SINGLE ? TERM_COLS / 2 - 1 : TERM_COLS - 1;
}
// Keep the cursor inside the row it is on, after any move that changes rows.
static void clamp_col(void) { if (scr.col > last_col()) scr.col = last_col(); }

// Whole-row moves carry the line size with the row; a blanked row is single.
static void row_copy(int dst, int src) {
    memcpy(tm_cells[dst], tm_cells[src], sizeof(tm_cells[0]));
    tm_line[dst] = tm_line[src];
}
static void row_blank(int r, cell_t b) {
    for (int c = 0; c < TERM_COLS; ++c) tm_cells[r][c] = b;
    tm_line[r] = LINE_SINGLE;
}

static void render_row(int r) {
    for (int c = 0; c < TERM_COLS; ++c)
        textmode_render_cell(r, c);
}
static void render_rows(int r0, int r1) {
    for (int r = r0; r <= r1; ++r) render_row(r);
}

void screen_hide_cursor(void) {
    if (scr.cursor_visible) textmode_render_cell(scr.row, scr.col);
}
void screen_show_cursor(void) {
    if (scr.cursor_visible) textmode_render_cursor(scr.row, scr.col);
}

void screen_reset(void) {
    scr.row = scr.col = 0;
    scr.fg = 7; scr.bg = 0; scr.attr = 0;
    scr.scroll_top = 0;
    scr.scroll_bot = TERM_ROWS - 1;
    scr.wrap_pending = 0;
    scr.origin_mode = 0;
    scr.autowrap = 1;
    scr.insert_mode = 0;
    scr.cursor_visible = 1;
    scr.charset[0] = scr.charset[1] = CS_ASCII;
    scr.gl = 0;
    init_tab_stops();
    scr.saved_row = scr.saved_col = 0;
    scr.saved_fg = 7; scr.saved_bg = 0; scr.saved_attr = 0;
    scr.saved_charset[0] = scr.saved_charset[1] = 0;
    scr.saved_gl = scr.saved_origin = 0;
    textmode_set_screen_reverse(0);
    screen_erase_screen();
}

void screen_init(void) {
    screen_reset();
    screen_show_cursor();
}

void screen_set_sgr(uint8_t fg, uint8_t bg, uint8_t attr) {
    scr.fg = fg; scr.bg = bg; scr.attr = attr;
}

// ---- Erase ---------------------------------------------------------------
void screen_erase_screen(void) {
    cell_t b = blank_cell();
    for (int r = 0; r < TERM_ROWS; ++r) row_blank(r, b);
    textmode_render_all();
    scr.row = scr.col = 0;
    scr.wrap_pending = 0;
}

void screen_erase_in_line(int mode) {
    cell_t b = blank_cell();
    int c0 = 0, c1 = TERM_COLS - 1;
    if (mode == 0) c0 = scr.col;             // cursor to end
    else if (mode == 1) c1 = scr.col;        // start to cursor
    for (int c = c0; c <= c1; ++c) tm_cells[scr.row][c] = b;
    render_row(scr.row);
    scr.wrap_pending = 0;
}

void screen_erase_in_display(int mode) {
    cell_t b = blank_cell();
    int r0 = 0, r1 = TERM_ROWS - 1;
    if (mode == 0) {                         // cursor to end of screen
        screen_erase_in_line(0);
        r0 = scr.row + 1;
    } else if (mode == 1) {                  // start of screen to cursor
        screen_erase_in_line(1);
        r1 = scr.row - 1;
    }
    // Lines erased whole return to single width, as on the VT100.
    for (int r = r0; r <= r1; ++r) row_blank(r, b);
    if (r0 <= r1) render_rows(r0, r1);
    scr.wrap_pending = 0;
}

void screen_erase_chars(int n) {
    if (n < 1) n = 1;
    cell_t b = blank_cell();
    for (int i = 0; i < n && scr.col + i <= last_col(); ++i)
        tm_cells[scr.row][scr.col + i] = b;
    render_row(scr.row);
}

// ---- Character insert/delete (within the cursor row) ---------------------
void screen_insert_chars(int n) {
    int w = last_col() + 1;
    if (n < 1) n = 1;
    if (n > w - scr.col) n = w - scr.col;
    cell_t *row = tm_cells[scr.row];
    for (int c = w - 1; c >= scr.col + n; --c) row[c] = row[c - n];
    cell_t b = blank_cell();
    for (int c = scr.col; c < scr.col + n; ++c) row[c] = b;
    render_row(scr.row);
}

void screen_delete_chars(int n) {
    int w = last_col() + 1;
    if (n < 1) n = 1;
    if (n > w - scr.col) n = w - scr.col;
    cell_t *row = tm_cells[scr.row];
    for (int c = scr.col; c < w - n; ++c) row[c] = row[c + n];
    cell_t b = blank_cell();
    for (int c = w - n; c < w; ++c) row[c] = b;
    render_row(scr.row);
}

// ---- Line scroll / insert / delete (within scroll region) ----------------
void screen_scroll_up(int n) {
    if (n <= 0) return;
    int top = scr.scroll_top, bot = scr.scroll_bot, rows = bot - top + 1;
    if (n > rows) n = rows;
    for (int r = top; r <= bot - n; ++r)
        row_copy(r, r + n);
    cell_t b = blank_cell();
    for (int r = bot - n + 1; r <= bot; ++r) row_blank(r, b);
    // A single-line scroll of the WHOLE screen can slide smoothly (draws only the
    // new bottom row); scroll regions and multi-line scrolls always jump, exactly
    // as a real VT100 does.
    if (textmode_smooth_enabled() && n == 1 && top == 0 && bot == TERM_ROWS - 1)
        textmode_smooth_line();
    else
        render_rows(top, bot);
}

void screen_scroll_down(int n) {
    if (n <= 0) return;
    int top = scr.scroll_top, bot = scr.scroll_bot, rows = bot - top + 1;
    if (n > rows) n = rows;
    for (int r = bot; r >= top + n; --r)
        row_copy(r, r - n);
    cell_t b = blank_cell();
    for (int r = top; r < top + n; ++r) row_blank(r, b);
    render_rows(top, bot);
}

void screen_insert_lines(int n) {
    if (scr.row < scr.scroll_top || scr.row > scr.scroll_bot) return;
    if (n < 1) n = 1;
    int bot = scr.scroll_bot, rows = bot - scr.row + 1;
    if (n > rows) n = rows;
    for (int r = bot; r >= scr.row + n; --r)
        row_copy(r, r - n);
    cell_t b = blank_cell();
    for (int r = scr.row; r < scr.row + n; ++r) row_blank(r, b);
    render_rows(scr.row, bot);
    scr.col = 0; scr.wrap_pending = 0;
}

void screen_delete_lines(int n) {
    if (scr.row < scr.scroll_top || scr.row > scr.scroll_bot) return;
    if (n < 1) n = 1;
    int bot = scr.scroll_bot, rows = bot - scr.row + 1;
    if (n > rows) n = rows;
    for (int r = scr.row; r <= bot - n; ++r)
        row_copy(r, r + n);
    cell_t b = blank_cell();
    for (int r = bot - n + 1; r <= bot; ++r) row_blank(r, b);
    render_rows(scr.row, bot);
    scr.col = 0; scr.wrap_pending = 0;
}

// ---- Cursor motion -------------------------------------------------------
void screen_move_to(int row, int col) {
    if (scr.origin_mode) {
        row += scr.scroll_top;
        if (row < scr.scroll_top) row = scr.scroll_top;
        if (row > scr.scroll_bot) row = scr.scroll_bot;
    } else {
        if (row < 0) row = 0;
        if (row > TERM_ROWS - 1) row = TERM_ROWS - 1;
    }
    if (col < 0) col = 0;
    if (col > TERM_COLS - 1) col = TERM_COLS - 1;
    scr.row = row; scr.col = col;
    clamp_col();
    scr.wrap_pending = 0;
}

void screen_cursor_up(int n) {
    if (n < 1) n = 1;
    int lo = (scr.row >= scr.scroll_top) ? scr.scroll_top : 0;
    scr.row -= n; if (scr.row < lo) scr.row = lo;
    clamp_col();
    scr.wrap_pending = 0;
}
void screen_cursor_down(int n) {
    if (n < 1) n = 1;
    int hi = (scr.row <= scr.scroll_bot) ? scr.scroll_bot : TERM_ROWS - 1;
    scr.row += n; if (scr.row > hi) scr.row = hi;
    clamp_col();
    scr.wrap_pending = 0;
}
void screen_cursor_left(int n) {
    if (n < 1) n = 1;
    scr.col -= n; if (scr.col < 0) scr.col = 0;
    scr.wrap_pending = 0;
}
void screen_cursor_right(int n) {
    if (n < 1) n = 1;
    scr.col += n; if (scr.col > last_col()) scr.col = last_col();
    scr.wrap_pending = 0;
}

void screen_carriage_return(void) { scr.col = 0; scr.wrap_pending = 0; }

void screen_line_feed(void) {
    scr.wrap_pending = 0;
    if (scr.row == scr.scroll_bot) screen_scroll_up(1);
    else if (scr.row < TERM_ROWS - 1) scr.row++;
    clamp_col();
}

void screen_index(void)        { screen_line_feed(); }
void screen_next_line(void)    { screen_carriage_return(); screen_line_feed(); }
void screen_reverse_index(void) {
    scr.wrap_pending = 0;
    if (scr.row == scr.scroll_top) screen_scroll_down(1);
    else if (scr.row > 0) scr.row--;
    clamp_col();
}

void screen_backspace(void) { scr.wrap_pending = 0; if (scr.col > 0) scr.col--; }

void screen_tab(void) {
    scr.wrap_pending = 0;
    int c = scr.col + 1, last = last_col();
    while (c < last && !scr.tab_stops[c]) c++;
    scr.col = (c > last) ? last : c;
}

void screen_set_tab(void)  { if (scr.col >= 0 && scr.col < TERM_COLS) scr.tab_stops[scr.col] = 1; }
void screen_clear_tab(int mode) {
    if (mode == 3) { for (int c = 0; c < TERM_COLS; ++c) scr.tab_stops[c] = 0; }
    else if (scr.col >= 0 && scr.col < TERM_COLS) scr.tab_stops[scr.col] = 0;
}

void screen_designate_charset(int which, int cs) {
    if (which == 0 || which == 1) scr.charset[which] = cs;
}
void screen_shift(int gl) { scr.gl = gl ? 1 : 0; }

void screen_set_reverse(int on) { textmode_set_screen_reverse(on); }

// ---- Save/restore, region, modes ----------------------------------------
void screen_save_cursor(void) {
    scr.saved_row = scr.row; scr.saved_col = scr.col;
    scr.saved_fg = scr.fg; scr.saved_bg = scr.bg; scr.saved_attr = scr.attr;
    scr.saved_charset[0] = scr.charset[0]; scr.saved_charset[1] = scr.charset[1];
    scr.saved_gl = scr.gl; scr.saved_origin = scr.origin_mode;
}
void screen_restore_cursor(void) {
    scr.row = scr.saved_row; scr.col = scr.saved_col;
    scr.fg = scr.saved_fg; scr.bg = scr.saved_bg; scr.attr = scr.saved_attr;
    scr.charset[0] = scr.saved_charset[0]; scr.charset[1] = scr.saved_charset[1];
    scr.gl = scr.saved_gl; scr.origin_mode = scr.saved_origin;
    clamp_col();
    scr.wrap_pending = 0;
}

void screen_set_scroll_region(int top, int bot) {
    if (top < 1) top = 1;
    if (bot > TERM_ROWS) bot = TERM_ROWS;
    if (bot <= top) { top = 1; bot = TERM_ROWS; }   // reset if invalid
    scr.scroll_top = top - 1;
    scr.scroll_bot = bot - 1;
    // DECSTBM homes the cursor (origin-mode aware).
    screen_move_to(0, 0);
}

void screen_set_origin_mode(int on)   { scr.origin_mode = on; screen_move_to(0, 0); }
void screen_set_autowrap(int on)      { scr.autowrap = on; }
void screen_set_insert_mode(int on)   { scr.insert_mode = on; }
void screen_set_cursor_visible(int on){ scr.cursor_visible = on; }

// ---- Glyph output --------------------------------------------------------
void screen_put_glyph(uint8_t glyph) {
    // Translate via the active charset. DEC Special Graphics keeps the raw char
    // and flags the cell, so the renderer draws the VT100's own glyph for it.
    uint8_t attr = scr.attr;
    int cs = scr.charset[scr.gl];
    if (cs == CS_DEC && glyph >= 0x5f && glyph <= 0x7e) attr |= ATTR_DECG;
    else if (cs == CS_UK && glyph == '#') glyph = 0x9c;   // CP437 pound sign

    if (scr.wrap_pending && scr.autowrap) {
        screen_carriage_return();
        screen_line_feed();
    }
    clamp_col();
    int last = last_col();
    if (scr.insert_mode) {
        cell_t *row = tm_cells[scr.row];
        for (int c = last; c > scr.col; --c) row[c] = row[c - 1];
    }
    tm_cells[scr.row][scr.col] = (cell_t){ glyph, scr.fg, scr.bg, attr };
    if (scr.insert_mode) render_row(scr.row);
    else textmode_render_cell(scr.row, scr.col);

    if (scr.col == last)
        scr.wrap_pending = scr.autowrap;     // park only if autowrap on
    else
        scr.col++;
}

// ---- VT100 line size, alignment, column mode ----------------------------
void screen_set_line_size(int size) {
    int was = tm_line[scr.row];
    if (size == was) return;
    // Going wide drops the right half of the line, as on the VT100.
    if (was == LINE_SINGLE) {
        cell_t b = blank_cell();
        for (int c = TERM_COLS / 2; c < TERM_COLS; ++c) tm_cells[scr.row][c] = b;
    }
    tm_line[scr.row] = (uint8_t)size;
    clamp_col();
    scr.wrap_pending = 0;
    render_row(scr.row);
}

void screen_alignment(void) {
    for (int r = 0; r < TERM_ROWS; ++r) row_blank(r, (cell_t){ 'E', 7, 0, 0 });
    scr.scroll_top = 0;
    scr.scroll_bot = TERM_ROWS - 1;
    scr.origin_mode = 0;
    textmode_render_all();
    screen_move_to(0, 0);
}

// The VT100 erases the screen and resets the margins on a column-mode change.
// The grid stays 80 columns either way: 132-column mode is not emulated.
void screen_column_mode(void) {
    scr.scroll_top = 0;
    scr.scroll_bot = TERM_ROWS - 1;
    cell_t b = blank_cell();
    for (int r = 0; r < TERM_ROWS; ++r) row_blank(r, b);
    textmode_render_all();
    screen_move_to(0, 0);
}

// ---- Glass-TTY byte sink (kept for the boot banner) ----------------------
void screen_putc(uint8_t c) {
    screen_hide_cursor();
    switch (c) {
        case 0x08: screen_backspace();       break;
        case 0x09: screen_tab();             break;
        case 0x0a: case 0x0b: case 0x0c: screen_line_feed(); break;
        case 0x0d: screen_carriage_return(); break;
        case 0x07:                           break;
        default: if (c >= 0x20) screen_put_glyph(c); break;
    }
    screen_show_cursor();
}
