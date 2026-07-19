// Network host link: a TCP/Telnet connection that stands in for the serial host
// link. Clean (de-Telnet'd) bytes are handed to the emit callback (the main loop
// pushes them into the same scroll buffer as serial); keyboard/replies go back
// out the socket. SSH is layered on later via a PTY, not here.
#ifndef NETLINK_H
#define NETLINK_H

#include <stdint.h>

// Where received, de-Telnet'd bytes go (the main loop points this at its ring).
void netlink_set_emit(void (*emit)(uint8_t byte));

// Open a Telnet connection to host:port. Returns 0 on success, -1 on failure
// (name resolution / connect error). Non-blocking connect with a 5s timeout.
int  netlink_connect_telnet(const char *host, int port);

// Run the system `ssh` client to `dest` (host or user@host) over a PTY; the
// master fd becomes the host link (raw bytes). Returns 0 on success, -1 on
// failure to spawn. Password/host-key prompts appear in the terminal as usual.
int  netlink_connect_ssh(const char *dest);

void netlink_close(void);
int  netlink_connected(void);
int  netlink_fd(void);          // socket fd for poll(); -1 when not connected

// Drain the socket: read available bytes, run them through the Telnet filter
// (emit -> callback, negotiation replies -> socket). Detects and handles EOF.
void netlink_poll(void);

// Send bytes to the host (keyboard, DSR/DA replies), Telnet-escaping 0xFF.
void netlink_write(const uint8_t *buf, uint32_t len);

#endif // NETLINK_H
