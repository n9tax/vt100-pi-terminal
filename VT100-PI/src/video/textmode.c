#include "video/textmode.h"
#include "video/hstx_dvi.h"
#include "video/font_8x16.h"

cell_t tm_cells[TERM_ROWS][TERM_COLS];

// ---- Smooth scroll -------------------------------------------------------
// The framebuffer is a vertical ring of FB_STORE_H rows. render_row is the phys
// row where logical row 0 is drawn; it advances one cell height per scroll and
// we redraw ONLY the incoming line into the freshly exposed spare. The scanout
// origin then eases up to render_row — but that easing runs in the video IRQ
// (see hstx_dvi.c), frame-locked, so it stays smooth no matter how busy core0
// is. render_row and the scan origin agree whenever a slide has settled.
static uint32_t render_row  = 0;        // phys row of logical row 0 (free-running)
static int      smooth_on   = 0;
static int      smooth_step = 4;        // pan pixels per frame (from speed setting)

#if VIDEO_MODE == 2
// Smooth scroll pans the whole scanned framebuffer window, which only works when
// the text fills the full height (TEXT_Y0 == 0). Mode 2 has 9px top/bottom
// borders, so force jump scroll there (the borders would otherwise scroll too).
int  textmode_smooth_enabled(void) { return 0; }
#else
int  textmode_smooth_enabled(void) { return smooth_on; }
#endif
void textmode_set_smooth(int on, int pps) {
    smooth_on = on ? 1 : 0;
    // The IRQ steps the pan once per 60 Hz frame, so convert pixels/second into
    // pixels/frame (at least 1).
    if (pps > 0) { smooth_step = pps / 60; if (smooth_step < 1) smooth_step = 1; }
    if (!smooth_on) textmode_scroll_snap();   // settle any in-flight slide
}

// Standard 16-colour ANSI/xterm palette, precomputed to RGB332.
static uint8_t pal[16];
static int blink_phase = 0;   // when set, ATTR_BLINK cells render as blank
static int screen_reverse = 0; // DECSCNM: invert the whole screen
static int cursor_style = 0;   // 0 = block, 1 = underline
static int flash_on = 0;       // visual bell: momentarily invert everything

#if FB_BPP != 2 && FB_BPP != 4
static const uint8_t pal_rgb[16][3] = {
    {  0,  0,  0}, {170,  0,  0}, {  0,170,  0}, {170, 85,  0},
    {  0,  0,170}, {170,  0,170}, {  0,170,170}, {170,170,170},
    { 85, 85, 85}, {255, 85, 85}, { 85,255, 85}, {255,255, 85},
    { 85, 85,255}, {255, 85,255}, { 85,255,255}, {255,255,255},
};
#endif

// Fill for the pillarbox side borders (outside the text area). Black for most
// themes; the C64 look uses a light-blue border. In 2bpp it is a brightness.
static uint8_t border_px = 0;

#if FB_BPP == 2 || FB_BPP == 4
// ---- Monochrome palette --------------------------------------------------
// Each ANSI colour maps to a brightness stored in the pixel's low bits; the top
// bit is kept 0 (a guaranteed-zero reference the HSTX expander uses to build
// clean OFF/HALF colour lanes). The phosphor HUE (amber/green/white/...) is
// applied by the expander in hardware, so this table is the same for every mono
// theme: background = 0, foreground = full brightness. In 2bpp that is on/off
// (1 bit). In 4bpp it is a 3-bit 0..7 brightness, which lets the renderer place
// intermediate values on glyph edges -> anti-aliased text.
static void build_theme(int theme) {
    (void)theme;
#if FB_BPP == 4
    pal[0] = 0;
    for (int i = 1; i < 16; ++i) pal[i] = 7;   // full brightness (8 levels)
#else
    pal[0] = 0;
    for (int i = 1; i < 16; ++i) pal[i] = 1;   // on/off
#endif
    border_px = 0;
}

// The text is centred with black borders (or fills exactly): nothing to paint.
static void fill_border(void) { }

#else
// ---- 8bpp RGB332 palette -------------------------------------------------
// Build a monochrome phosphor palette: colour 0 is the screen background; every
// other ANSI colour maps to a shade of (base_r,base_g,base_b) scaled by its
// brightness, so bold/bright text glows brighter and dim colours are darker —
// like an amber or green CRT, while still tracking an app's intended contrast.
// bg_rgb/border_rgb are packed 0xRRGGBB for the background and border.
static void build_mono_bg(uint8_t br, uint8_t bg_, uint8_t bb,
                          uint32_t bg_rgb, uint32_t border_rgb) {
    pal[0] = rgb332((bg_rgb >> 16) & 0xff, (bg_rgb >> 8) & 0xff, bg_rgb & 0xff);
    for (int i = 1; i < 16; ++i) {
        int lum = (30 * pal_rgb[i][0] + 59 * pal_rgb[i][1] + 11 * pal_rgb[i][2]) / 100;
        int v = 110 + lum * 145 / 255;           // 110..255: default text stays bright
        pal[i] = rgb332((uint8_t)(br * v / 255),
                        (uint8_t)(bg_ * v / 255),
                        (uint8_t)(bb * v / 255));
    }
    border_px = rgb332((border_rgb >> 16) & 0xff, (border_rgb >> 8) & 0xff, border_rgb & 0xff);
}
static void build_mono(uint8_t br, uint8_t bg_, uint8_t bb) {
    build_mono_bg(br, bg_, bb, 0x000000, 0x000000);   // black screen + border
}

