// USB keyboard host over PIO-USB (GP6/GP7). TinyUSB host runs on core1; decoded
// key bytes (VT100 sequences) are pushed to a cross-core queue that core0 drains
// and forwards to the serial host (and optionally echoes locally).
#ifndef KBD_HOST_H
#define KBD_HOST_H

void kbd_host_start(void);   // init queue + launch core1 USB host
int  kbd_getc(void);         // pop one decoded byte on core0, or -1 if empty
int  kbd_connected(void);    // 1 while a keyboard is mounted
int  kbd_setup_requested(void); // 1 (once) if Ctrl+F3 was pressed

// Bring-up diagnostics (all updated on core1, read on core0).
int      kbd_dbg_stage(void);     // core1 init progress: 1..4 (4 = task loop)
int      kbd_dbg_core1_up(void);  // 1 once core1 reached its USB task loop
int      kbd_dbg_dev_mounts(void);// count of ANY USB device enumerations
int      kbd_dbg_hid_mounts(void);// count of HID-keyboard mounts
unsigned kbd_dbg_vidpid(void);    // (vid<<16)|pid of last mounted device

#endif // KBD_HOST_H
