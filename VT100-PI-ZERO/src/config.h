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
// THEME_COLOR = full 16-colour ANSI. AMBER/GREEN/WHITE/BLUE/RED/YELLOW = monochrome
// phosphor (every colour mapped to shades of that hue by brightness).
#define THEME_COLOR   0
#define THEME_AMBER   1
#define THEME_GREEN   2
#define THEME_WHITE   3
#define THEME_BLUE    4
#define THEME_RED     5
#define THEME_YELLOW  6
#define THEME_COUNT   7
#define THEME_DEFAULT THEME_AMBER

// Echo locally typed keys to the screen as well as sending them to the host.
// A real terminal leaves this OFF (the host echoes).
#define LOCAL_ECHO 0

#endif // VT100_CONFIG_H
