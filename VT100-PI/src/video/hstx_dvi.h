// HSTX-based DVI/HDMI output, 640x480@60, RGB332 framebuffer.
// Adapted from pico-examples/hstx/dvi_out_hstx_encoder (RPi, BSD-3-Clause).
#ifndef HSTX_DVI_H
#define HSTX_DVI_H

#include <stdint.h>
#include "config.h"

// The scanned-out framebuffer: FB_STRIDE bytes per scanline x FB_STORE_H rows
// (FB_STORE_H = visible height + one spare line for smooth scroll). One RGB332
// byte per pixel in 8bpp modes; four 2-bit monochrome pixels per byte in 2bpp
// mode. DMA reads it directly during the active display period.
extern uint8_t dvi_framebuf[FB_STRIDE * FB_STORE_H];

// Smooth scroll. The scanout window can be panned down through the spare strip
// (wrapping modulo FB_STORE_H). The pan is stepped inside the scanline IRQ, once
// per frame during vblank, so it stays perfectly smooth and tear-free no matter
// how busy core0 is. hstx_dvi_scroll_line() begins sliding one text line at
// step_px pixels per frame; _snap finishes instantly; _busy is 1 while sliding.
void hstx_dvi_scroll_line(uint32_t step_px);
void hstx_dvi_scroll_snap(void);
int  hstx_dvi_scroll_busy(void);

// Pack r,g,b (each 0..255) into an RGB332 byte. The HSTX TMDS expander config
// in hstx_dvi.c fixes the bit-field widths as [7:5]/[4:2]/[1:0] = 3/3/2 bits.
// On this PiCowBell + 800x480 panel the data lanes come out with Red and Blue
// swapped relative to the Pico-DVI-Sock reference, so we map Red into the top
// field and Blue into the bottom: panel decodes [7:5]=Red, [4:2]=Green, [1:0]=Blue.
static inline uint8_t rgb332(uint8_t r, uint8_t g, uint8_t b) {
    return (uint8_t)((r & 0xe0) | ((g & 0xe0) >> 3) | ((b & 0xc0) >> 6));
}

// Reserve the two DMA channels HSTX will use, so other DMA users (e.g. PIO-USB)
// never allocate them. Safe to call early, before DVI is actually started.
void hstx_dvi_reserve_dma(void);

// Configure HSTX, GPIO 12-19, and the ping-pong DMA, then start scanout.
// Returns after DMA is running; scanout continues via the DMA IRQ handler.
void hstx_dvi_init(void);

// Scanout heartbeat (advances every scanline IRQ) + re-arm after a stall.
uint32_t hstx_dvi_heartbeat(void);
void     hstx_dvi_kick(void);

// 2bpp monochrome: set the phosphor hue (THEME_*) applied by the HSTX TMDS
// expander. No-op / unused in 8bpp RGB332 modes.
void hstx_dvi_set_tint(int theme);

#endif // HSTX_DVI_H
