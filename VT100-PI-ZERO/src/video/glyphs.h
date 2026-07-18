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

// 8-bit coverage bitmap (row-major, glyph_atlas_w * glyph_atlas_h bytes) for a
// CP437 code point. 0 = background, 255 = full foreground.
const uint8_t *glyph_alpha(uint8_t code);
int glyph_atlas_w(void);
int glyph_atlas_h(void);

#endif // GLYPHS_H
