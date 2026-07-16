// VT100-PI — global hardware/geometry configuration.
//
// Pin map (verified against board docs — see plan):
//   GP0/GP1   UART0 TX/RX  -> Waveshare RS232 ch0 DB9 (primary host link)
//   GP4/GP5   UART1 TX/RX  -> Waveshare RS232 ch1 DB9 (secondary/AUX)
//   GP6/GP7   PIO-USB D+/D- -> USB keyboard (PiCowBell host pads)
//   GP12-GP19 HSTX TMDS     -> PiCowBell DVI/HDMI out
#ifndef VT100_CONFIG_H
#define VT100_CONFIG_H

// ---- Display theme -------------------------------------------------------
// THEME_COLOR = full 16-colour ANSI. AMBER/GREEN/WHITE = monochrome phosphor
// (every colour mapped to shades of that hue by brightness). These are the
// default; the live value lives in settings.theme (changeable in Setup).
#define THEME_COLOR     0
#define THEME_AMBER     1
#define THEME_GREEN     2
#define THEME_WHITE     3
#define THEME_BLUE      4
#define THEME_RED       5
#define THEME_YELLOW    6
#define THEME_COMMODORE 7   // C64 light-blue text on blue screen, light-blue border
#define THEME_BORLAND   8   // Borland/Turbo IDE: bright yellow on DOS blue
#define THEME_COUNT     9
#define THEME_DEFAULT   THEME_AMBER

// ---- Video ---------------------------------------------------------------
// Output mode (compile-time).
//   VIDEO_MODE 0 = 800x480@60, 30 MHz pixel (clk_hstx 150 MHz), RGB332 8bpp.
//                  Native for the small Waveshare/PiCowBell LCD panel (5:3).
//   VIDEO_MODE 1 = 640x480@60 standard VGA, 25 MHz pixel (clk_hstx 125 MHz),
//                  RGB332 8bpp. True 4:3, any HDMI/DVI monitor scales it to fill.
//   VIDEO_MODE 2 = 1024x768@60 native, ~64.8 MHz pixel (clk_hstx ~324 MHz,
//                  HSTX OVERCLOCK). Monochrome 2bpp: a full-colour 1024x768
//                  framebuffer is 768 KB and will not fit RP2350 SRAM, so this
//                  mode stores 2 bits/pixel (4 brightness levels) = 192 KB and
//                  the HSTX encoder expands them to the phosphor colour in
//                  hardware. Fills a native 1024x768 panel pixel-crisp.
#define VIDEO_MODE 2

#if VIDEO_MODE == 2
#define FB_WIDTH   1024
#define FB_HEIGHT  768
#define FB_BPP     2      // monochrome on/off (4bpp AA path exists but is unstable here)
#elif VIDEO_MODE == 1
#define FB_WIDTH   640
#define FB_HEIGHT  480
#define FB_BPP     8      // RGB332
#else
#define FB_WIDTH   800
#define FB_HEIGHT  480
#define FB_BPP     8      // RGB332
#endif

// Bytes per framebuffer scanline (8bpp -> FB_WIDTH; 2bpp -> FB_WIDTH/4).
#define FB_STRIDE  (FB_WIDTH * FB_BPP / 8)

// Text grid. Cells are 8 wide (native font width -> crisp, no horizontal
// stretch). GLYPH_* is the font source cell; CELL_* the on-screen cell.
#if VIDEO_MODE == 2
// 80x25 at a 12x30 cell = 960x750, centred in 1024x768 -> 32px side / 9px top-
// bottom black borders (fills nearly the whole panel). The glyph is the classic
// IBM VGA 8x16 font (font_8x16), nearest-neighbour scaled up into the cell
// (~1.5x wide, ~1.9x tall). The left border also hides the E13 col-0 glitch.
#define GLYPH_W    8      // font source cell (matches font_8x16, the VGA font)
#define GLYPH_H    16
#define CELL_W     12
#define CELL_H     30
#define TERM_COLS  80
#define TERM_ROWS  25
#else
// 80x24 at 8x20 = 640x480 (fills 640 mode; pillarboxed in 800 mode). 8x16 glyph
// scaled 1.25x vertically to fill the height; line-drawing still connects.
#define GLYPH_W    8      // font source cell (do not change; matches font_8x16)
#define GLYPH_H    16
#define CELL_W     8
#define CELL_H     20
#define TERM_COLS  80
#define TERM_ROWS  24
#endif
#define TEXT_W     (TERM_COLS * CELL_W)
#define TEXT_H     (TERM_ROWS * CELL_H)
#define TEXT_X0    ((FB_WIDTH  - TEXT_W) / 2)
#define TEXT_Y0    ((FB_HEIGHT - TEXT_H) / 2)

// The framebuffer normally stores ONE extra text row beyond the visible height:
// a spare strip where the incoming line is drawn during a smooth scroll, as the
// scanout window's top row (the "scan origin") pans down through it. Mode 2 has
// smooth scroll disabled (it has top/bottom borders) and needs the RAM for its
// 4bpp framebuffer, so it drops the spare strip.
#if VIDEO_MODE == 2
#define FB_STORE_H FB_HEIGHT
#else
#define FB_STORE_H (FB_HEIGHT + CELL_H)
#endif

// ---- Serial --------------------------------------------------------------
// Host link = Waveshare RS232 ch0 (UART0 GP0/1 -> SP3232 -> DB9). Connect the
// DB9 to a real RS232 port (e.g. the PC's ttyS0) via a null-modem cable.
#define HOST_UART        uart0
#define HOST_UART_TX_PIN 0
#define HOST_UART_RX_PIN 1
#define HOST_BAUD        9600

#define AUX_UART         uart1
#define AUX_UART_TX_PIN  4
#define AUX_UART_RX_PIN  5
#define AUX_BAUD         9600

// ---- WiFi + Telnet -------------------------------------------------------
// These are only compile-time DEFAULTS pre-filled into the Setup screen; you can
// edit SSID / password / host / port live in Setup (Ctrl-F3) and they persist to
// flash. Leave them blank to ship publicly, or fill them in for your own build.
#define NET_ENABLE       1
#define NET_DIAG_IDLE    0   // diag: init cyw43 but don't join/poll (video test)
#define WIFI_SSID        ""            // your WiFi network name (set in Setup)
#define WIFI_PASS        ""            // your WiFi password    (set in Setup)
#define TELNET_HOST      ""            // default telnet host   (set in Setup)
#define TELNET_PORT      23

// ---- USB keyboard --------------------------------------------------------
#define PIO_USB_DP_PIN   6   // D+ on GP6; D- is DP+1 (GP7)
// PIO-USB uses PIO1 so PIO0/PIO2 stay free for the CYW43 wireless driver.
#define PIO_USB_PIO_NUM  1

// Echo locally typed keys to the screen as well as sending them to the host.
// A real terminal leaves this OFF (the host echoes). Turn on only to validate
// the keyboard standalone without a host attached.
#define LOCAL_ECHO       0

#endif // VT100_CONFIG_H
