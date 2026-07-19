// Minimal Telnet (RFC 854) client filter. Strips IAC command/negotiation
// sequences out of the server byte stream (emitting clean data to the terminal)
// and answers option negotiation so a normal telnetd is happy.
#ifndef TELNET_H
#define TELNET_H

#include <stdint.h>

// Callbacks the filter uses: `emit` receives clean data bytes (-> VT100 engine);
// `reply` sends raw bytes back to the server (negotiation responses).
typedef void (*telnet_emit_fn)(uint8_t byte);
typedef void (*telnet_reply_fn)(const uint8_t *buf, uint32_t len);

void telnet_init(telnet_emit_fn emit, telnet_reply_fn reply);
void telnet_reset(void);
void telnet_rx(const uint8_t *buf, uint32_t len);   // feed received TCP bytes

#endif // TELNET_H