static void build_theme(int theme) {
    switch (theme) {
        case THEME_AMBER:  build_mono(255, 176,   0); break;   // P3 amber
        case THEME_GREEN:  build_mono( 32, 255,  32); break;   // P1 green (pure)
        case THEME_WHITE:  build_mono(255, 255, 255); break;   // white
        case THEME_BLUE:   build_mono( 64, 128, 255); break;   // blue phosphor
        case THEME_RED:    build_mono(255,  48,  32); break;   // red phosphor
        case THEME_YELLOW: build_mono(255, 224,  32); break;   // yellow phosphor
        case THEME_COMMODORE:                                  // C64, brightened for readability
            build_mono_bg(190, 200, 255, 0x2A2280, 0x8078D0); break;
        case THEME_BORLAND:                                    // Turbo/Borland IDE: yellow on DOS blue
            build_mono_bg(255, 255,  80, 0x0000A8, 0x0000A8); break;
        default:                                               // full colour
            for (int i = 0; i < 16; ++i)
                pal[i] = rgb332(pal_rgb[i][0], pal_rgb[i][1], pal_rgb[i][2]);
            border_px = rgb332(0, 0, 0);
            break;
    }
}

// Paint the pillarbox side borders (and the smooth-scroll spare strip margins)
// in the current border colour. The text area itself is covered by cell renders.
static void fill_border(void) {
    for (int y = 0; y < FB_STORE_H; ++y) {
        uint8_t *row = &dvi_framebuf[y * FB_WIDTH];
        for (int x = 0; x < TEXT_X0; ++x)               row[x] = border_px;
        for (int x = TEXT_X0 + TEXT_W; x < FB_WIDTH; ++x) row[x] = border_px;
    }
}
#endif

// Rebuild the palette and repaint the border. The caller re-renders the cells
// (avoids a double full-screen render burst, which can momentarily starve HSTX).
void textmode_set_theme(int theme) {
    build_theme(theme);
    fill_border();
#if FB_BPP == 2 || FB_BPP == 4
    hstx_dvi_set_tint(theme);   // apply the phosphor hue in the HSTX expander
#endif
}

#if FB_BPP == 4
// Precomputed source coordinate + bilinear fraction for the fixed GLYPH->CELL
// upscale, so the anti-aliased render has no per-pixel divides. src[] is the
// floor source pixel; frac[] is the blend weight to src+1 in eighths (0..7).
static uint8_t sxi_tab[CELL_W], fx_tab[CELL_W];
static uint8_t syi_tab[CELL_H], fy_tab[CELL_H];
static void axis_table(uint8_t *si, uint8_t *fr, int cells, int glyph) {
    for (int d = 0; d < cells; ++d) {
        int c8 = ((2 * d + 1) * glyph * 8) / (2 * cells) - 4;  // (d+.5)*g/c*8 - 0.5
        int i = c8 >> 3, f = c8 & 7;
        if (i < 0)          { i = 0;         f = 0; }
        if (i >= glyph - 1) { i = glyph - 1; f = 0; }          // no right neighbour
        si[d] = (uint8_t)i; fr[d] = (uint8_t)f;
    }
}
static void build_scale_tables(void) {
    axis_table(sxi_tab, fx_tab, CELL_W, GLYPH_W);
    axis_table(syi_tab, fy_tab, CELL_H, GLYPH_H);
}
#endif

void textmode_set_cursor_style(int style) {
    cursor_style = style;
}

void textmode_set_flash(int on) {
    if (on == flash_on) return;
    flash_on = on;
    textmode_render_all();
}

void textmode_init(void) {
    build_theme(THEME_DEFAULT);
#if FB_BPP == 4
    build_scale_tables();
#endif

    // Fill the whole framebuffer (incl. the border and the smooth-scroll spare
    // strip) with the background. pal[0] == 0 in 2bpp (a byte of 0x00 is four
    // level-0 pixels) and the black RGB332 byte in 8bpp, so a plain byte fill
    // works for both. FB_STRIDE bytes/row (== FB_WIDTH at 8bpp).
    uint8_t black = pal[0];
    for (int i = 0; i < FB_STRIDE * FB_STORE_H; ++i)
        dvi_framebuf[i] = black;

    for (int r = 0; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c)
            tm_cells[r][c] = (cell_t){ ' ', 7, 0, 0 };
    textmode_render_all();
}

