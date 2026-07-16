// VT100-PI — hardware VT100 terminal on Raspberry Pi Pico 2 W (RP2350).
//   Video : PiCowBell HSTX DVI (GP12-19), 800x480@60, 80x24 amber (Setup-selectable)
//   Serial: Waveshare RS232 ch0 (UART0 GP0/1) — host link
//   Input : USB keyboard via PIO-USB (GP6/7), TinyUSB host on core1
//   Setup : Ctrl+F3 opens the on-screen settings menu
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "config.h"
#include "fault.h"
#include "settings.h"
#include "setup.h"
#include "io/sys_clock.h"
#include "video/hstx_dvi.h"
#include "video/textmode.h"
#include "io/serial.h"
#include "io/kbd_host.h"
#include "terminal/screen.h"
#include "terminal/vt100.h"
#if NET_ENABLE
#include "net/net.h"
#endif

static void screen_puts(const char *s) {
    while (*s) screen_putc((uint8_t)*s++);
}

// Power-on self-test splash: "VT100 OK" in a box, centred, VT220/320 style.
// Cleared by RETURN into a blank terminal (see the main loop's boot phase).
static void splash_draw(void) {
    for (int r = 0; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c)
            tm_cells[r][c] = (cell_t){ ' ', 7, 0, 0 };

    const char *msg = "VT100 OK";
    const int w = 12, r0 = 10, c0 = (TERM_COLS - w) / 2;   // box: 12 wide, 3 tall
    tm_cells[r0][c0]         = (cell_t){ 0xDA, 7, 0, 0 };   // top-left  corner
    tm_cells[r0][c0 + w - 1] = (cell_t){ 0xBF, 7, 0, 0 };   // top-right corner
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

int main(void) {
    sys_clock_init_dvi_usb();       // clk_hstx=150 (video), clk_sys=120 (PIO-USB)

    // If the previous boot ended in a hard fault, capture it to display it.
    uint32_t fpc, flr, fcfsr, fhfsr;
    int had_fault = fault_get(&fpc, &flr, &fcfsr, &fhfsr);

    // Reserve the video DMA channels up front so the CYW43 driver (brought up
    // later, on demand) can never allocate them out from under the scanout.
    hstx_dvi_reserve_dma();

    hstx_dvi_init();
    textmode_init();
    serial_init();
    screen_init();
    vt100_init();
    settings_load();
    settings_apply_all();           // theme, cursor, baud
    kbd_host_start();               // core1 USB host

    char line[80];
    splash_draw();                  // "VT100 OK" self-test box; RETURN opens the terminal
    int booting = 1;

    absolute_time_t next_blink = make_timeout_time_ms(500);
    absolute_time_t flash_until = nil_time;
    absolute_time_t next_dvi_check = make_timeout_time_ms(40);
    absolute_time_t last_dvi_time = get_absolute_time();
    uint32_t last_hb = hstx_dvi_heartbeat();
    int blink = 0;

    while (true) {
#if NET_ENABLE
        net_poll();                 // service lwIP/CYW43 (no-op while radio down)
        // Reflect live connection status: into the Setup menu if it is open, else
        // as a one-line note in the terminal (only once the radio is up).
        {
            static const char *prev_net = 0;
            const char *ns = net_status();
            if (ns != prev_net) {
                prev_net = ns;
                if (setup_active()) setup_refresh();
                else if (net_active()) {
                    char s[72]; snprintf(s, sizeof s, "[net: %s]\r\n", ns);
                    screen_hide_cursor(); screen_puts(s); screen_show_cursor();
                }
            }
        }
        // Run any connect/disconnect the menu requested (heavy cyw43 work kept at
        // this shallow stack depth rather than inside the Setup key handler).
        {
            int act = setup_take_net_action();
            if (act == 1) net_connect(settings.wifi_ssid, settings.wifi_pass,
                                      settings.telnet_host, settings.telnet_port);
            else if (act == 2) net_stop();
        }
#endif
      if (booting) {
        // Boot self-test splash: hold until RETURN, then open a clean terminal.
        int k;
        while ((k = kbd_getc()) >= 0) {
            if (k == '\r' || k == '\n') {
                booting = 0;
                screen_init();          // clear the splash to a blank screen + cursor
                if (had_fault) {
                    snprintf(line, sizeof line,
                             "*** prev fault  pc=%08lx lr=%08lx cfsr=%08lx ***\r\n",
                             fpc, flr, fcfsr);
                    screen_puts(line);
                }
                break;
            }
        }
      } else {
        // Ctrl+F3 toggles the Setup screen.
        if (kbd_setup_requested()) {
            textmode_scroll_snap();     // settle any slide so the menu draws aligned
            screen_hide_cursor();
            setup_toggle();
            if (!setup_active()) screen_show_cursor();
        }

        if (setup_active()) {
            // Local mode: keys drive the menu; serial stays buffered in its ring.
            int k;
            while ((k = kbd_getc()) >= 0) setup_feed((uint8_t)k);
        } else {
            int activity = 0;

            // Smooth scroll paces the display: while a line is sliding we hold off
            // feeding more host data, then re-check busy after each byte so exactly
            // one line scrolls at a time. As the input backlog grows we slide
            // faster (still smooth) to catch up; only a near-overflow backlog hard-
            // snaps to a jump, so bytes are never dropped.
            if (textmode_smooth_enabled()) {
                int backlog = serial_rx_level();
#if NET_ENABLE
                backlog += net_rx_level();
#endif
                textmode_set_scroll_pace(backlog);
                if (textmode_scroll_busy() && backlog > 1536) textmode_scroll_snap();
            }
            // Host -> screen: RS232 and/or Telnet both feed the engine.
            int c;
            while (!textmode_scroll_busy() && (c = serial_getc()) >= 0) {
                if (!activity) { screen_hide_cursor(); activity = 1; }
                vt100_feed((uint8_t)c);
            }
#if NET_ENABLE
            while (!textmode_scroll_busy() && (c = net_getc()) >= 0) {
                if (!activity) { screen_hide_cursor(); activity = 1; }
                vt100_feed((uint8_t)c);
            }
#endif
            // Keyboard -> host (Telnet if connected, else RS232) + optional echo.
            uint8_t kb[64]; int kn = 0, k;
            while ((k = kbd_getc()) >= 0) {
                if (kn < (int)sizeof kb) kb[kn++] = (uint8_t)k;
                if (settings.local_echo) {
                    if (!activity) { screen_hide_cursor(); activity = 1; }
                    vt100_feed((uint8_t)k);
                    if (k == '\r') vt100_feed('\n');
                }
            }
            if (kn) {
#if NET_ENABLE
                if (net_connected()) net_send(kb, (uint32_t)kn);
                else                 serial_write(kb, (uint32_t)kn);
#else
                serial_write(kb, (uint32_t)kn);
#endif
            }
            if (activity) screen_show_cursor();

            // Visual bell.
            if (vt100_take_bell() && settings.bell_visual) {
                textmode_set_flash(1);
                flash_until = make_timeout_time_ms(60);
            }
        }
      }

        // Video watchdog. The scanout IRQ fires at a rock-steady 58800/sec when
        // healthy (980 transfers/frame x 60fps), independent of this loop's jitter.
        // A cyw43 DMA glitch (RP2350-E13/E5) can drop/add one HSTX FIFO word and
        // shift the command-stream alignment: the IRQ keeps firing (so a bare
        // "stopped" check never triggers) but the HSTX misreads pixel words as
        // commands, so the rate swings wildly off 58800/sec. Detect that rate
        // deviation (and a hard stall) and re-arm via a clean HSTX reset. The band
        // is wide (0.5x..2x) so normal operation can never false-trigger a blink.
        if (absolute_time_diff_us(get_absolute_time(), next_dvi_check) <= 0) {
            absolute_time_t now = get_absolute_time();
            uint32_t hb = hstx_dvi_heartbeat();
            uint32_t delta = hb - last_hb;
            int64_t el_us = absolute_time_diff_us(last_dvi_time, now);
            uint32_t expected = (uint32_t)((el_us * 588) / 10000);  // 58800/sec
            if (delta < expected / 2 || delta > expected * 2) hstx_dvi_kick();
            last_hb = hstx_dvi_heartbeat();       // re-read (kick paused the IRQ)
            last_dvi_time = get_absolute_time();
            next_dvi_check = make_timeout_time_ms(40);
        }

        if (!is_nil_time(flash_until) &&
            absolute_time_diff_us(get_absolute_time(), flash_until) <= 0) {
            textmode_set_flash(0);
            flash_until = nil_time;
        }
        if (absolute_time_diff_us(get_absolute_time(), next_blink) <= 0) {
            blink ^= 1;
            // Not during a slide: re-rendering blinking cells mid-scroll would
            // draw them at the new origin and misplace them.
            if (!setup_active() && !textmode_scroll_busy()) textmode_set_blink(blink);
            next_blink = make_timeout_time_ms(500);
        }
    }
}
