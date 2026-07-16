#include "io/sys_clock.h"
#include "config.h"

#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/structs/clocks.h"
#include "hardware/vreg.h"
#include "pico/time.h"

void sys_clock_init_dvi_usb(void) {
#if VIDEO_MODE == 2
    // 1024x768@60 needs clk_hstx ~324 MHz (see below) -- well above the HSTX's
    // nominal rating, so nudge the core regulator up a step for timing margin
    // BEFORE we run anything at that rate, and let it settle. clk_sys itself
    // stays at a modest 120 MHz, so only the HSTX domain is being pushed.
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(3);
#endif

    // 1) Decouple HSTX from clk_sys: drive it straight from PLL_SYS (150 MHz),
    //    which the default config already runs at. Do this BEFORE moving
    //    clk_sys, so the 30 MHz pixel clock is preserved throughout.
    clock_configure(clk_hstx,
                    0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    150 * MHZ, 150 * MHZ);

    // 2) Reprogram PLL_USB to 480 MHz (VCO 960 / 2 / 1). The CPU is still
    //    running from PLL_SYS, and clk_usb has no consumer yet (USB not started).
    pll_init(pll_usb, 1, 960 * MHZ, 2, 1);

    // 3) Re-derive the PLL_USB consumers at the new PLL rate.
    clock_configure(clk_usb, 0,
                    CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    480 * MHZ, 48 * MHZ);          // USB stays at 48 MHz
    clock_configure(clk_adc, 0,
                    CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    480 * MHZ, 48 * MHZ);

    // 4) Move clk_sys onto PLL_USB at 120 MHz (a clean /10 of 48 -> PIO-USB ok).
    clock_configure(clk_sys,
                    CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    480 * MHZ, 120 * MHZ);

    // 5) clk_peri (UART/SPI reference) follows the new clk_sys (120 MHz).
    clock_configure(clk_peri, 0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    120 * MHZ, 120 * MHZ);

#if VIDEO_MODE == 1
    // 6) 640x480 VGA needs a 25 MHz pixel clock = clk_hstx/5, i.e. clk_hstx =
    //    125 MHz. clk_sys now runs from PLL_USB, so PLL_SYS is free to retune to
    //    125 MHz solely for HSTX. (VCO 1500 MHz / 6 / 2 = 125 MHz.)
    pll_init(pll_sys, 1, 1500 * MHZ, 6, 2);
    clock_configure(clk_hstx, 0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    125 * MHZ, 125 * MHZ);
#elif VIDEO_MODE == 2
    // 6) 1024x768@60 (VESA DMT: H total 1344, V total 806) wants a 65 MHz pixel
    //    clock = clk_hstx/5 -> clk_hstx = 325 MHz. A clean 12 MHz-referenced PLL
    //    can't hit exactly 325 (needs VCO 1300, not a multiple of 12), so use
    //    VCO 1296 / 2 / 2 = 324 MHz -> 64.8 MHz pixel -> 59.8 Hz refresh, which
    //    every 1024x768 panel locks to. clk_sys runs from PLL_USB, so PLL_SYS is
    //    free to be dedicated to the HSTX overclock.
    pll_init(pll_sys, 1, 1296 * MHZ, 2, 2);
    clock_configure(clk_hstx, 0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    324 * MHZ, 324 * MHZ);
#endif
}
