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
void textmode_set_chrome(int on);            // force the fixed Setup-menu palette
void textmode_set_cursor_style(int style);   // 0 = block, 1 = underline
void textmode_set_flash(int on);             // visual bell: invert everything

// ---- Smooth scroll ---------------------------------------------------------
// A one-line whole-screen scroll can slide instead of jumping: screen.c calls
// textmode_smooth_line() (which queues a pixel slide), and the main loop drives
// textmode_scroll_tick() at ~60 Hz to pan the framebuffer up a few pixels per
// frame, catching up faster when lines pile up. See textmode.c.
void textmode_set_smooth(int on, int pps);   // enable + pixels/second
int  textmode_smooth_enabled(void);
void textmode_smooth_line(void);             // queue one line's slide
void textmode_scroll_tick(void);             // advance the slide one frame (main loop)
int  textmode_scroll_busy(void);             // 1 while a slide is in flight
void textmode_scroll_snap(void);             // finish any slide immediately
void textmode_set_scroll_pace(int backlog);  // (pacing is derived internally)

// ---- Double-buffered presentation (vsync page flips) -----------------------
// Rendering targets an off-screen shadow; textmode_present() copies it to a free
// buffer and flips at the vblank, so the display never shows a half-drawn frame.
// The main loop polls textmode_drm_fd() and calls textmode_handle_flip() on
// readable, advances the frame, then calls textmode_present().
int  textmode_drm_fd(void);        // add to the main poll() set
void textmode_handle_flip(void);   // call when drm_fd is readable
void textmode_present(void);       // publish the shadow (copy + flip if changed)
int  textmode_flip_pending(void);  // 1 while a flip is awaiting vblank

// Convenience for the boot splash: write a string starting at (row,col).
void tm_puts(int row, int col, const char *s, uint8_t fg, uint8_t bg, uint8_t attr);

#endif // TEXTMODE_H
