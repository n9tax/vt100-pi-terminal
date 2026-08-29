// VT100-PI-ZERO — global configuration.
//
// Host link: a GPIO-UART RS232 hat (onboard RS232 transceiver + DB9) on the
// Pi's built-in UART, presented as /dev/serial0. On the Zero 2 W, prefer the
// PL011 by adding `dtoverlay=disable-bt` to /boot/firmware/config.txt so
// /dev/serial0 -> ttyAMA0 (the mini-UART's baud drifts with the core clock).
// Keyboard: any USB keyboard, read via evdev. Video: whatever HDMI/DVI monitor
// is plugged in — resolution is negotiated at runtime via DRM/KMS, not fixed at
// compile time (see src/video/textmode.c).
//
// Alternative host link (not this build): an SC16IS752 I2C-to-UART hat instead
// exposes its channels as /dev/ttySC0 / /dev/ttySC1 — set SERIAL_DEV to that.
// Note the SC16IS752 is TTL-level (3.3V), so it needs its own RS232 converter.
#ifndef VT100_CONFIG_H
#define VT100_CONFIG_H

// ---- Terminal grid ---------------------------------------------------------
#define TERM_COLS  80
#define TERM_ROWS  24

// ---- Serial (RS232 host link) ---------------------------------------------
#define SERIAL_DEV   "/dev/serial0"
#define SERIAL_BAUD  9600

// ---- Display theme ----------------------------------------------------------
// The full theme list (color, amber/green/... phosphor, c64/vic20/c128/borland
// retro, custom) lives in src/video/themes.c. This is just the default, by name.
#define THEME_DEFAULT_NAME "amber"

// Default custom-theme colours (used when theme = custom), overridable in the
// settings file / Setup menu as #RRGGBB.
#define CUSTOM_FG_DEFAULT "#FFFFFF"
#define CUSTOM_BG_DEFAULT "#000000"

// ---- Font -----------------------------------------------------------------
// TrueType font rendered (anti-aliased) into the glyph atlas. Leave empty to
// use the bundled DejaVuSansMono.ttf (or a system DejaVu); set an absolute path
// to override (e.g. a Glass TTY VT220 TTF for a more authentic DEC look).
#define FONT_TTF_PATH ""

// Echo locally typed keys to the screen as well as sending them to the host.
// A real terminal leaves this OFF (the host echoes).
#define LOCAL_ECHO 0

// ---- Smooth scroll ----------------------------------------------------------
// Slide text up a few pixels per frame instead of jumping a whole line. Speed is
// in pixels/second (a line is ~32px tall); bursts catch up and very fast output
// falls back to jump scrolling.
#define SMOOTH_SCROLL_DEFAULT 1
#define SCROLL_SPEED_DEFAULT  300

// Pacing depth: how many lines of slide may be in flight at once. This is the
// buffer between the host and the glass. Higher lets more of a burst animate
// smoothly, at the cost of the screen trailing further behind the host; lower
// keeps the terminal closer to live but gives the scroll less to work with, so
// bursty output starves the slide (an "underrun"). The renderer caps it at the
// scroll headroom (MAXPEND in textmode.c).
// Scroll-speed range the Setup menu can dial through (px/s). Setup steps in
// 10/25/50 px increments depending on the band.
#define SCROLL_SPEED_MIN  20
#define SCROLL_SPEED_MAX 1500

#define SCROLL_DEPTH_DEFAULT 2
#define SCROLL_DEPTH_MIN     1
#define SCROLL_DEPTH_MAX     8

// Overrun limit: buffered lines past which a constant-speed scroll can no longer
// catch up, so the whole ring is dumped to the screen at once and the animation
// resets -- the way a real terminal overruns -- rather than lagging seconds
// behind the host. Raise it to ride out longer bursts smoothly at the cost of
// more lag; lower it to stay closer to live and jump more often.
#define SCROLL_DUMP_DEFAULT  80
#define SCROLL_DUMP_MIN      10
#define SCROLL_DUMP_MAX     400

// ---- Network host link ------------------------------------------------------
// Optional network destination connected on boot instead of the serial link
// (ssh_host takes precedence over telnet_host; empty both = serial).
// ssh_host is "host" or "user@host"; telnet_host is a hostname/IP.
#define SSH_HOST_DEFAULT    ""
#define TELNET_HOST_DEFAULT ""
#define TELNET_PORT_DEFAULT 23

// Connect to a local shell on the Pi (use the Pi OS directly) instead of a
// serial/network host. Highest priority when on.
#define LOCAL_SHELL_DEFAULT 0

// ---- Reconnect --------------------------------------------------------------
// A network/PTY host link can end at any time (server reboot, ssh timeout, a
// pulled cable, `exit` in the shell). When auto-reconnect is on, the link is
// re-dialled on its own with an exponential backoff; either way Setup (Ctrl+F3)
// has a "Host link" row whose Enter re-dials immediately.
#define AUTO_RECONNECT_DEFAULT 1
#define RECONNECT_DELAY_MIN_MS  1000    // first retry, then doubling
#define RECONNECT_DELAY_MAX_MS 30000    // backoff ceiling
// A link must stay up this long to count as "good"; only then does the next
// drop start over at RECONNECT_DELAY_MIN_MS. Without it a link that dies
// immediately (bad host key, refused login) would retry every second forever.
#define RECONNECT_STABLE_MS    10000

// Keepalives, so a peer that vanishes without closing the connection (unplugged
// cable, crashed server) actually surfaces as a drop instead of hanging forever.
#define TCP_KEEPALIVE_IDLE_S   30       // idle before the first probe
#define TCP_KEEPALIVE_INTVL_S  10       // between probes
#define TCP_KEEPALIVE_CNT       3       // failed probes before the link is dead
#define SSH_ALIVE_INTERVAL_S   15       // ssh -o ServerAliveInterval
#define SSH_ALIVE_COUNT_MAX     3       // ssh -o ServerAliveCountMax

#endif // VT100_CONFIG_H
