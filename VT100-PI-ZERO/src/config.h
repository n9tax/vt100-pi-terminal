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
// Frame format (SERIAL_FMT_* in settings.h): 8N1, or 7 data bits with even,
// odd, mark or space parity -- the VT100 Setup's "bits per char" and parity.
#define SERIAL_FORMAT_DEFAULT 0   // 8N1
// Send XOFF when the input ring fills and XON once it drains, as the VT100
// does. Only ever fires on a deep backlog (or with Setup open on a busy host).
#define XON_XOFF_DEFAULT      1

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

// New-line mode (LNM) at power-on, the VT100 SET-UP's NEWLINE: received LF
// also returns the carriage, and Return sends CR LF. On for text with bare-LF
// line ends (Unix files, many .vt animations); hosts can still set/reset it.
#define NEWLINE_MODE_DEFAULT 0

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
// Scroll-speed range and step the Setup menu dials through (px/s). A flat fine
// step: the keyboard's auto-repeat is passed through to the menu, so holding the
// arrow crosses the range while a tap trims by one increment.
#define SCROLL_SPEED_MIN   20
#define SCROLL_SPEED_MAX 1500
#define SCROLL_SPEED_STEP   5

// ---- Auto speed (closed-loop pacing) ---------------------------------------
// A PID controller that trims the scroll speed to hold the backlog at a small
// setpoint -- in effect matching the scroll rate to the host's output rate, so
// a burst speeds up and quiet output slows down without the screen ever running
// away or starving. Off by default: it is a different feel, not a fix.
#define AUTO_SPEED_DEFAULT 0

// Setpoint, in buffered lines. Small: this is how far behind the host the screen
// deliberately sits in order to keep the slide fed.
#define AUTO_SPEED_TARGET_LINES 4

// Gains. Error is in lines, output in px/s.
// There is no integral term and no derivative term, and that is deliberate.
// The plant is already an integrator -- a speed error accumulates into backlog --
// so an integral controller on top makes a double integrator, which hunts. A
// derivative on a small, bursty, quantised line count is mostly sampling noise.
// The rate feedforward does the work; KP only corrects slow drift, and is capped
// so that a saturated error (backlog pinned at 0 between bursts) cannot drive
// the speed on its own.
// Guarded so a test harness can sweep them without editing this file.
#ifndef AUTO_SPEED_KP
#define AUTO_SPEED_KP   4      // px/s per line of backlog error
#endif
#ifndef AUTO_SPEED_TRIM_MAX
#define AUTO_SPEED_TRIM_MAX 120   // px/s: the trim corrects drift, it does not drive
#endif

// Averaging window for the measured backlog, in ms -- the time constant of a
// first-order lag over it. The backlog is a small, bursty integer: real output
// arrives in clumps, so sampling it raw makes the P term jump and the scroll
// visibly swing (a few lines of wobble times KP is a large fraction of the
// speed). Averaging is what makes the controller look like a steady hand rather
// than a twitchy one. Longer = smoother but slower to react.
#define AUTO_SPEED_AVG_DEFAULT 4000
#define AUTO_SPEED_AVG_MIN      250
#define AUTO_SPEED_AVG_MAX     8000

// Control period and slew limit. Speed changes must be gradual or the scroll
// visibly surges; this caps the ramp at AUTO_SPEED_SLEW px/s per update.
// Deadband, as a percentage of the current speed. A real VT100 scrolls at one
// fixed rate; a speed that continuously tracks every fluctuation in the host
// reads as restless, however accurate it is. So hold the current speed until the
// measured requirement differs by more than this, then move to it and hold
// again. The result is long stretches of genuinely constant speed with
// occasional steps, which is what looks right.
#ifndef AUTO_SPEED_DEADBAND_PCT
#define AUTO_SPEED_DEADBAND_PCT 20
#endif

#define AUTO_SPEED_PERIOD_MS 250
#ifndef AUTO_SPEED_SLEW
#define AUTO_SPEED_SLEW       15
#endif

// How long output must be quiet before the controller eases back to the
// configured base speed.
#define AUTO_SPEED_IDLE_MS 2000

// How far below the configured speed the controller may go, as a divisor of it.
// Rate-matching a slow host all the way down would drive the speed to the
// minimum and make every line crawl -- fine for watching a log, awful at a shell
// prompt, where a line should appear promptly however slowly you type. So the
// controller may ease off for gentle output, but only to base/N; it is free to
// go all the way up, because falling behind is the failure that actually shows.
#define AUTO_SPEED_FLOOR_DIV 2

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
#define SSH_PORT_DEFAULT    22
#define TELNET_HOST_DEFAULT ""
#define TELNET_PORT_DEFAULT 23

// Connect to a local shell on the Pi (use the Pi OS directly) instead of a
// serial/network host. Highest priority when on.
#define LOCAL_SHELL_DEFAULT 0
// Who the local shell logs in as ("" = the git clone's owner from the updater's
// config, else the first normal user). It is never root unless no other
// account exists: use sudo for root, as on any other login.
#define LOCAL_SHELL_USER    ""
#define UPDATE_CONF_PATH    "/etc/vt100-pi/update.conf"

// ---- Web view ---------------------------------------------------------------
// Mirror the screen to a web page on the Pi (for OBS or a projector): open
// http://<this Pi>:<port>/ in any browser. Off by default -- the page shows
// whatever is on screen to anyone on the network, with no login.
#define WEB_VIEW_DEFAULT  0
#define WEB_PORT_DEFAULT  8080
#define WEB_MAX_CLIENTS   6         // simultaneous browsers (OBS + a few viewers)

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
