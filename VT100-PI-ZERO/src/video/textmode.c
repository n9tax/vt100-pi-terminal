// DRM/KMS text-mode renderer. Opens the display, mode-sets to the connected
// monitor's preferred mode, and owns a single dumb buffer that cell/glyph
// blits write into directly. There is no page-flip/double-buffering here —
// a terminal only repaints on state changes (not every frame), so occasional
// tearing on a fast scroll is an acceptable trade for a lot less code than a
// double-buffered swap chain.
#include "video/textmode.h"
#include "video/glyphs.h"
#include "video/themes.h"

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
static uint32_t crtc_id, connector_id;
static uint32_t db_handle[2], db_fbid[2];   // two scanout buffers (double-buffered)
static uint8_t *db_mem[2];
static uint8_t *shadow;                      // RAM render target
static uint8_t *fb_mem;                       // == shadow; all rendering writes here
static uint32_t fb_pitch, fb_width, fb_height, fb_size;
static int front_idx = 0, back_idx = 1;      // which buffer is scanning / free
static int flipped_to = 0, flip_pending = 0; // page-flip in flight bookkeeping
static int dirty = 0;                         // shadow changed since last present

// No fixed cell size: the TERM_COLS x TERM_ROWS grid is stretched to fill the
// entire display. Each cell's pixel rectangle is derived from its row/col (see
// cell_x0/cell_y0), so the grid always spans the whole framebuffer with no
// border. Fractional pixels are spread across cells, so adjacent columns/rows
// differ by at most 1px, and the 8x16 font is nearest-neighbour scaled in.

static int cur_theme = 1;           // amber; overridden from settings at startup
static uint32_t cur_custom_fg = 0xffffff, cur_custom_bg = 0x000000;
static int chrome_on = 0;           // Setup menu: force a readable fixed palette
static int cursor_style = 0;        // 0 = block, 1 = underline
static int screen_reverse = 0;      // DECSCNM
static int flash_on = 0;            // visual bell
static int blink_phase = 0;

// ---- smooth scroll state ---------------------------------------------------
static int smooth_on = 0;           // enabled by settings
static int line_h = 16;             // nominal row height in px (fb_height/ROWS)
static int base_step = 10;          // pan px/frame at the configured speed
static int anim_px = 0;             // pixels left to pan (>0 = a slide in flight)
static int max_anim_px = 0;         // beyond this backlog we jump instead of slide
static int scrolls_this_frame = 0;  // scrolls since the last tick; >1 => burst, jump

// ---- palette ---------------------------------------------------------------
// Fixed high-contrast palette for the Setup menu ("chrome"), so a bad terminal
// theme / custom colour can never make Setup itself unreadable — you can always
// open it and fix things.
static uint32_t chrome_rgb(uint8_t idx) {
    switch (idx & 0xf) {
        case 0:  return 0x00007a;   // background: setup blue
        case 7:  return 0xc8c8c8;   // normal text: light grey
        default: return 0xffffff;   // bold/bright: white
    }
}

// All colour logic lives in themes.c; this resolves an ANSI index for the
// current theme (and the custom fg/bg for the "custom" theme).
static uint32_t palette_rgb(uint8_t idx) {
    if (chrome_on) return chrome_rgb(idx);
    return theme_rgb(cur_theme, idx, cur_custom_fg, cur_custom_bg);
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

    // Two dumb buffers (XRGB8888) for tear-free double-buffered page flips, plus
    // a RAM shadow that all rendering targets. present() copies shadow into the
    // free buffer and flips to it at the vertical blank.
    for (int i = 0; i < 2; ++i) {
        struct drm_mode_create_dumb creq = { .width = fb_width, .height = fb_height, .bpp = 32 };
        if (drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
            fprintf(stderr, "textmode: DRM_IOCTL_MODE_CREATE_DUMB failed\n");
            exit(1);
        }
        db_handle[i] = creq.handle;
        fb_pitch = creq.pitch;
        fb_size = (uint32_t)creq.size;
        if (drmModeAddFB(drm_fd, fb_width, fb_height, 24, 32, fb_pitch, db_handle[i], &db_fbid[i]) < 0) {
            fprintf(stderr, "textmode: drmModeAddFB failed\n");
            exit(1);
        }
        struct drm_mode_map_dumb mreq = { .handle = db_handle[i] };
        if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
            fprintf(stderr, "textmode: DRM_IOCTL_MODE_MAP_DUMB failed\n");
            exit(1);
        }
        db_mem[i] = mmap(0, (size_t)creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, (off_t)mreq.offset);
        if (db_mem[i] == MAP_FAILED) {
            fprintf(stderr, "textmode: mmap framebuffer failed\n");
            exit(1);
        }
        memset(db_mem[i], 0, creq.size);
    }

    shadow = malloc(fb_size);
    if (!shadow) { fprintf(stderr, "textmode: shadow alloc failed\n"); exit(1); }
    memset(shadow, 0, fb_size);
    fb_mem = shadow;
    front_idx = 0; back_idx = 1; flip_pending = 0;

    if (drmModeSetCrtc(drm_fd, crtc_id, db_fbid[0], 0, 0, &connector_id, 1, &mode) < 0) {
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
    dirty = 1;
}