// Render one cell. `cursor` draws a solid block cursor (extra fg/bg swap) over
// the cell's real content without disturbing the stored cell.
static void render(int row, int col, int cursor) {
    if (row < 0 || row >= TERM_ROWS || col < 0 || col >= TERM_COLS) return;
    const cell_t *cell = &tm_cells[row][col];

    uint8_t fg = cell->fg & 0x0f;
    uint8_t bg = cell->bg & 0x0f;
    if ((cell->attr & ATTR_BOLD) && fg < 8) fg += 8;     // bold -> bright fg
    int cursor_block = cursor && cursor_style == 0;
    int cursor_uline = cursor && cursor_style == 1;
    int rev = ((cell->attr & ATTR_REVERSE) ? 1 : 0) ^ (cursor_block ? 1 : 0)
              ^ (screen_reverse ? 1 : 0) ^ (flash_on ? 1 : 0);
    if (rev) { uint8_t t = fg; fg = bg; bg = t; }

    uint8_t fg_px = pal[fg], bg_px = pal[bg];
    int blanked = (cell->attr & ATTR_BLINK) && blink_phase;

    const uint8_t *glyph = &font_8x16[cell->glyph * FONT_H];
    int x0 = TEXT_X0 + col * CELL_W;
    int y0 = TEXT_Y0 + row * CELL_H;
    uint32_t ro = render_row % FB_STORE_H;       // ring origin of logical row 0

#if FB_BPP == 4
    // 4bpp: 2 pixels per byte (low nibble = left pixel). The 8x16 VGA glyph is
    // bilinearly scaled up into the CELL_W x CELL_H cell, producing intermediate
    // brightness (0..7) on edges -> anti-aliased amber. fg_px/bg_px are the two
    // brightness endpoints; each dest pixel blends them by its glyph coverage.
    int bx0 = x0 >> 1;                            // first framebuffer byte (2 px/byte)
    int fgl = fg_px, bgl = bg_px;
    for (int dy = 0; dy < CELL_H; ++dy) {
        int syi = syi_tab[dy], fy = fy_tab[dy];
        uint8_t r0, r1;
        if (blanked) { r0 = 0; r1 = 0; }
        else { r0 = glyph[syi]; r1 = (syi + 1 < GLYPH_H) ? glyph[syi + 1] : r0; }
        int under = (cell->attr & ATTR_UNDERLINE) && syi == GLYPH_H - 2;
        int cur_u = cursor_uline && dy >= CELL_H - 3;
        uint32_t py = (uint32_t)(y0 + dy) + ro;
        if (py >= FB_STORE_H) py -= FB_STORE_H;
        uint8_t *dst = &dvi_framebuf[py * FB_STRIDE + bx0];
        for (int b = 0; b < CELL_W / 2; ++b) {
            uint8_t out = 0;
            for (int half = 0; half < 2; ++half) {
                int dx = 2 * b + half;
                int val;
                if (under || cur_u) {
                    val = fgl;
                } else {
                    int sxi = sxi_tab[dx], fx = fx_tab[dx];
                    int b00 = (r0 >> (7 - sxi)) & 1;
                    int b01 = (r1 >> (7 - sxi)) & 1;
                    int rn  = sxi + 1 < GLYPH_W;
                    int b10 = rn ? (r0 >> (6 - sxi)) & 1 : 0;
                    int b11 = rn ? (r1 >> (6 - sxi)) & 1 : 0;
                    int cov = b00 * (8 - fx) * (8 - fy) + b10 * fx * (8 - fy)
                            + b01 * (8 - fx) * fy       + b11 * fx * fy;   // 0..64
                    val = (fgl * cov + bgl * (64 - cov)) >> 6;             // 0..7
                }
                out |= (uint8_t)(val << (half * 4));
            }
            dst[b] = out;
        }
    }
#elif FB_BPP == 2
    // 2bpp: 4 pixels per byte, pixel k of the byte at bits [2k+1:2k] (leftmost
    // pixel = low bits, matching the HSTX expand order). CELL_W is a multiple of
    // 4 and x0 is /4-aligned, so each cell row maps onto whole bytes -> we can
    // build them without a read-modify-write of neighbouring cells. The 8x16 VGA
    // glyph is nearest-neighbour scaled up into the CELL_W x CELL_H cell to fill
    // the 80x25 screen.
    int bx0 = x0 >> 2;                           // first framebuffer byte of cell
    for (int dy = 0; dy < CELL_H; ++dy) {
        int sy = dy * GLYPH_H / CELL_H;          // source row 0..GLYPH_H-1
        uint8_t bits = blanked ? 0 : glyph[sy];
        if ((cell->attr & ATTR_UNDERLINE) && sy == GLYPH_H - 2) bits = 0xff;
        if (cursor_uline && dy >= CELL_H - 3) bits = 0xff;   // underline cursor
        uint32_t py = (uint32_t)(y0 + dy) + ro;  // ring-map into the framebuffer
        if (py >= FB_STORE_H) py -= FB_STORE_H;
        uint8_t *dst = &dvi_framebuf[py * FB_STRIDE + bx0];
        for (int b = 0; b < CELL_W / 4; ++b) {
            uint8_t packed = 0;
            for (int p = 0; p < 4; ++p) {
                int dx = b * 4 + p;
                int sx = dx * GLYPH_W / CELL_W;  // source col 0..GLYPH_W-1
                uint8_t v = (bits & (0x80u >> sx)) ? fg_px : bg_px;
                packed |= (uint8_t)(v << (p * 2));
            }
            dst[b] = packed;
        }
    }
#else
    // 8bpp: nearest-neighbour scale the GLYPH_W x GLYPH_H source into CELL_W x CELL_H.
    for (int dy = 0; dy < CELL_H; ++dy) {
        int sy = dy * GLYPH_H / CELL_H;          // source row 0..GLYPH_H-1
        uint8_t bits = blanked ? 0 : glyph[sy];
        if ((cell->attr & ATTR_UNDERLINE) && sy == GLYPH_H - 2) bits = 0xff;
        if (cursor_uline && dy >= CELL_H - 3) bits = 0xff;   // underline cursor
        uint32_t py = (uint32_t)(y0 + dy) + ro;  // ring-map into the framebuffer
        if (py >= FB_STORE_H) py -= FB_STORE_H;
        uint8_t *row_px = &dvi_framebuf[py * FB_WIDTH + x0];
        for (int dx = 0; dx < CELL_W; ++dx) {
            int sx = dx * GLYPH_W / CELL_W;      // source col 0..GLYPH_W-1
            row_px[dx] = (bits & (0x80u >> sx)) ? fg_px : bg_px;
        }
    }
#endif
}

