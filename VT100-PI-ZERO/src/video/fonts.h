// Registry of selectable fonts for the Setup menu. Index 0 is the built-in
// default; the rest are the bundled TTFs (assets/). The settings "font" value
// is either "" (default), a registry name, or an absolute .ttf path.
#ifndef FONTS_H
#define FONTS_H

#include <stddef.h>

int         fonts_count(void);              // selectable entries, including default at 0
const char *fonts_name(int i);              // display label for index i
const char *fonts_value(int i);             // settings value for index i ("" = default)
int         fonts_index_of(const char *v);  // index whose value == v, or -1 (custom path)

// Resolve a settings value ("", a name, or an absolute path) to an existing
// .ttf path, or NULL if none found. Returned pointer is to a static buffer.
const char *fonts_resolve(const char *value);

#endif // FONTS_H
