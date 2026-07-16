// Character-cell text layer rendered into the DVI framebuffer.
// An 80x24 grid of cells (glyph + fg/bg colour + attributes) blitted from the
// 8x16 VGA font. This is the display surface the terminal screen model drives.
#ifndef TEXTMODE_H
#define TEXTMODE_H

#include <stdint.h>
#include "config.h"

// Cell attribute bits (a subset of VT100 SGR rendition).
#define ATTR_BOLD      0x01u   // use the bright palette entry for fg 0-7
#define ATTR_UNDERLINE 0x02u   // underline on the second-to-last glyph row
#define ATTR_BLINK     0x04u   // toggled invisible during the blink phase
#define ATTR_REVERSE   0x08u   // swap fg/bg

typedef struct {
    uint8_t glyph;   // index into font_8x16 (CP437)
    uint8_t fg;      // ANSI colour 0..15
    uint8_t bg;      // ANSI colour 0..15
    uint8_t attr;    // ATTR_* bits
} cell_t;

// The live cell grid (row-major). The screen model writes here.
extern cell_t tm_cells[TERM_ROWS][TERM_COLS];

// Initialise palette, clear the framebuffer border, blank all cells.
void textmode_init(void);

// Blit one cell (or the whole grid) from tm_cells into the framebuffer.
void textmode_render_cell(int row, int col);
void textmode_render_all(void);

// Draw a solid block cursor over a cell (its content, fg/bg swapped).
void textmode_render_cursor(int row, int col);

// Advance the blink phase (call at a steady rate); re-renders blinking cells.
void textmode_set_blink(int on);

// DECSCNM whole-screen reverse video (re-renders everything on change).
void textmode_set_screen_reverse(int on);

// Runtime display settings (driven by the Setup screen).
void textmode_set_theme(int theme);          // THEME_* ; re-renders all
void textmode_set_cursor_style(int style);   // 0 = block, 1 = underline
void textmode_set_flash(int on);             // visual bell: invert everything

// ---- Smooth scroll -------------------------------------------------------
void textmode_set_smooth(int on, int pps);   // enable + speed (pixels/second)
int  textmode_smooth_enabled(void);
void textmode_smooth_line(void);             // animate a one-line scroll-up
int  textmode_scroll_busy(void);             // 1 while a slide is in progress
void textmode_scroll_snap(void);             // finish any slide immediately
void textmode_set_scroll_pace(int backlog);  // speed up the slide when behind

// Convenience for tests/boot banner: write a string starting at (row,col).
void tm_puts(int row, int col, const char *s, uint8_t fg, uint8_t bg, uint8_t attr);

#endif // TEXTMODE_H
