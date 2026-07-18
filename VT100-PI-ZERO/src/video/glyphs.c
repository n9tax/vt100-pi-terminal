// FreeType-backed glyph atlas. See glyphs.h. Renders each CP437 code point to
// an 8-bit coverage cell, squeezed to the display's cell aspect (so 80 columns
// fill the width) and vertically centred on the font's baseline.
#include "video/glyphs.h"
#include "config.h"
#include "settings.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ASSET_FONT_PATH
#define ASSET_FONT_PATH ""   // normally set by CMake to the bundled TTF
#endif

#define GLYPH_COUNT 256

static int aw, ah;            // atlas cell size (pixels)
static uint8_t *atlas;        // GLYPH_COUNT * aw * ah coverage bytes

// ---- CP437 -> Unicode ------------------------------------------------------
// 0x20..0x7E are ASCII (identity); the control range and high half are the
// classic IBM PC code page 437 glyphs (smilies, arrows, box drawing, Greek).
static uint16_t cp437[256];

static void build_cp437(void) {
    for (int i = 0; i < 256; ++i) cp437[i] = (uint16_t)i;   // ASCII identity
    static const uint16_t lo[32] = {
        0x0000,0x263A,0x263B,0x2665,0x2666,0x2663,0x2660,0x2022,
        0x25D8,0x25CB,0x25D9,0x2642,0x2640,0x266A,0x266B,0x263C,
        0x25BA,0x25C4,0x2195,0x203C,0x00B6,0x00A7,0x25AC,0x21A8,
        0x2191,0x2193,0x2192,0x2190,0x221F,0x2194,0x25B2,0x25BC,
    };
    for (int i = 0; i < 32; ++i) cp437[i] = lo[i];
    cp437[0x7F] = 0x2302;
    static const uint16_t hi[128] = {
        0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,
        0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
        0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,
        0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
        0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,
        0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
        0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,
        0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
        0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,
        0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
        0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,
        0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
        0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,
        0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
        0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,
        0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0,
    };
    for (int i = 0; i < 128; ++i) cp437[0x80 + i] = hi[i];
}

// ---- procedural block / box-drawing glyphs ---------------------------------
// A normal mono font's line-drawing glyphs don't span a squeezed cell, so
// adjacent cells wouldn't connect. Draw CP437's block/shade/box region
// (0xB0-0xDF) directly instead, edge-to-edge, so it tiles seamlessly.
//
// box_arms packs the four arms of each line glyph, 2 bits each as U,D,L,R,
// value 0=none 1=single 2=double. 0 = not a line glyph.
static const uint8_t box_arms[256] = {
    [0xB3]=0x50,[0xB4]=0x54,[0xB5]=0x58,[0xB6]=0xA4,[0xB7]=0x24,[0xB8]=0x18,
    [0xB9]=0xA8,[0xBA]=0xA0,[0xBB]=0x28,[0xBC]=0x88,[0xBD]=0x84,[0xBE]=0x48,
    [0xBF]=0x14,[0xC0]=0x41,[0xC1]=0x45,[0xC2]=0x15,[0xC3]=0x51,[0xC4]=0x05,
    [0xC5]=0x55,[0xC6]=0x52,[0xC7]=0xA1,[0xC8]=0x82,[0xC9]=0x22,[0xCA]=0x8A,
    [0xCB]=0x2A,[0xCC]=0xA2,[0xCD]=0x0A,[0xCE]=0xAA,[0xCF]=0x4A,[0xD0]=0x85,
    [0xD1]=0x1A,[0xD2]=0x25,[0xD3]=0x81,[0xD4]=0x42,[0xD5]=0x12,[0xD6]=0x21,
    [0xD7]=0xA5,[0xD8]=0x5A,[0xD9]=0x44,[0xDA]=0x11,
};

static void fill_rect(uint8_t *cell, int x0, int y0, int x1, int y1, uint8_t v) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > aw) x1 = aw;
    if (y1 > ah) y1 = ah;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) cell[y * aw + x] = v;
}

// Render CP437 0xB0-0xDF procedurally; return 1 if handled, 0 to fall through
// to the TrueType glyph.
static int render_special(int code, uint8_t *cell) {
    switch (code) {
        case 0xB0: fill_rect(cell, 0, 0, aw, ah, 64);  return 1;  // light shade
        case 0xB1: fill_rect(cell, 0, 0, aw, ah, 128); return 1;  // medium shade
        case 0xB2: fill_rect(cell, 0, 0, aw, ah, 192); return 1;  // dark shade
        case 0xDB: fill_rect(cell, 0, 0, aw, ah, 255); return 1;  // full block
        case 0xDC: fill_rect(cell, 0, ah / 2, aw, ah, 255); return 1;  // lower half
        case 0xDD: fill_rect(cell, 0, 0, aw / 2, ah, 255); return 1;   // left half
        case 0xDE: fill_rect(cell, aw / 2, 0, aw, ah, 255); return 1;  // right half
        case 0xDF: fill_rect(cell, 0, 0, aw, ah / 2, 255); return 1;   // upper half
        default: break;
    }
    uint8_t a = box_arms[code & 0xFF];
    if (!a) return 0;

    int U = (a >> 6) & 3, D = (a >> 4) & 3, L = (a >> 2) & 3, R = a & 3;
    int cx = aw / 2, cy = ah / 2;
    int tv = aw / 6;  if (tv < 2) tv = 2;   // vertical-stroke width
    int th = ah / 12; if (th < 2) th = 2;   // horizontal-stroke height
    int ov = tv, oh = th;                   // parallel offset for double lines

    // Vertical arms (up/down). Extend past centre by a stroke so junctions fill.
    if (U || D) {
        int y0 = U ? 0 : (cy - th);
        int y1 = D ? ah : (cy + th);
        int dbl = (U == 2 || D == 2);
        int cols2[2] = { dbl ? cx - ov : cx, cx + ov };
        int n = dbl ? 2 : 1;
        for (int k = 0; k < n; ++k)
            fill_rect(cell, cols2[k] - tv / 2, y0, cols2[k] - tv / 2 + tv, y1, 255);
    }
    // Horizontal arms (left/right).
    if (L || R) {
        int x0 = L ? 0 : (cx - tv);
        int x1 = R ? aw : (cx + tv);
        int dbl = (L == 2 || R == 2);
        int rows2[2] = { dbl ? cy - oh : cy, cy + oh };
        int n = dbl ? 2 : 1;
        for (int k = 0; k < n; ++k)
            fill_rect(cell, x0, rows2[k] - th / 2, x1, rows2[k] - th / 2 + th, 255);
    }
    return 1;
}

