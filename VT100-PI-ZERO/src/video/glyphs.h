// Glyph atlas: renders the 256 CP437 code points from a TrueType font (via
// FreeType) into per-cell grayscale coverage bitmaps at the display's cell
// size. textmode.c alpha-blends these against fg/bg, so scaled text is smooth
// and resolution-independent instead of a stretched 1-bit bitmap font.
#ifndef GLYPHS_H
#define GLYPHS_H

#include <stdint.h>

// Render the atlas at cell_w x cell_h pixels per glyph. Picks a TTF from
// FONT_TTF_PATH (config.h) if set, else the bundled asset, else common system
// paths. Exits the process if no usable font is found.
void glyphs_init(int cell_w, int cell_h);

// Atlas slots: 0-255 are CP437; GLYPH_DEC_BASE + (c - 0x5F) is the VT100's own
// DEC Special Graphics glyph for c in 0x5F-0x7E (scan lines 1-9, the two-letter
// control pictures, ...), which CP437 has no equivalent for.
#define GLYPH_DEC_BASE  256
#define GLYPH_DEC_COUNT 32

// 8-bit coverage bitmap (row-major, glyph_atlas_w * glyph_atlas_h bytes) for an
// atlas slot. 0 = background, 255 = full foreground.
const uint8_t *glyph_alpha(int code);
int glyph_atlas_w(void);
int glyph_atlas_h(void);

#endif // GLYPHS_H
