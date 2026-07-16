// HSTX DVI scanout. Adapted from pico-examples/hstx/dvi_out_hstx_encoder
// (Copyright (c) 2024 Raspberry Pi (Trading) Ltd., BSD-3-Clause), reworked to
// scan out a full RGB332 framebuffer that the terminal renderer writes into.
#include "video/hstx_dvi.h"

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/resets.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"

// ---- DVI 640x480@60 timing ----------------------------------------------
#define TMDS_CTRL_00 0x354u
#define TMDS_CTRL_01 0x0abu
#define TMDS_CTRL_10 0x154u
#define TMDS_CTRL_11 0x2abu

#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

#if VIDEO_MODE == 2
// 1024x768@60 native (VESA DMT). 64.8 MHz pixel (clk_hstx/5, clk_hstx=324 MHz),
// negative sync. Totals H=1344, V=806 -> 64.8e6/(1344*806) = 59.8 Hz.
#define MODE_H_FRONT_PORCH   24
#define MODE_H_SYNC_WIDTH    136
#define MODE_H_BACK_PORCH    160
#define MODE_V_FRONT_PORCH   3
#define MODE_V_SYNC_WIDTH    6
#define MODE_V_BACK_PORCH    29
#elif VIDEO_MODE == 1
// 640x480@60 standard VGA. 25 MHz pixel (clk_hstx/5, clk_hstx=125 MHz), negative
// sync. Totals H=800, V=525 -> 25e6/(800*525) = 59.5 Hz. Every DVI/HDMI monitor
// locks to this and scales it to fill the panel.
#define MODE_H_FRONT_PORCH   16
#define MODE_H_SYNC_WIDTH    96
#define MODE_H_BACK_PORCH    48
#define MODE_V_FRONT_PORCH   10
#define MODE_V_SYNC_WIDTH    2
#define MODE_V_BACK_PORCH    33
#else
// 800x480@60, ~30 MHz pixel clock. Blanking chosen so the totals are
// H_TOTAL=1000, V_TOTAL=500 -> 30e6/(1000*500) = 60.0 Hz exactly.
// (Close to the CVT "800x480_60" modeline; small LCD scalers lock to this.)
#define MODE_H_FRONT_PORCH   24
#define MODE_H_SYNC_WIDTH    72
#define MODE_H_BACK_PORCH    104
#define MODE_V_FRONT_PORCH   3
#define MODE_V_SYNC_WIDTH    10
#define MODE_V_BACK_PORCH    7
#endif
#define MODE_H_ACTIVE_PIXELS FB_WIDTH
#define MODE_V_ACTIVE_LINES  FB_HEIGHT

#define MODE_V_TOTAL_LINES ( \
    MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH + MODE_V_ACTIVE_LINES)

#define HSTX_CMD_RAW         (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT  (0x1u << 12)
#define HSTX_CMD_TMDS        (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)
#define HSTX_CMD_NOP         (0xfu << 12)

// The framebuffer scanned out during the active display period. FB_STRIDE bytes
// per scanline (== FB_WIDTH for 8bpp RGB332; FB_WIDTH/4 for 2bpp monochrome).
uint8_t dvi_framebuf[FB_STRIDE * FB_STORE_H];

// Smooth-scroll pan state. scan_origin is the physical framebuffer row that
// display scanline 0 reads (0..FB_STORE_H-1). scan_pending is how many more
// pixels to slide; scan_step is the per-frame increment. The scanline IRQ steps
// it once per frame during vblank (see dma_irq_handler) — frame-locked, so it is
// smooth regardless of core0 load, and tear-free since it only moves at vblank.
static volatile uint32_t scan_origin = 0;
static volatile int      scan_pending = 0;
static volatile int      scan_step    = 4;

int hstx_dvi_scroll_busy(void) { return scan_pending != 0; }

void hstx_dvi_scroll_snap(void) {
    if (scan_pending > 0) {
        uint32_t o = scan_origin + (uint32_t)scan_pending;
        if (o >= FB_STORE_H) o -= FB_STORE_H;
        scan_origin = o;
        scan_pending = 0;
    }
}

void hstx_dvi_scroll_line(uint32_t step_px) {
    hstx_dvi_scroll_snap();               // never stack two slides (one spare line)
    scan_step = step_px ? (int)step_px : 1;
    scan_pending = CELL_H;
}

// ---- HSTX command lists (one per scanline class) -------------------------
static uint32_t vblank_line_vsync_off[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V1_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
    SYNC_V1_H1,
    HSTX_CMD_NOP
};

static uint32_t vblank_line_vsync_on[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V0_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V0_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
    SYNC_V0_H1,
    HSTX_CMD_NOP
};

static uint32_t vactive_line[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V1_H0,
    HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_BACK_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_TMDS       | MODE_H_ACTIVE_PIXELS
};

