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

#endif // VT100_CONFIG_H
