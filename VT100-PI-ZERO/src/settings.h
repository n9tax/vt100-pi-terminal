// Runtime settings, loaded from ~/.config/vt100-pi/vt100.conf (created with
// documented defaults on first run). Compile-time values in config.h are the
// defaults; the file overrides them. This is the data model the future F3
// Setup menu will edit and persist.
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

// Serial frame formats. With 7 data bits the kernel strips the eighth (parity)
// bit, the way a VT100 ignores it.
#define SERIAL_FMT_8N1   0
#define SERIAL_FMT_7E1   1
#define SERIAL_FMT_7O1   2
#define SERIAL_FMT_7M1   3   // mark parity: the eighth bit always 1
#define SERIAL_FMT_7S1   4   // space parity: always 0
#define SERIAL_FMT_COUNT 5

typedef struct {
    char serial_dev[128];   // e.g. /dev/serial0
    int  baud;              // 300..115200
    int  serial_format;     // SERIAL_FMT_*
    int  xon_xoff;          // 1 = send XOFF/XON to throttle the host
    int  theme;             // themes.c index
    int  cursor_style;      // 0 = block, 1 = underline
    int  local_echo;        // 0/1
    char font_path[256];    // "" = bundled/system font, else absolute .ttf path
    char fg_hex[8];         // custom-theme foreground "#RRGGBB"
    char bg_hex[8];         // custom-theme background "#RRGGBB"
    int  smooth_scroll;     // 0/1
    int  scroll_speed;      // pixels/second
    int  scroll_depth;      // lines of slide allowed in flight (pacing depth)
    int  scroll_dump;       // backlog in lines past which we dump-and-reset
    int  auto_speed;        // 1 = PID-trim the scroll speed to match the host
    int  speed_avg_ms;      // averaging window for the auto-speed measurement
    char telnet_host[128];  // "" = use serial; else connect Telnet on boot
    int  telnet_port;       // default 23
    char ssh_host[128];     // "" = none; else "host"/"user@host", connect ssh on boot
    int  ssh_port;          // default 22 (22 = leave it to ssh / ~/.ssh/config)
    int  local_shell;       // 1 = connect a local Pi shell (highest priority)
    int  auto_reconnect;    // 1 = re-dial a dropped ssh/telnet/shell link
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

// "8N1", "7E1", ... for a SERIAL_FMT_* value, and back (-1 if unknown).
const char *settings_serial_format_name(int fmt);
int settings_serial_format_parse(const char *s);

// Name of a theme index ("amber", "c64", ...); "amber" if out of range.
const char *settings_theme_name(int theme);

// Parse "#RRGGBB" or "RRGGBB" to 0xRRGGBB (0 on malformed input).
uint32_t settings_parse_hex(const char *s);

#endif // SETTINGS_H
