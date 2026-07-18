// DRM/KMS text-mode renderer. Opens the display, mode-sets to the connected
// monitor's preferred mode, and owns a single dumb buffer that cell/glyph
// blits write into directly. There is no page-flip/double-buffering here —
// a terminal only repaints on state changes (not every frame), so occasional
// tearing on a fast scroll is an acceptable trade for a lot less code than a
// double-buffered swap chain.
#include "video/textmode.h"
#include "video/font_8x16.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm.h>
#include <drm_mode.h>

cell_t tm_cells[TERM_ROWS][TERM_COLS];

static int drm_fd = -1;
static uint32_t crtc_id, connector_id, fb_id, dumb_handle;
static uint8_t *fb_mem;
static uint32_t fb_pitch, fb_width, fb_height;

// No fixed cell size: the TERM_COLS x TERM_ROWS grid is stretched to fill the
// entire display. Each cell's pixel rectangle is derived from its row/col (see
// cell_x0/cell_y0), so the grid always spans the whole framebuffer with no
// border. Fractional pixels are spread across cells, so adjacent columns/rows
// differ by at most 1px, and the 8x16 font is nearest-neighbour scaled in.

static int cur_theme = THEME_DEFAULT;
static int cursor_style = 0;        // 0 = block, 1 = underline
static int screen_reverse = 0;      // DECSCNM
static int flash_on = 0;            // visual bell
static int blink_phase = 0;

// ---- palette ---------------------------------------------------------------
// ANSI 0..15 -> 0xRRGGBB, used directly for THEME_COLOR.
static const uint32_t ansi_palette[16] = {
    0x000000, 0xaa0000, 0x00aa00, 0xaa5500, 0x0000aa, 0xaa00aa, 0x00aaaa, 0xaaaaaa,
    0x555555, 0xff5555, 0x55ff55, 0xffff55, 0x5555ff, 0xff55ff, 0x55ffff, 0xffffff,
};

// Monochrome phosphor themes: every ANSI index maps to a shade of one hue by
// its perceived brightness (bold/bright indices are brighter shades).
static uint32_t phosphor_hue(int theme) {
    switch (theme) {
        case THEME_AMBER:  return 0xffb000;
        case THEME_GREEN:  return 0x33ff33;
        case THEME_WHITE:  return 0xffffff;
        case THEME_BLUE:   return 0x66aaff;
        case THEME_RED:    return 0xff3333;
        case THEME_YELLOW: return 0xffee33;
        default:           return 0xffffff;
    }
}

