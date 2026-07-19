// Character-cell text layer rendered into an HDMI/DVI framebuffer via DRM/KMS.
// An 80x24 grid of cells (glyph + fg/bg colour + attributes) blitted from the
// 8x16 VGA font. This is the display surface the terminal screen model
// (terminal/screen.c) drives. API is intentionally identical to the Pico
// build's textmode.h so screen.c ports without changes; the implementation
// (textmode.c) is a from-scratch DRM/KMS renderer, not a port.
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

// Open the DRM device, pick the connected display's preferred mode, allocate
// and map a framebuffer, and blank the grid. Exits the process on failure
// (there is no meaningful fallback for a terminal with no display).
void textmode_init(void);

// Rebuild the glyph atlas from the current font (g_settings.font_path) and
// repaint. Used by the Setup menu when the font changes.
void textmode_reload_font(void);

// Blit one cell (or the whole grid) from tm_cells into the framebuffer.
void textmode_render_cell(int row, int col);
void textmode_render_all(void);

// Draw a solid block (or underline) cursor over a cell (fg/bg swapped).
void textmode_render_cursor(int row, int col);

// Advance the blink phase (call at a steady rate); re-renders blinking cells.
void textmode_set_blink(int on);

// DECSCNM whole-screen reverse video (re-renders everything on change).
void textmode_set_screen_reverse(int on);

// Runtime display settings.
void textmode_set_theme(int theme);          // themes.c index; re-renders all
void textmode_set_custom_colors(uint32_t fg, uint32_t bg);  // for the "custom" theme
void textmode_set_cursor_style(int style);   // 0 = block, 1 = underline
void textmode_set_flash(int on);             // visual bell: invert everything

// ---- Smooth scroll ---------------------------------------------------------
// Not yet implemented on VT100-PI-ZERO (the Pico build's smooth scroll exists
// to make the most of a DMA-scanned-out framebuffer; on a Pi Zero 2 W with a
// real GPU this would be better done as a genuine animated blit — future
// work). textmode_smooth_enabled() always reports off, so screen.c's scroll
// path always takes its jump-scroll branch; the rest are correct no-ops.
void textmode_set_smooth(int on, int pps);
int  textmode_smooth_enabled(void);
void textmode_smooth_line(void);
int  textmode_scroll_busy(void);
void textmode_scroll_snap(void);
void textmode_set_scroll_pace(int backlog);

// Convenience for the boot splash: write a string starting at (row,col).
void tm_puts(int row, int col, const char *s, uint8_t fg, uint8_t bg, uint8_t attr);

#endif // TEXTMODE_H
