// Persistent user settings, edited via the Setup screen (Ctrl+F3) and stored in
// the last flash sector so they survive power-off.
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

// Telnet "speed dial" entries stored in Setup.
#define NUM_BOOKMARKS 8
typedef struct {
    char     name[16];      // label shown in the list
    char     host[64];      // hostname or IP
    uint16_t port;          // 0 -> treated as 23
} bookmark_t;

typedef struct {
    uint8_t  theme;         // THEME_AMBER/GREEN/WHITE/COLOR
    uint8_t  cursor_style;  // 0 = block, 1 = underline
    uint8_t  cursor_blink;  // 0/1
    uint8_t  local_echo;    // 0/1
    uint8_t  bell_visual;   // 0 = off, 1 = flash the screen
    uint8_t  scroll_smooth; // 0 = jump scroll (default), 1 = smooth
    uint8_t  scroll_speed;  // 0 = slow, 1 = medium, 2 = fast
    uint32_t baud;          // host serial baud rate
    // Network (edited in Setup; telnet is brought up on demand, never on boot).
    char     wifi_ssid[33]; // 32 chars + NUL
    char     wifi_pass[64];
    char     telnet_host[64];
    uint16_t telnet_port;
    bookmark_t bookmarks[NUM_BOOKMARKS];
} settings_t;

extern settings_t settings;

// Load from flash (or defaults if none/invalid). Call once at boot.
void settings_load(void);

// Persist current settings to flash (pauses core1 briefly for the flash write).
void settings_save(void);

// Push the current settings out to the hardware/modules (palette, baud, ...).
void settings_apply_all(void);

// Smooth-scroll speed (pixels/second) for the current scroll_speed setting.
int settings_scroll_pps(void);

#endif // SETTINGS_H
