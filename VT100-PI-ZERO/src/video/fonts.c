// See fonts.h. Bundled fonts live in assets/ (compiled-in ASSET_FONT_DIR), and
// are also looked up under an install dir and the usual system font dirs so a
// deployed binary still finds them.
#include "video/fonts.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#ifndef ASSET_FONT_DIR
#define ASSET_FONT_DIR ""   // set by CMake to the repo's assets/ directory
#endif

typedef struct { const char *name; const char *file; } entry;

// Index 0 is DejaVu, the default (empty settings value). EXTRA are the other
// selectable bundled fonts, in cycle order after the default.
#define DEFAULT_NAME "DejaVu Sans Mono"
#define DEFAULT_FILE "DejaVuSansMono.ttf"

static const entry EXTRA[] = {
    { "Liberation Mono", "LiberationMono-Regular.ttf" },
    { "Noto Sans Mono",  "NotoSansMono-Regular.ttf" },
    { "Hack",            "Hack-Regular.ttf" },
    { "JetBrains Mono",  "JetBrainsMono-Regular.ttf" },
    { "Fira Code",       "FiraCode-Regular.ttf" },
    { "Source Code Pro", "SourceCodePro-Regular.ttf" },
    { "VT323",           "VT323-Regular.ttf" },
};
#define NEXTRA ((int)(sizeof EXTRA / sizeof EXTRA[0]))

int fonts_count(void) { return 1 + NEXTRA; }

const char *fonts_name(int i) {
    if (i <= 0) return DEFAULT_NAME " (default)";
    if (i - 1 < NEXTRA) return EXTRA[i - 1].name;
    return "?";
}

const char *fonts_value(int i) {
    if (i <= 0 || i - 1 >= NEXTRA) return "";   // default = empty value
    return EXTRA[i - 1].name;
}

int fonts_index_of(const char *v) {
    if (!v || !v[0] || !strcasecmp(v, DEFAULT_NAME)) return 0;
    for (int i = 0; i < NEXTRA; ++i)
        if (!strcasecmp(v, EXTRA[i].name)) return i + 1;
    return -1;   // an absolute path / unknown name
}

// Search the candidate directories for `file`, returning the first that exists.
static const char *find_in_dirs(const char *file, char *out, size_t n) {
    static const char *dirs[] = {
        ASSET_FONT_DIR,
        "/usr/local/share/vt100-pi/fonts",
        "/usr/share/fonts/TTF",
        "/usr/share/fonts/truetype/dejavu",
        "/usr/share/fonts/liberation",
        "/usr/share/fonts/truetype/liberation",
        "/usr/share/fonts/noto",
        "/usr/share/fonts/truetype/noto",
    };
    for (unsigned i = 0; i < sizeof dirs / sizeof dirs[0]; ++i) {
        if (!dirs[i][0]) continue;
        snprintf(out, n, "%s/%s", dirs[i], file);
        FILE *f = fopen(out, "rb");
        if (f) { fclose(f); return out; }
    }
    return NULL;
}

const char *fonts_resolve(const char *value) {
    static char buf[512];

    if (value && value[0] == '/') {              // explicit absolute path
        FILE *f = fopen(value, "rb");
        if (f) { fclose(f); snprintf(buf, sizeof buf, "%s", value); return buf; }
        return NULL;
    }

    const char *file = DEFAULT_FILE;             // "" or the default name -> DejaVu
    if (value && value[0] && strcasecmp(value, DEFAULT_NAME) != 0)
        for (int i = 0; i < NEXTRA; ++i)
            if (!strcasecmp(value, EXTRA[i].name)) { file = EXTRA[i].file; break; }

    return find_in_dirs(file, buf, sizeof buf);
}