static uint32_t scale_rgb(uint32_t rgb, int pct) {
    int r = ((rgb >> 16) & 0xff) * pct / 100;
    int g = ((rgb >> 8)  & 0xff) * pct / 100;
    int b = (rgb & 0xff) * pct / 100;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t palette_rgb(uint8_t idx) {
    if (cur_theme == THEME_COLOR) return ansi_palette[idx & 0xf];
    // Monochrome: brightness by ANSI index (0=off .. 15=full), independent hue.
    static const int brightness[16] = {
        0, 55, 55, 55, 55, 55, 55, 70,
        40, 100, 100, 100, 100, 100, 100, 100,
    };
    return scale_rgb(phosphor_hue(cur_theme), brightness[idx & 0xf]);
}

// ---- DRM setup ---------------------------------------------------------
static void open_display(void) {
    static const char *candidates[] = { "/dev/dri/card0", "/dev/dri/card1", "/dev/dri/card2" };
    drmModeRes *res = NULL;

    for (unsigned i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        int fd = open(candidates[i], O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        res = drmModeGetResources(fd);
        if (res) { drm_fd = fd; break; }
        close(fd);
    }
    if (!res) {
        fprintf(stderr, "textmode: no usable DRM device found under /dev/dri\n");
        exit(1);
    }

    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; ++i) {
        drmModeConnector *c = drmModeGetConnector(drm_fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) { conn = c; break; }
        if (c) drmModeFreeConnector(c);
    }
    if (!conn) {
        fprintf(stderr, "textmode: no connected display found\n");
        exit(1);
    }
    connector_id = conn->connector_id;
    drmModeModeInfo mode = conn->modes[0];   // preferred mode is first
    fb_width = mode.hdisplay;
    fb_height = mode.vdisplay;

    drmModeEncoder *enc = conn->encoder_id ? drmModeGetEncoder(drm_fd, conn->encoder_id) : NULL;
    if (enc && enc->crtc_id) {
        crtc_id = enc->crtc_id;
    } else if (conn->count_encoders > 0) {
        // Fall back to the first crtc compatible with this connector's first encoder.
        drmModeEncoder *e0 = drmModeGetEncoder(drm_fd, conn->encoders[0]);
        if (e0) {
            for (int i = 0; i < res->count_crtcs; ++i)
                if (e0->possible_crtcs & (1 << i)) { crtc_id = res->crtcs[i]; break; }
            drmModeFreeEncoder(e0);
        }
    }
    if (crtc_id == 0) {
        fprintf(stderr, "textmode: could not find a usable crtc for the connected display\n");
        exit(1);
    }
    if (enc) drmModeFreeEncoder(enc);

    // Dumb buffer: XRGB8888.
    struct drm_mode_create_dumb creq = { .width = fb_width, .height = fb_height, .bpp = 32 };
    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
        fprintf(stderr, "textmode: DRM_IOCTL_MODE_CREATE_DUMB failed\n");
        exit(1);
    }
    dumb_handle = creq.handle;
    fb_pitch = creq.pitch;

    if (drmModeAddFB(drm_fd, fb_width, fb_height, 24, 32, fb_pitch, dumb_handle, &fb_id) < 0) {
        fprintf(stderr, "textmode: drmModeAddFB failed\n");
        exit(1);
    }

    struct drm_mode_map_dumb mreq = { .handle = dumb_handle };
    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
        fprintf(stderr, "textmode: DRM_IOCTL_MODE_MAP_DUMB failed\n");
        exit(1);
    }
    fb_mem = mmap(0, (size_t)creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, (off_t)mreq.offset);
    if (fb_mem == MAP_FAILED) {
        fprintf(stderr, "textmode: mmap framebuffer failed\n");
        exit(1);
    }
    memset(fb_mem, 0, creq.size);

    if (drmModeSetCrtc(drm_fd, crtc_id, fb_id, 0, 0, &connector_id, 1, &mode) < 0) {
        fprintf(stderr, "textmode: drmModeSetCrtc failed\n");
        exit(1);
    }

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
}

// ---- pixel/glyph blit -------------------------------------------------------
static inline void put_px(int x, int y, uint32_t rgb) {
    if ((unsigned)x >= fb_width || (unsigned)y >= fb_height) return;
    *(uint32_t *)(fb_mem + (uint32_t)y * fb_pitch + (uint32_t)x * 4) = rgb;
}

// Pixel bounds of a cell. The grid is stretched across the whole framebuffer,
// so these map the integer grid coordinate onto the display; cell (row,col)
// spans x in [cell_x0(col), cell_x0(col+1)) and y in [cell_y0(row), cell_y0(row+1)).
static inline int cell_x0(int col) { return (int)((long)col * fb_width  / TERM_COLS); }
static inline int cell_y0(int row) { return (int)((long)row * fb_height / TERM_ROWS); }

// Blend fg over bg by an SS*SS-step coverage fraction (0 = all bg, SS*SS = all fg).
#define SS 4   // supersampling grid per destination pixel (4x4 -> 17 blend levels)
static inline uint32_t blend(uint32_t fg, uint32_t bg, int cov) {
    int d = SS * SS;
    int r = (((fg >> 16) & 0xff) * cov + ((bg >> 16) & 0xff) * (d - cov)) / d;
    int g = (((fg >>  8) & 0xff) * cov + ((bg >>  8) & 0xff) * (d - cov)) / d;
    int b = ((( fg      ) & 0xff) * cov + (( bg      ) & 0xff) * (d - cov)) / d;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// Anti-aliased blit of one 8x16 glyph into cell (row,col)'s pixel rect. Each
// destination pixel is supersampled SS x SS times against the font bitmap and
// the fg/bg colours blended by the coverage, so the non-integer upscale reads
// as smooth VGA-style text rather than blocky nearest-neighbour stairsteps.
static void blit_glyph(int row, int col, uint8_t glyph, uint32_t fg, uint32_t bg, uint8_t attr) {
    const uint8_t *rows = &font_8x16[(unsigned)glyph * FONT_H];
    int x0 = cell_x0(col), x1 = cell_x0(col + 1);
    int y0 = cell_y0(row), y1 = cell_y0(row + 1);
    int cw = x1 - x0, ch = y1 - y0;
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    int ul = (attr & ATTR_UNDERLINE) ? 1 : 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            int cov = 0;
            for (int j = 0; j < SS; ++j) {
                int fy = (((y - y0) * SS + j) * FONT_H) / (ch * SS);   // font row 0..FONT_H-1
                uint8_t bits = rows[fy];
                int uline = ul && (fy == FONT_H - 2);
                for (int i = 0; i < SS; ++i) {
                    int fx = (((x - x0) * SS + i) * 8) / (cw * SS);     // font col 0..7
                    if (uline || ((bits >> (7 - fx)) & 1)) cov++;
                }
            }
            put_px(x, y, cov == 0 ? bg : (cov == SS * SS ? fg : blend(fg, bg, cov)));
        }
    }
}

