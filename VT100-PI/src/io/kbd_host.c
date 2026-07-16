#include "io/kbd_host.h"
#include "config.h"
#include "terminal/vt100.h"

#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pico/stdlib.h"
#include "hardware/dma.h"

#include "pio_usb.h"
#include "tusb.h"

#include <string.h>

static queue_t     kbd_q;                 // decoded bytes, core1 -> core0
static volatile int mounted = 0;
static bool        caps_lock = false;
static uint8_t     prev_keys[6];

// Bring-up diagnostics.
static volatile int      dbg_stage     = 0;
static volatile int      dbg_core1_up  = 0;
static volatile int      dbg_dev_mounts = 0;
static volatile int      dbg_hid_mounts = 0;
static volatile unsigned dbg_vidpid    = 0;

int      kbd_dbg_stage(void)      { return dbg_stage; }
int      kbd_dbg_core1_up(void)   { return dbg_core1_up; }
int      kbd_dbg_dev_mounts(void) { return dbg_dev_mounts; }
int      kbd_dbg_hid_mounts(void) { return dbg_hid_mounts; }
unsigned kbd_dbg_vidpid(void)     { return dbg_vidpid; }

static void push(uint8_t c)         { queue_try_add(&kbd_q, &c); }
static void emit(const char *s)     { while (*s) push((uint8_t)*s++); }

int kbd_connected(void) { return mounted; }

int kbd_getc(void) {
    uint8_t c;
    return queue_try_remove(&kbd_q, &c) ? c : -1;
}

// ---- HID usage -> VT100 byte sequence (US layout) ------------------------
static volatile int setup_request = 0;
int kbd_setup_requested(void) { int r = setup_request; setup_request = 0; return r; }

static void emit_key(uint8_t mod, uint8_t key) {
    bool shift = mod & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
    bool ctrl  = mod & (KEYBOARD_MODIFIER_LEFTCTRL  | KEYBOARD_MODIFIER_RIGHTCTRL);
    bool alt   = mod & (KEYBOARD_MODIFIER_LEFTALT   | KEYBOARD_MODIFIER_RIGHTALT);

    // Ctrl+F3 opens/closes the local Setup screen; never sent to the host.
    if (ctrl && key == 0x3c) { setup_request = 1; return; }

    // Letters a-z
    if (key >= 0x04 && key <= 0x1d) {
        char c = 'a' + (key - 0x04);
        if (ctrl) { push((uint8_t)(c & 0x1f)); return; }   // Ctrl+letter -> C0
        if (shift ^ caps_lock) c -= 32;                    // uppercase
        if (alt) push(0x1b);
        push((uint8_t)c);
        return;
    }
    // Number row 1..0
    if (key >= 0x1e && key <= 0x27) {
        static const char un[] = "1234567890";
        static const char sh[] = "!@#$%^&*()";
        if (alt) push(0x1b);
        push((uint8_t)(shift ? sh[key - 0x1e] : un[key - 0x1e]));
        return;
    }

    switch (key) {
        case 0x28: push('\r'); if (vt100_newline_mode()) push('\n'); return; // Enter
        case 0x29: push(0x1b); return;                    // Esc
        case 0x2a: push(0x7f); return;                    // Backspace -> DEL
        case 0x2b: push('\t'); return;                    // Tab
        case 0x2c: push(' ');  return;                    // Space
        case 0x2d: push(shift ? '_' : '-'); return;
        case 0x2e: push(shift ? '+' : '='); return;
        case 0x2f: push(shift ? '{' : '['); return;
        case 0x30: push(shift ? '}' : ']'); return;
        case 0x31: push(shift ? '|' : '\\'); return;
        case 0x33: push(shift ? ':' : ';'); return;
        case 0x34: push(shift ? '"' : '\''); return;
        case 0x35: push(shift ? '~' : '`'); return;
        case 0x36: push(shift ? '<' : ','); return;
        case 0x37: push(shift ? '>' : '.'); return;
        case 0x38: push(shift ? '?' : '/'); return;
        case 0x39: caps_lock = !caps_lock; return;        // CapsLock

        // Arrows honour DECCKM (application cursor keys)
        case 0x4f: emit(vt100_cursor_keys_app() ? "\x1bOC" : "\x1b[C"); return; // Right
        case 0x50: emit(vt100_cursor_keys_app() ? "\x1bOD" : "\x1b[D"); return; // Left
        case 0x51: emit(vt100_cursor_keys_app() ? "\x1bOB" : "\x1b[B"); return; // Down
        case 0x52: emit(vt100_cursor_keys_app() ? "\x1bOA" : "\x1b[A"); return; // Up

        case 0x49: emit("\x1b[2~"); return;   // Insert
        case 0x4a: emit("\x1b[H");  return;   // Home
        case 0x4b: emit("\x1b[5~"); return;   // PageUp
        case 0x4c: emit("\x1b[3~"); return;   // Delete
        case 0x4d: emit("\x1b[F");  return;   // End
        case 0x4e: emit("\x1b[6~"); return;   // PageDown

        case 0x3a: emit("\x1bOP");   return;  // F1
        case 0x3b: emit("\x1bOQ");   return;  // F2
        case 0x3c: emit("\x1bOR");   return;  // F3
        case 0x3d: emit("\x1bOS");   return;  // F4
        case 0x3e: emit("\x1b[15~"); return;  // F5
        case 0x3f: emit("\x1b[17~"); return;  // F6
        case 0x40: emit("\x1b[18~"); return;  // F7
        case 0x41: emit("\x1b[19~"); return;  // F8
        case 0x42: emit("\x1b[20~"); return;  // F9
        case 0x43: emit("\x1b[21~"); return;  // F10
        case 0x44: emit("\x1b[23~"); return;  // F11
        case 0x45: emit("\x1b[24~"); return;  // F12
        default: return;
    }
}

