// System clock tree for simultaneous HSTX-DVI + PIO-USB.
//
// The RP2350 default ties clk_hstx to clk_sys (both 150 MHz). But Pico-PIO-USB
// requires clk_sys to be a clean multiple of 12 MHz (we use 120), while HSTX
// needs 150 MHz for our 30 MHz pixel clock. So we split the domains:
//   PLL_SYS (150 MHz) -> clk_hstx = 150   (video unchanged)
//   PLL_USB (480 MHz) -> clk_usb  = 48     (native USB CDC)
//                     -> clk_sys  = 120    (CPU + PIO-USB)
//                     -> clk_peri = 120    (UART/SPI reference)
// Call once at boot, before stdio/peripheral init.
#ifndef SYS_CLOCK_H
#define SYS_CLOCK_H

void sys_clock_init_dvi_usb(void);

#endif // SYS_CLOCK_H
