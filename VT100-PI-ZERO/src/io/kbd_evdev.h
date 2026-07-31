// USB keyboard input via raw evdev (/dev/input/eventN). No libinput/X11/
// Wayland dependency — this app owns the console directly, so reading
// input_event structs off the device node is all that's needed. Decodes key
// presses straight into VT100 byte sequences, mirroring the Pico build's
// kbd_host.c (DECCKM-aware arrows, Ctrl+letter -> C0, etc.) against Linux
// keycodes instead of USB HID usage codes.
//
// A composite USB keyboard publishes several event nodes (the Keychron C3 Pro
// exposes a boot-protocol keyboard, an NKRO keyboard, a mouse, System Control
// and Consumer Control), and which one carries ordinary typing depends on the
// keyboard's own mode. Rather than guess, we open EVERY node that advertises a
// full typing keyboard and read them all — see kbd_evdev.c.
#ifndef KBD_EVDEV_H
#define KBD_EVDEV_H

// Upper bound on simultaneously open keyboard nodes (main.c sizes its pollfd
// array with this). Enough for two composite keyboards plugged in at once.
#define KBD_MAX_DEVS 8

// Scan /dev/input/event* and open every device that advertises a complete
// typing keyboard. Never exits: with no keyboard attached the terminal still
// runs, and kbd_rescan() picks one up as soon as it is plugged in.
void kbd_init(void);

// The open keyboard fds, to include in the main poll() loop. kbd_fd_count() is
// 0 while no keyboard is present; kbd_fd_at(i) is valid for i < count.
int kbd_fd_count(void);
int kbd_fd_at(int i);

// Reap devices that have gone away and pick up newly plugged-in ones. Cheap
// (an opendir plus a few ioctls); call about once a second from the main loop.
void kbd_rescan(void);

// Drain pending input_event structs from every open device and decode them into
// the internal byte queue that kbd_getc() reads from. Call when poll() reports
// any keyboard fd readable (or in error — a device that has been unplugged is
// dropped here). Keypresses mirrored onto two nodes of the same composite
// keyboard are de-duplicated, so nothing types twice.
void kbd_poll(void);

// Pop one decoded byte; -1 if the queue is empty.
int kbd_getc(void);

// Returns 1 (once) if Ctrl+F3 was pressed since the last call, else 0. The main
// loop uses this to open/close the Setup screen. Ctrl+F3 is never sent to host.
int kbd_take_setup_toggle(void);

#endif // KBD_EVDEV_H