// Pixel bounds of a cell. The grid is stretched across the whole framebuffer,
// so these map the integer grid coordinate onto the display; cell (row,col)
// spans x in [cell_x0(col), cell_x0(col+1)) and y in [cell_y0(row), cell_y0(row+1)).
static inline int cell_x0(int col) { return (int)((long)col * fb_width  / TERM_COLS); }
static inline int cell_y0(int row) { return (int)((long)row * fb_height / TERM_ROWS); }

// Blend fg over bg by an 8-bit coverage/alpha (0 = all bg, 255 = all fg).
static inline uint32_t blend(uint32_t fg, uint32_t bg, int a) {
    int r = (((fg >> 16) & 0xff) * a + ((bg >> 16) & 0xff) * (255 - a)) / 255;
    int g = (((fg >>  8) & 0xff) * a + ((bg >>  8) & 0xff) * (255 - a)) / 255;
    int b = ((( fg      ) & 0xff) * a + (( bg      ) & 0xff) * (255 - a)) / 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// Blit one glyph into cell (row,col)'s pixel rect by sampling its anti-aliased
// coverage atlas (rendered at ~cell size by glyphs.c) and alpha-blending fg/bg.
// The atlas is already smooth, so a nearest sample into the cell rect is fine.
static void blit_glyph(int row, int col, uint8_t glyph, uint32_t fg, uint32_t bg, uint8_t attr) {
    const uint8_t *al = glyph_alpha(glyph);
    int aw = glyph_atlas_w(), ah = glyph_atlas_h();
    int x0 = cell_x0(col), x1 = cell_x0(col + 1);
    int y0 = cell_y0(row), y1 = cell_y0(row + 1);
    int cw = x1 - x0, ch = y1 - y0;
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    int ul = (attr & ATTR_UNDERLINE) ? 1 : 0;
    for (int y = y0; y < y1; ++y) {
        int ay = (y - y0) * ah / ch;
        int uline = ul && (ay >= ah - 2);
        const uint8_t *arow = al + ay * aw;
        for (int x = x0; x < x1; ++x) {
            int ax = (x - x0) * aw / cw;
            int a = uline ? 255 : arow[ax];
            put_px(x, y, a == 0 ? bg : (a == 255 ? fg : blend(fg, bg, a)));
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
        int bar = (y1 - cell_y0(row)) / 12;
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

    // Render the glyph atlas at the display's cell size (rounded up so it
    // covers the widest column). The grid itself is stretched to fill the whole
    // display; blit_glyph samples the atlas into each cell's exact pixel rect.
    int cell_w = ((int)fb_width  + TERM_COLS - 1) / TERM_COLS;
    int cell_h = ((int)fb_height + TERM_ROWS - 1) / TERM_ROWS;
    glyphs_init(cell_w, cell_h);

    line_h = (int)fb_height / TERM_ROWS;
    if (line_h < 1) line_h = 1;
    max_anim_px = line_h * 8;   // >8 lines behind -> jump rather than slide

    for (int r = 0; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c)
            tm_cells[r][c] = (cell_t){ ' ', 7, 0, 0 };
    textmode_render_all();
}

void textmode_reload_font(void) {
    int cell_w = ((int)fb_width  + TERM_COLS - 1) / TERM_COLS;
    int cell_h = ((int)fb_height + TERM_ROWS - 1) / TERM_ROWS;
    glyphs_init(cell_w, cell_h);   // re-renders atlas from g_settings.font_path
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
    if (theme < 0 || theme >= themes_count()) return;
    cur_theme = theme;
    textmode_render_all();
}

void textmode_set_custom_colors(uint32_t fg, uint32_t bg) {
    cur_custom_fg = fg;
    cur_custom_bg = bg;
    if (themes_is_custom(cur_theme)) textmode_render_all();
}

void textmode_set_chrome(int on) { chrome_on = on ? 1 : 0; }

void textmode_set_cursor_style(int style) { cursor_style = style ? 1 : 0; }

void textmode_set_flash(int on) {
    if (flash_on == on) return;
    flash_on = on;
    textmode_render_all();
}

// ---- smooth scroll ---------------------------------------------------------
// A one-line scroll leaves a blank row at the bottom and the (already-rendered)
// content above it. So we slide by panning the framebuffer up a few pixels per
// frame and filling the freshly-exposed strip at the bottom with the background,
// then snap to the exact frame with a full render once the slide settles. The
// main loop drives textmode_scroll_tick() at ~60 Hz while a slide is in flight.
//
// Pacing: each queued line adds line_h to anim_px; the step grows with the
// backlog (up to a whole row per frame) so bursts catch up smoothly rather than
// lagging, and a backlog bigger than max_anim_px jumps outright.

int textmode_smooth_enabled(void) { return smooth_on; }

void textmode_set_smooth(int on, int pps) {
    smooth_on = on ? 1 : 0;
    if (pps > 0) { base_step = pps / 60; if (base_step < 1) base_step = 1; }
    if (!smooth_on) textmode_scroll_snap();
}

void textmode_smooth_line(void) {
    if (!smooth_on) { textmode_render_all(); return; }
    ++scrolls_this_frame;
    // A single scroll per batch slides; two or more in one batch is a burst whose
    // rows overwrite each other at the bottom before the pan runs, so jump to the
    // exact frame instead. Also jump if the backlog is already too deep.
    if (scrolls_this_frame > 1 || anim_px + line_h > max_anim_px) {
        anim_px = 0;
        textmode_render_all();
        return;
    }
    anim_px += line_h;                      // queue one line's worth of slide
}

void textmode_scroll_tick(void) {
    scrolls_this_frame = 0;   // new batch window starts after each tick
    if (anim_px <= 0) return;

    // Constant speed for uniform, non-jerky motion. If output out-runs the slide
    // the backlog grows until it passes max_anim_px, at which point smooth_line
    // jumps to catch up (rather than visibly speeding the slide up and down).
    int step = base_step;
    if (step > anim_px) step = anim_px;
    if (step < 1) step = 1;

    // Pan the whole framebuffer up by `step` pixels.
    memmove(fb_mem, fb_mem + (size_t)step * fb_pitch, (size_t)((int)fb_height - step) * fb_pitch);

    // Fill the newly-exposed bottom strip with the background colour.
    uint32_t bg = palette_rgb(0);
    uint8_t *dst = fb_mem + (size_t)((int)fb_height - step) * fb_pitch;
    for (int y = 0; y < step; ++y) {
        uint32_t *row = (uint32_t *)(dst + (size_t)y * fb_pitch);
        for (int x = 0; x < (int)fb_width; ++x) row[x] = bg;
    }

    dirty = 1;
    anim_px -= step;
    if (anim_px <= 0) { anim_px = 0; textmode_render_all(); }   // snap exact
}

// ---- present / page flip (double-buffered, vsync) --------------------------
static void on_flip(int fd, unsigned seq, unsigned s, unsigned us, void *data) {
    (void)fd; (void)seq; (void)s; (void)us; (void)data;
    front_idx = flipped_to;         // the buffer we flipped to is now scanning out
    back_idx = 1 - front_idx;
    flip_pending = 0;
}

int textmode_drm_fd(void) { return drm_fd; }
int textmode_flip_pending(void) { return flip_pending; }

// Process a page-flip-complete event (call when drm_fd is readable).
void textmode_handle_flip(void) {
    drmEventContext ev = { .version = DRM_EVENT_CONTEXT_VERSION, .page_flip_handler = on_flip };
    drmHandleEvent(drm_fd, &ev);
}

// Copy the shadow to the free buffer and flip to it at the next vblank. No-op if
// nothing changed or a flip is still in flight (keeps us to one frame per vblank).
void textmode_present(void) {
    if (flip_pending || !dirty) return;
    memcpy(db_mem[back_idx], shadow, fb_size);
    dirty = 0;
    flipped_to = back_idx;
    if (drmModePageFlip(drm_fd, crtc_id, db_fbid[back_idx], DRM_MODE_PAGE_FLIP_EVENT, NULL) == 0) {
        flip_pending = 1;
    } else {
        // Flip rejected (driver without flip support, etc.): fall back to writing
        // the front buffer directly so the screen still updates (may tear) rather
        // than freezing. Retry the flip on the next present.
        memcpy(db_mem[front_idx], shadow, fb_size);
    }
}

int  textmode_scroll_busy(void) { return anim_px > 0; }
void textmode_scroll_snap(void) { if (anim_px > 0) { anim_px = 0; textmode_render_all(); } }
void textmode_set_scroll_pace(int backlog) { (void)backlog; }   // pacing derived internally

void tm_puts(int row, int col, const char *s, uint8_t fg, uint8_t bg, uint8_t attr) {
    for (int i = 0; s[i] && col + i < TERM_COLS; ++i) {
        tm_cells[row][col + i] = (cell_t){ (uint8_t)s[i], fg, bg, attr };
        textmode_render_cell(row, col + i);
    }
}