void textmode_render_cell(int row, int col)   { render(row, col, 0); }
void textmode_render_cursor(int row, int col) { render(row, col, 1); }

void textmode_set_screen_reverse(int on) {
    if (on == screen_reverse) return;
    screen_reverse = on;
    textmode_render_all();
}

void textmode_render_all(void) {
    for (int r = 0; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c)
            textmode_render_cell(r, c);
}

void textmode_set_blink(int on) {
    if (on == blink_phase) return;
    blink_phase = on;
    for (int r = 0; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c)
            if (tm_cells[r][c].attr & ATTR_BLINK)
                textmode_render_cell(r, c);
}

void tm_puts(int row, int col, const char *s, uint8_t fg, uint8_t bg, uint8_t attr) {
    for (; *s && col < TERM_COLS; ++s, ++col) {
        tm_cells[row][col] = (cell_t){ (uint8_t)*s, fg, bg, attr };
        textmode_render_cell(row, col);
    }
}

// ---- Smooth scroll animation ---------------------------------------------
// The actual pixel-by-pixel pan lives in the video IRQ (hstx_dvi.c); these just
// drive it. busy/snap mirror the IRQ pan state.
int  textmode_scroll_busy(void) { return hstx_dvi_scroll_busy(); }
void textmode_scroll_snap(void) { hstx_dvi_scroll_snap(); }

// Catch-up pacing: when the host out-runs smooth scroll, slide faster (bigger
// per-frame steps — still pixel-smooth) rather than jumping. Ramps from the
// configured speed up to a whole line per frame as the input backlog grows.
static int pace_step = 4;
void textmode_set_scroll_pace(int backlog) {
    // Dead-zone: keep the configured (constant, smooth) step until the backlog is
    // clearly growing, so ordinary scrolling never micro-varies its speed. Only
    // when we are genuinely falling behind do we slide faster to catch up.
    int s = smooth_step;
    if (backlog > 256) s += (backlog - 256) / 48;
    if (s > CELL_H) s = CELL_H;
    pace_step = s;
}

// Begin sliding up one line. The caller (screen model) has already shifted the
// cell grid up and blanked the new bottom row; we advance the render origin and
// draw ONLY that incoming row into the freshly exposed spare — the other rows
// are already in the framebuffer and simply re-map under the new origin. The IRQ
// then eases the scan origin up to meet it.
void textmode_smooth_line(void) {
    render_row += CELL_H;
    for (int c = 0; c < TERM_COLS; ++c) textmode_render_cell(TERM_ROWS - 1, c);
    hstx_dvi_scroll_line((uint32_t)pace_step);
}
