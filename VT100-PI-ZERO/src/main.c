// VT100-PI-ZERO — hardware VT100 terminal on a Raspberry Pi Zero 2 W.
//   Video : HDMI/DVI monitor, DRM/KMS, centred TERM_COLS x TERM_ROWS grid
//   Serial: RS232 hat on the GPIO UART (/dev/serial0) — host link
//   Input : USB keyboard via evdev
//
// This is the MVP wiring: serial host link only, no network/SSH transport
// and no on-screen Setup menu yet (see ../README.md for the build order).
#include <stdio.h>
#include <stdint.h>
#include <poll.h>
#include <time.h>

#include "config.h"
#include "settings.h"
#include "setup.h"
#include "video/textmode.h"
#include "io/serial_linux.h"
#include "io/kbd_evdev.h"
#include "io/host_link.h"
#include "terminal/screen.h"
#include "terminal/vt100.h"

// Power-on self-test splash: "VT100 OK" in a box, centred. Cleared by RETURN
// into a blank terminal (see the main loop's boot phase).
static void splash_draw(void) {
    for (int r = 0; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c)
            tm_cells[r][c] = (cell_t){ ' ', 7, 0, 0 };

    const char *msg = "VT100 OK";
    const int w = 12, r0 = TERM_ROWS / 2 - 1, c0 = (TERM_COLS - w) / 2;
    tm_cells[r0][c0]             = (cell_t){ 0xDA, 7, 0, 0 };   // top-left corner
    tm_cells[r0][c0 + w - 1]     = (cell_t){ 0xBF, 7, 0, 0 };   // top-right corner
    tm_cells[r0 + 2][c0]         = (cell_t){ 0xC0, 7, 0, 0 };   // bottom-left
    tm_cells[r0 + 2][c0 + w - 1] = (cell_t){ 0xD9, 7, 0, 0 };   // bottom-right
    for (int i = 1; i < w - 1; ++i) {
        tm_cells[r0][c0 + i]     = (cell_t){ 0xC4, 7, 0, 0 };   // top rule
        tm_cells[r0 + 2][c0 + i] = (cell_t){ 0xC4, 7, 0, 0 };   // bottom rule
    }
    tm_cells[r0 + 1][c0]         = (cell_t){ 0xB3, 7, 0, 0 };   // side bars
    tm_cells[r0 + 1][c0 + w - 1] = (cell_t){ 0xB3, 7, 0, 0 };
    for (int i = 0; msg[i]; ++i)
        tm_cells[r0 + 1][c0 + 2 + i] = (cell_t){ (uint8_t)msg[i], 7, 0, 0 };

    textmode_render_all();
}

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(void) {
    settings_load();                        // ~/.config/vt100-pi/vt100.conf -> g_settings

    textmode_init();                        // reads g_settings.font_path via glyphs.c
    textmode_set_custom_colors(settings_parse_hex(g_settings.fg_hex),
                               settings_parse_hex(g_settings.bg_hex));
    textmode_set_theme(g_settings.theme);
    textmode_set_cursor_style(g_settings.cursor_style);
    textmode_set_smooth(g_settings.smooth_scroll, g_settings.scroll_speed);
    serial_init(g_settings.serial_dev, g_settings.baud);
    kbd_init();
    screen_init();
    vt100_init();
    host_link_set_write_fn(serial_write);   // MVP: serial is the only transport

    splash_draw();
    int booting = 1;

    long long next_blink = now_ms() + 500;
    long long flash_until = -1;   // -1 = no visual-bell flash pending
    int blink = 0;

    struct pollfd pfds[2];
    pfds[0].fd = serial_fd(); pfds[0].events = POLLIN;
    pfds[1].fd = kbd_fd();    pfds[1].events = POLLIN;

    while (1) {
        pfds[0].fd = serial_fd();   // may change if serial was reopened in Setup

        // ~60Hz while a smooth scroll is animating, else a lazy 50ms tick.
        poll(pfds, 2, textmode_scroll_busy() ? 15 : 50);

        if (pfds[1].revents & POLLIN) kbd_poll();

        if (!booting && kbd_take_setup_toggle()) setup_toggle();

        if (booting) {
            // Boot self-test splash: hold until RETURN, then open a clean terminal.
            int k;
            while ((k = kbd_getc()) >= 0) {
                if (k == '\r' || k == '\n') {
                    booting = 0;
                    screen_init();   // clear the splash to a blank screen + cursor
                    break;
                }
            }
        } else if (setup_active()) {
            // Setup owns the screen: route keys to the menu, pause the host link.
            int k;
            while ((k = kbd_getc()) >= 0) setup_feed((uint8_t)k);
        } else {
            int activity = 0;

            // Host -> screen.
            int c;
            while ((c = serial_getc()) >= 0) {
                if (!activity) { screen_hide_cursor(); activity = 1; }
                vt100_feed((uint8_t)c);
            }

            // Keyboard -> host, + optional local echo.
            uint8_t kb[64]; int kn = 0, k;
            while ((k = kbd_getc()) >= 0) {
                if (kn < (int)sizeof kb) kb[kn++] = (uint8_t)k;
                if (g_settings.local_echo) {
                    if (!activity) { screen_hide_cursor(); activity = 1; }
                    vt100_feed((uint8_t)k);
                    if (k == '\r') vt100_feed('\n');
                }
            }
            if (kn) serial_write(kb, (uint32_t)kn);
            if (activity) screen_show_cursor();

            // Visual bell (no audible-bell hardware on this board).
            if (vt100_take_bell()) {
                textmode_set_flash(1);
                flash_until = now_ms() + 60;
            }
        }

        if (setup_active()) continue;   // don't blink/flash over the menu

        textmode_scroll_tick();                  // advance any in-flight smooth scroll
        if (textmode_scroll_busy()) continue;    // hold off blink/bell mid-slide

        long long t = now_ms();
        if (flash_until >= 0 && t >= flash_until) {
            textmode_set_flash(0);
            flash_until = -1;
        }
        if (t >= next_blink) {
            blink ^= 1;
            textmode_set_blink(blink);
            next_blink = t + 500;
        }
    }
}
