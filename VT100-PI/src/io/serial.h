// RS232 serial I/O over the Waveshare 2-channel hat.
//   Channel 0 = UART0 on GP0/GP1  (primary link to the host computer)
//   Channel 1 = UART1 on GP4/GP5  (secondary; available for Phase 7)
// RX is interrupt-driven into a ring buffer; TX is blocking.
#ifndef SERIAL_H
#define SERIAL_H

#include <stdbool.h>
#include <stdint.h>

void serial_init(void);

// Host channel (UART0) receive: pop one byte, or -1 if the ring is empty.
int  serial_getc(void);
bool serial_rx_ready(void);
int  serial_rx_level(void);   // bytes currently queued (for flow/scroll pacing)

// Host channel transmit (blocking).
void serial_putc(uint8_t c);
void serial_write(const uint8_t *buf, uint32_t len);

// Change the host channel baud rate at runtime (Setup screen).
void serial_set_baud(uint32_t baud);

#endif // SERIAL_H
