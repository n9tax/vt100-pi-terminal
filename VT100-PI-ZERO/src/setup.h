// On-screen Setup menu (Ctrl+F3). Edits g_settings live over a snapshot of the
// terminal screen, then saves to the config file and applies changes, or
// cancels and restores the screen. Driven from the main loop.
#ifndef SETUP_H
#define SETUP_H

#include <stdint.h>

int  setup_active(void);       // 1 while the menu owns the screen

// Toggle the menu: open (snapshot the screen) or cancel (restore it). Wired to
// Ctrl+F3 via kbd_take_setup_toggle().
void setup_toggle(void);

// Feed one decoded key byte (from kbd_getc) to the menu while it is active.
void setup_feed(uint8_t byte);

#endif // SETUP_H
