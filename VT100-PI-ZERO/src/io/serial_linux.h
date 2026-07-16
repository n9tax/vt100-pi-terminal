// RS232 host link over a Linux tty (the GPIO UART hat, /dev/serial0). Unlike
// the Pico build's IRQ + ring buffer, the kernel tty driver already buffers
// RX for us, so this is a thin termios wrapper: open, configure, non-blocking
// read/write, plus runtime baud change from Setup (once that lands).
#ifndef SERIAL_LINUX_H
#define SERIAL_LINUX_H

#include <stdint.h>

// Opens `path` (e.g. SERIAL_DEV) at `baud`, 8N1, raw mode, non-blocking.
// Exits the process on failure (no serial host link is fatal for this app).
void serial_init(const char *path, int baud);

// The fd to include in the main poll() loop.
int serial_fd(void);

// Non-blocking single-byte read; -1 if nothing pending or on error.
int serial_getc(void);

void serial_putc(uint8_t c);
void serial_write(const uint8_t *buf, uint32_t len);

// Re-applies termios with a new baud rate (values matching common RS232
// speeds: 300..115200).
void serial_set_baud(int baud);

#endif // SERIAL_LINUX_H
