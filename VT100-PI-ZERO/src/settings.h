// Runtime settings, loaded from ~/.config/vt100-pi/vt100.conf (created with
// documented defaults on first run). Compile-time values in config.h are the
// defaults; the file overrides them. This is the data model the future F3
// Setup menu will edit and persist.
#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct {
    char serial_dev[128];   // e.g. /dev/serial0
    int  baud;              // 300..115200
    int  theme;             // THEME_* (config.h)
    int  cursor_style;      // 0 = block, 1 = underline
    int  local_echo;        // 0/1
    char font_path[256];    // "" = bundled/system font, else absolute .ttf path
} settings_t;

extern settings_t g_settings;

// Populate g_settings from config.h defaults, then overlay the config file
// (creating it with a documented template if absent). Safe to call once at
// startup, before textmode_init()/serial_init().
void settings_load(void);

// Absolute path of the active config file (for logging / the Setup menu).
const char *settings_path(void);

// Write g_settings back to the config file (used by the Setup menu on save).
void settings_save(void);

// Name of a THEME_* value ("amber", "green", ...); "amber" if out of range.
const char *settings_theme_name(int theme);

#endif // SETTINGS_H
