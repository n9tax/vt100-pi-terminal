// See themes.h. All colour math lives here so it can be unit-tested without the
// DRM framebuffer.
#include "video/themes.h"

typedef struct {
    const char *name;
    uint32_t    fg, bg;   // used when !color
    int         color;    // 1 = full ANSI palette (fg/bg ignored)
    int         custom;   // 1 = fg/bg come from runtime custom values
    int         dim;      // 1 = phosphor look (normal text ~70%); 0 = full intensity
} theme_t;

static const theme_t THEMES[] = {
    { "color",   0x000000, 0x000000, 1, 0, 0 },
    { "amber",   0xFFB000, 0x000000, 0, 0, 1 },
    { "green",   0x33FF33, 0x000000, 0, 0, 1 },
    { "white",   0xFFFFFF, 0x000000, 0, 0, 1 },
    { "blue",    0x66AAFF, 0x000000, 0, 0, 1 },
    { "red",     0xFF3333, 0x000000, 0, 0, 1 },
    { "yellow",  0xFFEE33, 0x000000, 0, 0, 1 },
    { "c64",     0xC0B6FF, 0x352879, 0, 0, 0 },   // light blue on C64 blue
    { "vic20",   0x2B2FB0, 0xFFFFFF, 0, 0, 0 },   // blue text on white screen
    { "c128",    0x8BE870, 0x000000, 0, 0, 0 },   // light green on black (BASIC 7.0)
    { "borland", 0xFFFF54, 0x0000A8, 0, 0, 0 },   // yellow on blue (Turbo IDE)
    { "custom",  0xFFFFFF, 0x000000, 0, 1, 0 },   // fg/bg from settings hex
};
#define NTHEMES ((int)(sizeof THEMES / sizeof THEMES[0]))

// Full ANSI palette for the "color" theme.
static const uint32_t ansi_palette[16] = {
    0x000000, 0xaa0000, 0x00aa00, 0xaa5500, 0x0000aa, 0xaa00aa, 0x00aaaa, 0xaaaaaa,
    0x555555, 0xff5555, 0x55ff55, 0xffff55, 0x5555ff, 0xff55ff, 0x55ffff, 0xffffff,
};

// Brightness of each ANSI index (0 = background .. 100 = foreground). The dim
// curve is the phosphor look (normal text ~70%, bold full); the full curve
// renders normal text at the exact foreground (so retro/custom colours and a
// hand-picked hex fg show as-is).
static const int bright_dim[16]  = { 0,55,55,55,55,55,55,70,  40,100,100,100,100,100,100,100 };
static const int bright_full[16] = { 0,60,60,60,60,60,60,100, 50,100,100,100,100,100,100,100 };

int themes_count(void) { return NTHEMES; }

const char *themes_name(int i) {
    return (i >= 0 && i < NTHEMES) ? THEMES[i].name : "amber";
}

int themes_index_of(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < NTHEMES; ++i) {
        const char *a = THEMES[i].name, *b = name;
        int eq = 1;
        while (*a && *b) {                       // case-insensitive compare
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) { eq = 0; break; }
            ++a; ++b;
        }
        if (eq && !*a && !*b) return i;
    }
    return -1;
}

int themes_is_custom(int i) { return (i >= 0 && i < NTHEMES) && THEMES[i].custom; }

static uint32_t lerp(uint32_t bg, uint32_t fg, int pct) {
    int r = ((bg >> 16) & 0xff) + (((int)((fg >> 16) & 0xff) - (int)((bg >> 16) & 0xff)) * pct) / 100;
    int g = ((bg >>  8) & 0xff) + (((int)((fg >>  8) & 0xff) - (int)((bg >>  8) & 0xff)) * pct) / 100;
    int b = ( bg        & 0xff) + (((int)( fg        & 0xff) - (int)( bg        & 0xff)) * pct) / 100;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

uint32_t theme_rgb(int i, uint8_t ansi_idx, uint32_t custom_fg, uint32_t custom_bg) {
    if (i < 0 || i >= NTHEMES) i = themes_index_of("amber");
    const theme_t *t = &THEMES[i];
    if (t->color) return ansi_palette[ansi_idx & 0xf];
    uint32_t fg = t->custom ? custom_fg : t->fg;
    uint32_t bg = t->custom ? custom_bg : t->bg;
    const int *curve = t->dim ? bright_dim : bright_full;
    return lerp(bg, fg, curve[ansi_idx & 0xf]);
}
