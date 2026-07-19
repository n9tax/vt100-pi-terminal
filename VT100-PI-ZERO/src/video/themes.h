// Colour themes. Each theme is either the full 16-colour ANSI palette ("color")
// or a two-colour scheme (a foreground and a background) that every ANSI index
// maps into by brightness -- the amber/green phosphor look, and the retro
// machine palettes (C64, VIC-20, C128, Borland). One theme, "custom", takes its
// fg/bg from runtime values (settings hex fields).
#ifndef THEMES_H
#define THEMES_H

#include <stdint.h>

int         themes_count(void);
const char *themes_name(int i);            // e.g. "amber", "c64"
int         themes_index_of(const char *name);  // -1 if unknown
int         themes_is_custom(int i);       // 1 for the "custom" theme

// Resolve an ANSI colour index (0..15) to an RGB value for theme `i`. For the
// custom theme, custom_fg/custom_bg supply the two colours; ignored otherwise.
uint32_t    theme_rgb(int i, uint8_t ansi_idx, uint32_t custom_fg, uint32_t custom_bg);

#endif // THEMES_H
