// The "host link" is whichever transport is currently carrying the terminal
// session — serial, telnet, or (later) ssh. vt100.c's DSR/DA replies and
// main.c's keyboard forwarding both write through this single indirection,
// so the terminal core never needs to know which transport is active.
#ifndef HOST_LINK_H
#define HOST_LINK_H

#include <stdint.h>

typedef void (*host_write_fn)(const uint8_t *buf, uint32_t len);

// Point host_write() at the active transport's writer. Call again whenever
// the active transport changes (e.g. serial -> telnet).
void host_link_set_write_fn(host_write_fn fn);

// Write to whichever transport is currently active. A no-op if none is set.
void host_write(const uint8_t *buf, uint32_t len);

#endif // HOST_LINK_H