// ---- Ping-pong DMA -------------------------------------------------------
#define DMACH_PING 0
#define DMACH_PONG 1

static bool dma_pong = false;
static uint v_scanline = 2;
static bool vactive_cmdlist_posted = false;

// Advances on every real scanline IRQ; a watchdog on core0 uses it to detect a
// stalled scanout and re-arm it.
volatile uint32_t hstx_dvi_hb = 0;
uint32_t hstx_dvi_heartbeat(void) { return hstx_dvi_hb; }

void __not_in_flash_func(dma_irq_handler)(void) {
    // Ignore purely spurious IRQs (neither of our channels flagged) so an E13
    // stray interrupt from a CYW43 DMA abort can't advance our ping-pong state.
    if (!(dma_hw->ints0 & ((1u << DMACH_PING) | (1u << DMACH_PONG)))) return;
    hstx_dvi_hb++;
    uint ch_num = dma_pong ? DMACH_PONG : DMACH_PING;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    dma_pong = !dma_pong;

    if (v_scanline >= MODE_V_FRONT_PORCH &&
        v_scanline < (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH)) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (v_scanline < MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if (!vactive_cmdlist_posted) {
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        vactive_cmdlist_posted = true;
    } else {
        uint y = v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES);
        // Ring-map through the scan origin so smooth scroll can pan the window.
        // y < FB_HEIGHT and origin < FB_STORE_H, so one subtract re-normalises.
        uint py = y + scan_origin;
        if (py >= FB_STORE_H) py -= FB_STORE_H;
        ch->read_addr = (uintptr_t)&dvi_framebuf[py * (uint)FB_STRIDE];
        ch->transfer_count = FB_STRIDE / sizeof(uint32_t);
        vactive_cmdlist_posted = false;
    }

    // Step the smooth-scroll pan once per frame, during vblank (v_scanline 0), so
    // the whole upcoming frame scans at the new origin (no mid-frame tear) and the
    // cadence is a rock-steady 60 Hz independent of what core0 is doing.
    if (v_scanline == 0 && scan_pending > 0) {
        int s = scan_step;
        if (s > scan_pending) s = scan_pending;
        uint32_t o = scan_origin + (uint32_t)s;
        if (o >= FB_STORE_H) o -= FB_STORE_H;
        scan_origin = o;
        scan_pending -= s;
    }

    if (!vactive_cmdlist_posted) {
        v_scanline = (v_scanline + 1) % MODE_V_TOTAL_LINES;
    }
}

// All HSTX peripheral register setup (TMDS/shift expanders, CSR, lane routing,
// GPIO). Shared by init and by the kick so a re-arm reproduces the exact clean
// boot state. Assumes the HSTX block was just taken out of reset (empty FIFO,
// command state machine at a known start), which is essential: restarting the
// scanout DMA against a half-consumed HSTX command stream desyncs it forever.

#if FB_BPP == 2 || FB_BPP == 4
// ---- Per-theme phosphor tint (monochrome) --------------------------------
// The pixel holds a brightness value in its low bits with the TOP bit kept 0 (a
// guaranteed-zero reference, maintained by textmode's palette). The HSTX
// expander builds each colour lane from those bits, MSB-aligned into the 8-bit
// channel, so per lane we choose FULL (value at full scale), HALF (value at
// ~half scale, using the zero bit as its MSB), or OFF (reads the zero bit -> 0).
// Mixing FULL/HALF/OFF across R/G/B tints the WHOLE display for one register
// write, zero per-pixel CPU, no smearing (every read references a known bit of
// THIS pixel). Amber = R FULL + G HALF + B OFF. In 2bpp the value is 1 bit
// (on/off); in 4bpp it is a 3-bit brightness 0..7 (bit3 = the zero ref) giving
// anti-aliased amber. DVI lane order on this wiring is L0=Blue, L1=Green, L2=Red.
typedef enum { LN_OFF, LN_HALF, LN_FULL } lane_t;

static volatile int cur_tint = THEME_DEFAULT;

static uint32_t lane_field(lane_t l, uint nbits_lsb, uint rot_lsb) {
    uint nbits, rot;
#if FB_BPP == 4
    // value in bits[2:0], bit3 == 0. FULL: read [2:0] -> V<<5. HALF: read [3:0]
    // = {0,V} -> V<<4. OFF: read bit3 (==0) -> 0.
    switch (l) {
        case LN_FULL: nbits = 2; rot = 27; break;
        case LN_HALF: nbits = 3; rot = 28; break;
        default:      nbits = 0; rot = 28; break;
    }
#else
    // value in bit0, bit1 == 0. FULL: read bit0 -> on<<7. HALF: read {bit1,bit0}
    // -> on<<6. OFF: read bit1 (==0) -> 0.
    switch (l) {
        case LN_FULL: nbits = 0; rot = 25; break;
        case LN_HALF: nbits = 1; rot = 26; break;
        default:      nbits = 0; rot = 26; break;
    }
#endif
    return (nbits << nbits_lsb) | (rot << rot_lsb);
}

