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

// ---- serial input ring -----------------------------------------------------
// Decouples bursty serial arrival from the paced feed into the terminal: raw
// bytes land here, then are metered into vt100_feed() so smooth scroll can slide
// cleanly and read the buffered line count as look-ahead for its speed.
#define INBUF_SIZE 65536
#define DUMP_LINES 80           // buffered lines past which we dump-and-reset (overrun)
static uint8_t inbuf[INBUF_SIZE];
static int inbuf_head, inbuf_tail;
static int inbuf_nl;                 // running count of buffered newlines (look-ahead)

static int  inbuf_empty(void) { return inbuf_head == inbuf_tail; }
static int  inbuf_pending_lines(void) { return inbuf_nl; }
static void inbuf_push(uint8_t b) {
    int next = (inbuf_head + 1) % INBUF_SIZE;
    if (next == inbuf_tail) return;   // full: drop (very deep backlog; rare)
    inbuf[inbuf_head] = b;
    inbuf_head = next;
    if (b == '\n') ++inbuf_nl;
}
static int inbuf_pop(void) {
    if (inbuf_empty()) return -1;
    uint8_t b = inbuf[inbuf_tail];
    inbuf_tail = (inbuf_tail + 1) % INBUF_SIZE;
    if (b == '\n') --inbuf_nl;
    return b;
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
    long long last_out = 0;       // last host-output time (for idle cursor)
    int cur_shown = 0;            // is the cursor currently painted?

    struct pollfd pfds[3];
    pfds[0].fd = serial_fd();          pfds[0].events = POLLIN;
    pfds[1].fd = kbd_fd();             pfds[1].events = POLLIN;
    pfds[2].fd = textmode_drm_fd();    pfds[2].events = POLLIN;   // page-flip events

    while (1) {
        pfds[0].fd = serial_fd();   // may change if serial was reopened in Setup

        // Wake on I/O or the vblank flip event; a short timeout keeps a smooth
        // scroll advancing when nothing else is happening.
        poll(pfds, 3, (textmode_scroll_busy() || textmode_flip_pending()) ? 15 : 50);

        if (pfds[2].revents & POLLIN) textmode_handle_flip();   // a page flip completed
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

            // Drain the kernel serial buffer into our ring (fast, no processing).
            int c;
            while ((c = serial_getc()) >= 0) inbuf_push((uint8_t)c);

            // Feed the terminal from the ring. Normally paced so at most ~2 lines
            // slide at once and the ring absorbs the burst; but once more than
            // DUMP_LINES are buffered the scroll can't keep up at a constant speed,
            // so dump the whole ring and let it reset (like a real terminal
            // overrunning) rather than lag seconds behind.
            textmode_set_backlog(inbuf_pending_lines());
            if (inbuf_pending_lines() > DUMP_LINES) {
                while (!inbuf_empty()) {
                    if (!activity) { screen_hide_cursor(); activity = 1; cur_shown = 0; }
                    vt100_feed((uint8_t)inbuf_pop());
                }
            } else {
                while (!inbuf_empty() && textmode_feed_room()) {
                    if (!activity) { screen_hide_cursor(); activity = 1; cur_shown = 0; }
                    vt100_feed((uint8_t)inbuf_pop());
                }
            }
            textmode_set_backlog(inbuf_pending_lines());

            // Keyboard -> host, + optional local echo.
            uint8_t kb[64]; int kn = 0, k;
            while ((k = kbd_getc()) >= 0) {
                if (kn < (int)sizeof kb) kb[kn++] = (uint8_t)k;
                if (g_settings.local_echo) {
                    if (!activity) { screen_hide_cursor(); activity = 1; cur_shown = 0; }
                    vt100_feed((uint8_t)k);
                    if (k == '\r') vt100_feed('\n');
                }
            }
            if (kn) serial_write(kb, (uint32_t)kn);
            if (activity) last_out = now_ms();   // cursor stays hidden until output idles

            // Visual bell (no audible-bell hardware on this board).
            if (vt100_take_bell()) {
                textmode_set_flash(1);
                flash_until = now_ms() + 60;
            }
        }

        // Advance a smooth scroll one step per vblank (gated on the flip so the
        // slide is paced to the display, not the CPU loop).
        if (!booting && !setup_active() && !textmode_flip_pending())
            textmode_scroll_tick();

        // Cursor appears only once output has been idle briefly and never
        // mid-scroll, so it doesn't flicker along the bottom during scrolling.
        if (!booting && !setup_active() && !cur_shown && !textmode_scroll_busy()
            && inbuf_empty() && now_ms() - last_out > 200) {
            screen_show_cursor();
            cur_shown = 1;
        }

        // Blink / bell timers, only when idle (not in Setup, not mid-slide).
        if (!setup_active() && !textmode_scroll_busy()) {
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

        textmode_present();   // publish the shadow to the display (vsync flip) if it changed
    }
}
