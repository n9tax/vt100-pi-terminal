// On-screen Setup menu (VT100 SET-UP style), opened with Ctrl+F3. While active,
// the terminal is "local": serial output is left buffered and keystrokes drive
// the menu instead of the host.
#ifndef SETUP_H
#define SETUP_H

#include <stdint.h>

int  setup_active(void);        // 1 while the menu is showing
void setup_toggle(void);        // enter / exit the menu
void setup_feed(uint8_t byte);  // feed a decoded key byte while active
void setup_refresh(void);       // redraw (e.g. to reflect live net status)

// Pending network action requested from the menu, for the main loop to run at a
// shallow stack depth: 0 = none, 1 = connect, 2 = disconnect. Clears on read.
int  setup_take_net_action(void);

#endif // SETUP_H