static void apply_tint(void) {
    lane_t r, g, b;
    switch (cur_tint) {
        case THEME_GREEN:     r=LN_OFF;  g=LN_FULL; b=LN_OFF;  break;  // pure green
        case THEME_WHITE:     r=LN_FULL; g=LN_FULL; b=LN_FULL; break;  // white/grey
        case THEME_BLUE:      r=LN_OFF;  g=LN_HALF; b=LN_FULL; break;  // blue phosphor
        case THEME_RED:       r=LN_FULL; g=LN_OFF;  b=LN_OFF;  break;  // red
        case THEME_YELLOW:    r=LN_FULL; g=LN_FULL; b=LN_OFF;  break;  // yellow
        case THEME_COMMODORE: r=LN_HALF; g=LN_FULL; b=LN_FULL; break;  // C64 light blue
        case THEME_BORLAND:   r=LN_FULL; g=LN_FULL; b=LN_OFF;  break;  // yellow-ish
        case THEME_AMBER:
        default:              r=LN_FULL; g=LN_HALF; b=LN_OFF;  break;  // amber (R + ½G)
    }
    hstx_ctrl_hw->expand_tmds =
        lane_field(b, HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB, HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB) |
        lane_field(g, HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB, HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB) |
        lane_field(r, HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB, HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB);
}

void hstx_dvi_set_tint(int theme) {
    cur_tint = theme;
    apply_tint();
}
#endif

