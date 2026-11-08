// RS232 host link over a Linux tty (the GPIO UART hat, /dev/serial0). Unlike
// the Pico build's IRQ + ring buffer, the kernel tty driver already buffers
// RX for us, so this is a thin termios wrapper: open, configure, non-blocking
// read/write, plus runtime baud change from Setup (once that lands).
#ifndef SERIAL_LINUX_H
#define SERIAL_LINUX_H

#include <stdint.h>

// Opens `path` (e.g. SERIAL_DEV) at `baud` and frame format `fmt`
// (SERIAL_FMT_*, settings.h), raw mode, non-blocking.
// Returns 0 on success, -1 if the port is missing or unusable -- which is NOT
// fatal: video, keyboard and any network transport still work, and Setup can
// point at another device. serial_fd() is then -1 and poll() ignores it.
int serial_init(const char *path, int baud, int fmt);

// 1 while a serial port is actually open.
int serial_connected(void);

// The fd to include in the main poll() loop.
int serial_fd(void);

// Non-blocking single-byte read; -1 if nothing pending or on error.
int serial_getc(void);

void serial_putc(uint8_t c);
void serial_write(const uint8_t *buf, uint32_t len);

// Re-applies termios with a new baud rate (values matching common RS232
// speeds: 300..115200).
void serial_set_baud(int baud);

// Hold the line in the space state for ~0.25-0.5 s: the BREAK key.
void serial_send_break(void);

// Switch to a different device, baud and/or format at runtime (from Setup).
// Opens the new device first and only then closes the old one, so a bad path
// leaves the current link untouched. Returns 0 on success, -1 on failure.
// NOTE: serial_fd() changes on success — refresh any stored poll fd.
int serial_reconfigure(const char *path, int baud, int fmt);

#endif // SERIAL_LINUX_H