// ---- font discovery --------------------------------------------------------
static const char *find_font(void) {
    static const char *cands[] = {
        g_settings.font_path,  // runtime override (settings file), "" if unset
        FONT_TTF_PATH,         // compile-time override (config.h), "" if unset
        ASSET_FONT_PATH,       // bundled DejaVuSansMono.ttf (compiled-in source path)
        "/usr/local/share/vt100-pi/font.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    };
    for (unsigned i = 0; i < sizeof cands / sizeof cands[0]; ++i) {
        if (!cands[i] || !cands[i][0]) continue;
        FILE *f = fopen(cands[i], "rb");
        if (f) { fclose(f); return cands[i]; }
    }
    return NULL;
}

// ---- atlas build -----------------------------------------------------------
void glyphs_init(int cell_w, int cell_h) {
    aw = cell_w > 0 ? cell_w : 1;
    ah = cell_h > 0 ? cell_h : 1;
    atlas = calloc((size_t)GLYPH_COUNT * aw * ah, 1);
    if (!atlas) { fprintf(stderr, "glyphs: out of memory for atlas\n"); exit(1); }

    build_cp437();

    const char *path = find_font();
    if (!path) {
        fprintf(stderr, "glyphs: no usable TTF found (set FONT_TTF_PATH in config.h,\n"
                        "        install fonts-dejavu-core, or bundle a font)\n");
        exit(1);
    }

    FT_Library lib;
    if (FT_Init_FreeType(&lib)) { fprintf(stderr, "glyphs: FT_Init_FreeType failed\n"); exit(1); }
    FT_Face face;
    if (FT_New_Face(lib, path, 0, &face)) {
        fprintf(stderr, "glyphs: cannot load font %s\n", path);
        exit(1);
    }
    fprintf(stderr, "glyphs: font %s, atlas %dx%d\n", path, aw, ah);

    // Fill the cell height with the font, and pick a pixel *width* so the glyph
    // advance fills the cell too. Setting width = aw directly makes glyphs ~40%
    // too narrow, because a monospace advance is only ~0.6 em -- so measure the
    // natural advance at a square size and scale the em width up to match aw.
    FT_Set_Pixel_Sizes(face, (FT_UInt)ah, (FT_UInt)ah);
    int adv_nat = ah;
    FT_UInt ref = FT_Get_Char_Index(face, 'M');
    if (ref && FT_Load_Glyph(face, ref, FT_LOAD_DEFAULT) == 0)
        adv_nat = (int)(face->glyph->advance.x >> 6);
    int em_w = (adv_nat > 0) ? (ah * aw / adv_nat) : aw;
    if (em_w < 1) em_w = 1;
    FT_Set_Pixel_Sizes(face, (FT_UInt)em_w, (FT_UInt)ah);

    int baseline = (int)(face->size->metrics.ascender >> 6);
    int line_h   = (int)(face->size->metrics.height >> 6);
    int y_base   = baseline + (ah - line_h) / 2;   // baseline, cell-centred

    for (int code = 0; code < GLYPH_COUNT; ++code) {
        uint8_t *cell = atlas + (size_t)code * aw * ah;

        // Block/shade/box-drawing region: draw procedurally so it tiles.
        if (render_special(code, cell)) continue;

        FT_UInt gi = FT_Get_Char_Index(face, cp437[code]);
        if (gi == 0) continue;                        // no glyph -> leave blank
        if (FT_Load_Glyph(face, gi, FT_LOAD_RENDER)) continue;

        FT_GlyphSlot g = face->glyph;
        FT_Bitmap *bm = &g->bitmap;

        int adv = (int)(g->advance.x >> 6);
        int ox = (aw - adv) / 2 + g->bitmap_left;     // centre by advance width
        int oy = y_base - g->bitmap_top;

        for (unsigned by = 0; by < bm->rows; ++by) {
            int py = oy + (int)by;
            if (py < 0 || py >= ah) continue;
            const unsigned char *srow = bm->buffer + (int)by * bm->pitch;
            for (unsigned bx = 0; bx < bm->width; ++bx) {
                int px = ox + (int)bx;
                if (px < 0 || px >= aw) continue;
                cell[py * aw + px] = srow[bx];
            }
        }
    }

    FT_Done_Face(face);
    FT_Done_FreeType(lib);
}

const uint8_t *glyph_alpha(uint8_t code) { return atlas + (size_t)code * aw * ah; }
int glyph_atlas_w(void) { return aw; }
int glyph_atlas_h(void) { return ah; }