static void hstx_peripheral_config(void) {
#if FB_BPP == 2 || FB_BPP == 4
    apply_tint();   // per-theme lane routing (defaults to THEME_DEFAULT)

    // 32/FB_BPP pixels per 32-bit word (FB_BPP bits each); RAW words pass whole.
    hstx_ctrl_hw->expand_shift =
        (32 / FB_BPP) << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
        FB_BPP        << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1             << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
        0             << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;
#else
    // TMDS encoder for RGB332: input byte decoded as B[7:5] G[4:2] R[1:0].
    hstx_ctrl_hw->expand_tmds =
        2  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
        0  << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB   |
        2  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
        29 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB   |
        1  << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
        26 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;

    // Pixels expand as 4 8-bit chunks; RAW control words are whole 32-bit words.
    hstx_ctrl_hw->expand_shift =
        4 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
        8 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
        0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;
#endif

    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        2u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS;

    // Clock pair on HSTX bits 2/3 (GP14/GP15); TMDS lanes on the PiCowBell /
    // Pico-DVI-Sock pinout: D0=GP12/13, D2=GP16/17, D1=GP18/19.
    hstx_ctrl_hw->bit[2] = HSTX_CTRL_BIT0_CLK_BITS;
    hstx_ctrl_hw->bit[3] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    for (uint lane = 0; lane < 3; ++lane) {
        static const int lane_to_output_bit[3] = {0, 6, 4};
        int bit = lane_to_output_bit[lane];
        uint32_t lane_data_sel_bits =
            (lane * 10    ) << HSTX_CTRL_BIT0_SEL_P_LSB |
            (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
        hstx_ctrl_hw->bit[bit    ] = lane_data_sel_bits;
        hstx_ctrl_hw->bit[bit + 1] = lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
    }

    for (int i = 12; i <= 19; ++i) {
        gpio_set_function(i, 0); // HSTX function
    }
}

static void configure_scan_channels(void) {
    dma_channel_config c;
    c = dma_channel_get_default_config(DMACH_PING);
    channel_config_set_chain_to(&c, DMACH_PONG);
    channel_config_set_dreq(&c, DREQ_HSTX);
    channel_config_set_high_priority(&c, true);
    dma_channel_configure(DMACH_PING, &c, &hstx_fifo_hw->fifo,
                          vblank_line_vsync_off, count_of(vblank_line_vsync_off), false);
    c = dma_channel_get_default_config(DMACH_PONG);
    channel_config_set_chain_to(&c, DMACH_PING);
    channel_config_set_dreq(&c, DREQ_HSTX);
    channel_config_set_high_priority(&c, true);
    dma_channel_configure(DMACH_PONG, &c, &hstx_fifo_hw->fifo,
                          vblank_line_vsync_off, count_of(vblank_line_vsync_off), false);
}

// Fully re-arm the ping-pong DMA from a clean state. Called by the core0
// watchdog when the heartbeat stalls, and rebuilds the channel config in case a
// glitch corrupted it (chain/dreq), then restarts scanout.
//
// The abort MUST follow the RP2350-E5 sequence (see hardware/dma.h): our two
// channels chain to each other, so aborting one while the other is enabled just
// re-triggers it and the abort never completes. Disable BOTH channels' enable
// bits first (non-triggering al1_ctrl alias), then abort both together and wait
// for them to drain. Doing this wrong is why a plain abort left the screen stuck
// white instead of recovering.
void hstx_dvi_kick(void) {
    // 1) Stop the scanout DMA cleanly. Our two channels chain to each other, so
    //    per RP2350-E5 we must clear BOTH enable bits (non-triggering al1_ctrl
    //    alias) before the abort, or the abort re-triggers through the chain and
    //    never completes. Then abort both together and wait for a real drain.
    hw_clear_bits(&dma_hw->ch[DMACH_PING].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    hw_clear_bits(&dma_hw->ch[DMACH_PONG].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_hw->abort = (1u << DMACH_PING) | (1u << DMACH_PONG);
    // Wait for the abort to drain, but NEVER spin forever: an E13 glitch (or the
    // HSTX overclock under heavy load) can leave a channel wedged "busy", and an
    // unbounded wait here would hang core0 permanently (frozen/dark screen, only
    // recoverable by a manual reset). Bound it (~1 ms at 120 MHz); the HSTX block
    // reset below then re-arms scanout cleanly whether or not the drain finished.
    for (uint32_t t = 0; (dma_hw->ch[DMACH_PING].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS)
                         && t < 120000u; ++t) tight_loop_contents();
    for (uint32_t t = 0; (dma_hw->ch[DMACH_PONG].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS)
                         && t < 120000u; ++t) tight_loop_contents();

    // 2) Reset the HSTX peripheral itself. This is the crucial step: without it
    //    the HSTX FIFO/command state is left half-consumed and restarting the DMA
    //    just re-desyncs it (the picture stays white). A block reset empties the
    //    FIFO and returns the command engine to a known start.
    reset_block_mask(RESETS_RESET_HSTX_BITS);
    unreset_block_mask_wait_blocking(RESETS_RESET_HSTX_BITS);
    hstx_peripheral_config();

    // 3) Rebuild and restart the ping-pong from scanline 2 (matches init).
    v_scanline = 2;
    vactive_cmdlist_posted = false;
    dma_pong = false;
    dma_hw->ints0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    configure_scan_channels();
    dma_channel_start(DMACH_PING);
}

void hstx_dvi_reserve_dma(void) {
    if (!dma_channel_is_claimed(DMACH_PING)) dma_channel_claim(DMACH_PING);
    if (!dma_channel_is_claimed(DMACH_PONG)) dma_channel_claim(DMACH_PONG);
}

void hstx_dvi_init(void) {
    hstx_peripheral_config();

    // Channels DMACH_PING/PONG must already be claimed (see hstx_dvi_reserve_dma),
    // so PIO-USB never grabs them. Claim here too if we're the first user.
    if (!dma_channel_is_claimed(DMACH_PING)) dma_channel_claim(DMACH_PING);
    if (!dma_channel_is_claimed(DMACH_PONG)) dma_channel_claim(DMACH_PONG);

    // Mark the scanout channels HIGH PRIORITY so the DMA arbiter always services
    // them ahead of any other DMA (e.g. PIO-USB's channel on core1). Together
    // with bus_ctrl DMA priority (DMA over CPUs) this keeps the HSTX FIFO fed and
    // prevents the sync-loss/white-screen when USB is busy.
    dma_channel_config c;
    c = dma_channel_get_default_config(DMACH_PING);
    channel_config_set_chain_to(&c, DMACH_PONG);
    channel_config_set_dreq(&c, DREQ_HSTX);
    channel_config_set_high_priority(&c, true);
    dma_channel_configure(DMACH_PING, &c, &hstx_fifo_hw->fifo,
                          vblank_line_vsync_off, count_of(vblank_line_vsync_off), false);

    c = dma_channel_get_default_config(DMACH_PONG);
    channel_config_set_chain_to(&c, DMACH_PING);
    channel_config_set_dreq(&c, DREQ_HSTX);
    channel_config_set_high_priority(&c, true);
    dma_channel_configure(DMACH_PONG, &c, &hstx_fifo_hw->fifo,
                          vblank_line_vsync_off, count_of(vblank_line_vsync_off), false);

    dma_hw->ints0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    dma_hw->inte0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    // The scanline IRQ must be serviced within one line (~31 us) or the HSTX
    // FIFO desyncs and the picture collapses to garbage. Raise it above USB and
    // everything else so it always preempts.
    irq_set_priority(DMA_IRQ_0, PICO_HIGHEST_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);

    // Video DMA gets top bus priority so the HSTX FIFO never underruns, even
    // when core1 is busy servicing PIO-USB. (Do NOT also raise a CPU here: a
    // prioritised core starves this DMA and the picture collapses to garbage.)
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    dma_channel_start(DMACH_PING);
}