static void process_report(hid_keyboard_report_t const *r) {
    for (int i = 0; i < 6; ++i) {
        uint8_t k = r->keycode[i];
        if (k == 0 || k == 1) continue;            // 0 = none, 1 = rollover error
        bool was_down = false;
        for (int j = 0; j < 6; ++j) if (prev_keys[j] == k) was_down = true;
        if (!was_down) emit_key(r->modifier, k);   // key-down edge only
    }
    memcpy(prev_keys, r->keycode, sizeof prev_keys);
}

// ---- TinyUSB host callbacks (run on core1) -------------------------------
// Generic device enumeration (any USB device, before class drivers).
void tuh_mount_cb(uint8_t dev_addr) {
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    dbg_vidpid = ((unsigned)vid << 16) | pid;
    dbg_dev_mounts++;
}
void tuh_umount_cb(uint8_t dev_addr) { (void)dev_addr; }

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const *desc_report, uint16_t desc_len) {
    (void)desc_report; (void)desc_len;
    dbg_hid_mounts++;
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD) {
        mounted++;
        memset(prev_keys, 0, sizeof prev_keys);
        tuh_hid_receive_report(dev_addr, instance);
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    (void)dev_addr; (void)instance;
    if (mounted > 0) mounted--;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const *report, uint16_t len) {
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD
        && len >= sizeof(hid_keyboard_report_t)) {
        process_report((hid_keyboard_report_t const *)report);
    }
    tuh_hid_receive_report(dev_addr, instance);   // keep the reports coming
}

// ---- core1: all USB host activity ---------------------------------------
static void core1_main(void) {
    dbg_stage = 1;                                 // reached core1
    // Let core0 park this core (which runs USB host code from flash) during a
    // flash write via multicore_lockout_*. Without this, a settings save would
    // execute flash on core1 mid-erase and hard-fault/hang the board.
    multicore_lockout_victim_init();
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = PIO_USB_DP_PIN;
    // Use PIO1 for USB so PIO0/PIO2 stay free for the CYW43 wireless driver.
    pio_cfg.pio_tx_num = PIO_USB_PIO_NUM;
    pio_cfg.pio_rx_num = PIO_USB_PIO_NUM;
    // PIO-USB defaults to DMA channel 0, which collides with the DVI scanout
    // channels. Hand it a guaranteed-free channel instead (PIO-USB re-claims it).
    int ch = dma_claim_unused_channel(true);
    dma_channel_unclaim(ch);
    pio_cfg.tx_ch = (uint8_t)ch;
    dbg_stage = 2;                                 // pio config built
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    dbg_stage = 3;                                 // configured
    tuh_init(1);                                   // roothub port 1 = PIO-USB
    dbg_stage = 4;
    dbg_core1_up = 1;
    while (true) tuh_task();
}

void kbd_host_start(void) {
    queue_init(&kbd_q, 1, 256);
    multicore_reset_core1();
    multicore_launch_core1(core1_main);
}