static void draw_cell(int row, int col, int cursor) {
    cell_t c = tm_cells[row][col];
    int hidden = (c.attr & ATTR_BLINK) && blink_phase;
    uint8_t glyph = hidden ? ' ' : c.glyph;

    int rev = ((c.attr & ATTR_REVERSE) ? 1 : 0) ^ screen_reverse ^ flash_on ^ (cursor ? 1 : 0);
    uint8_t fgi = c.fg, bgi = c.bg;
    if (rev) { uint8_t t = fgi; fgi = bgi; bgi = t; }

    uint32_t fg = palette_rgb((c.attr & ATTR_BOLD) ? (fgi | 8) : fgi);
    uint32_t bg = palette_rgb(bgi);

    if (cursor && cursor_style == 1) {
        // Underline cursor: draw the glyph normally, then an underline bar
        // ~2 font rows thick, scaled to this cell's height.
        blit_glyph(row, col, glyph, palette_rgb(fgi), palette_rgb(bgi), c.attr);
        int x0 = cell_x0(col), x1 = cell_x0(col + 1);
        int y1 = cell_y0(row + 1);
        int bar = (y1 - cell_y0(row)) * 2 / FONT_H;
        if (bar < 1) bar = 1;
        for (int y = y1 - bar; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                put_px(x, y, palette_rgb(fgi));
        return;
    }
    blit_glyph(row, col, glyph, fg, bg, c.attr);
}

// ---- public API --------------------------------------------------------
void textmode_init(void) {
    open_display();

    // No cell-size setup needed: the grid is stretched to fill the whole
    // display (see cell_x0/cell_y0 and blit_glyph). Just clear and paint.
    for (int r = 0; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c)
            tm_cells[r][c] = (cell_t){ ' ', 7, 0, 0 };
    textmode_render_all();
}

void textmode_render_cell(int row, int col) { draw_cell(row, col, 0); }
void textmode_render_cursor(int row, int col) { draw_cell(row, col, 1); }

void textmode_render_all(void) {
    for (int r = 0; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c)
            textmode_render_cell(r, c);
}

void textmode_set_blink(int on) {
    if (blink_phase == on) return;
    blink_phase = on;
    // Cheap on a Pi Zero 2 W: just redraw everything rather than tracking
    // which cells have ATTR_BLINK set.
    textmode_render_all();
}

void textmode_set_screen_reverse(int on) {
    if (screen_reverse == on) return;
    screen_reverse = on;
    textmode_render_all();
}

void textmode_set_theme(int theme) {
    if (theme < 0 || theme >= THEME_COUNT) return;
    cur_theme = theme;
    textmode_render_all();
}

void textmode_set_cursor_style(int style) { cursor_style = style ? 1 : 0; }

void textmode_set_flash(int on) {
    if (flash_on == on) return;
    flash_on = on;
    textmode_render_all();
}

// ---- smooth scroll: not implemented on VT100-PI-ZERO yet (see textmode.h) --
void textmode_set_smooth(int on, int pps) { (void)on; (void)pps; }
int  textmode_smooth_enabled(void) { return 0; }
void textmode_smooth_line(void) {}
int  textmode_scroll_busy(void) { return 0; }
void textmode_scroll_snap(void) {}
void textmode_set_scroll_pace(int backlog) { (void)backlog; }

void tm_puts(int row, int col, const char *s, uint8_t fg, uint8_t bg, uint8_t attr) {
    for (int i = 0; s[i] && col + i < TERM_COLS; ++i) {
        tm_cells[row][col + i] = (cell_t){ (uint8_t)s[i], fg, bg, attr };
        textmode_render_cell(row, col + i);
    }
}
