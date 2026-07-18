// USB keyboard input via raw evdev (/dev/input/eventN). No libinput/X11/
// Wayland dependency — this app owns the console directly, so reading
// input_event structs off the device node is all that's needed. Decodes key
// presses straight into VT100 byte sequences, mirroring the Pico build's
// kbd_host.c (DECCKM-aware arrows, Ctrl+letter -> C0, etc.) against Linux
// keycodes instead of USB HID usage codes.
#ifndef KBD_EVDEV_H
#define KBD_EVDEV_H

// Scans /dev/input/event* for the first device that looks like a keyboard
// (reports EV_KEY with a KEY_A bit set) and opens it non-blocking. Exits the
// process on failure to find one.
void kbd_init(void);

// The fd to include in the main poll() loop.
int kbd_fd(void);

// Drains pending input_event structs from the device and decodes them into
// the internal byte queue that kbd_getc() reads from. Call when poll()
// reports kbd_fd() readable.
void kbd_poll(void);

// Pop one decoded byte; -1 if the queue is empty.
int kbd_getc(void);

// Returns 1 (once) if Ctrl+F3 was pressed since the last call, else 0. The main
// loop uses this to open/close the Setup screen. Ctrl+F3 is never sent to host.
int kbd_take_setup_toggle(void);

#endif // KBD_EVDEV_H
