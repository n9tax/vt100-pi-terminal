#include "settings.h"
#include "config.h"
#include "video/textmode.h"
#include "io/serial.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

#include <string.h>
#include <stddef.h>

settings_t settings;

// Persist to the LAST flash sector. It sits far past the program image, so a
// correct write here can never touch code/bootloader (worst case is a BOOTSEL
// re-flash, not a brick). Guarded by a magic + version + checksum on load.
#define SETTINGS_MAGIC      0x30315456u        // 'VT10'
#define SETTINGS_VERSION    1u
#define SETTINGS_FLASH_OFF  (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct {
    uint32_t   magic;
    uint32_t   version;
    settings_t s;
    uint32_t   sum;
} persist_t;
_Static_assert(sizeof(persist_t) <= FLASH_SECTOR_SIZE, "settings image too big for one flash sector");

// Flash programs in whole pages; round the image up to a page multiple.
#define PERSIST_LEN (((sizeof(persist_t) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE)

static uint32_t calc_sum(const persist_t *p) {
    const uint8_t *b = (const uint8_t *)p;
    uint32_t s = 0x12345678u;
    for (size_t i = 0; i < offsetof(persist_t, sum); ++i) s = s * 31u + b[i];
    return s;
}

static void settings_defaults(void) {
    memset(&settings, 0, sizeof settings);
    settings.theme        = THEME_DEFAULT;
    settings.cursor_style = 0;
    settings.cursor_blink = 0;
    settings.local_echo   = 0;
    settings.bell_visual  = 1;
    settings.scroll_smooth = 0;   // jump scroll by default (unchanged behaviour)
    settings.scroll_speed  = 1;   // medium
    settings.baud         = HOST_BAUD;
    strncpy(settings.wifi_ssid,   WIFI_SSID,   sizeof settings.wifi_ssid   - 1);
    strncpy(settings.wifi_pass,   WIFI_PASS,   sizeof settings.wifi_pass   - 1);
    strncpy(settings.telnet_host, TELNET_HOST, sizeof settings.telnet_host - 1);
    settings.telnet_port  = TELNET_PORT;
    // Seed the first speed-dial slot from the compile-time default host, if one
    // was configured (left blank for a public build -> all slots start empty).
    if (TELNET_HOST[0]) {
        strncpy(settings.bookmarks[0].name, "Default",   sizeof settings.bookmarks[0].name - 1);
        strncpy(settings.bookmarks[0].host, TELNET_HOST, sizeof settings.bookmarks[0].host - 1);
        settings.bookmarks[0].port = TELNET_PORT;
    }
}

void settings_load(void) {
    const persist_t *fp = (const persist_t *)(XIP_BASE + SETTINGS_FLASH_OFF);
    if (fp->magic == SETTINGS_MAGIC && fp->version == SETTINGS_VERSION &&
        fp->sum == calc_sum(fp)) {
        settings = fp->s;
        // Belt-and-braces: force the string fields NUL-terminated.
        settings.wifi_ssid[sizeof settings.wifi_ssid - 1]     = 0;
        settings.wifi_pass[sizeof settings.wifi_pass - 1]     = 0;
        settings.telnet_host[sizeof settings.telnet_host - 1] = 0;
    } else {
        settings_defaults();
    }
}

void settings_save(void) {
    persist_t img;
    memset(&img, 0, sizeof img);
    img.magic   = SETTINGS_MAGIC;
    img.version = SETTINGS_VERSION;
    img.s       = settings;
    img.sum     = calc_sum(&img);

    // Skip the write if flash already holds exactly this: saves flash wear and
    // avoids the brief video/USB pause on every Setup close when nothing changed.
    const persist_t *fp = (const persist_t *)(XIP_BASE + SETTINGS_FLASH_OFF);
    if (fp->magic == img.magic && fp->version == img.version && fp->sum == img.sum)
        return;

    static uint8_t page[PERSIST_LEN];
    memset(page, 0xff, sizeof page);
    memcpy(page, &img, sizeof img);

    // Park core1 (it executes USB-host code from flash) before touching flash. If
    // it can't be parked in time, ABORT the save rather than risk a hang/brick.
    if (!multicore_lockout_start_timeout_us(2000000)) return;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(SETTINGS_FLASH_OFF, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_FLASH_OFF, page, PERSIST_LEN);
    restore_interrupts(ints);
    multicore_lockout_end_blocking();
}

// Smooth-scroll speed (pixels/second). pps/60 gives the per-frame step: Slow = 1
// px/frame (glass-smooth, ~3 lines/s), Fast = 2 px/frame (smooth, ~6 lines/s).
// Both are exact divisors of the CELL_H (20 px) line height, so every line
// animates in equal frames. (The pacing speeds the slide up to catch up when the
// host out-runs it, so a dedicated faster preset isn't needed.)
static const int scroll_pps[2] = { 60, 120 };
int settings_scroll_pps(void) { return scroll_pps[settings.scroll_speed % 2]; }

void settings_apply_all(void) {
    textmode_set_theme(settings.theme);        // rebuild palette
    textmode_set_cursor_style(settings.cursor_style);
    textmode_set_smooth(settings.scroll_smooth, settings_scroll_pps());
    serial_set_baud(settings.baud);
    textmode_render_all();                      // single re-render with new palette
}
