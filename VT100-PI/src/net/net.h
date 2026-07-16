// WiFi + Telnet transport. Brings up the CYW43 wireless on demand, joins the
// given network, and opens a Telnet TCP session to the given host. Received data
// is Telnet-filtered into a ring the main loop drains into the VT100 engine;
// net_send() carries keystrokes to the server.
//
// Nothing happens at boot: the radio stays off (zero video/power impact for a
// serial-only session) until net_connect() is called from the Setup menu.
#ifndef NET_H
#define NET_H

#include <stdint.h>

// Bring the radio up (first call does the heavy cyw43 firmware load), join the
// network and open the telnet session. Safe to call while the display is live;
// the video watchdog absorbs the one-time SPI burst. Returns 0 if it started.
int         net_connect(const char *ssid, const char *pass,
                        const char *host, uint16_t port);

// Close the telnet session and power the radio back down (back to idle).
void        net_stop(void);

void        net_poll(void);        // call every loop: drives join/DNS/TCP + lwIP
int         net_connected(void);   // 1 once the telnet TCP session is up
int         net_active(void);      // 1 once the radio has been brought up
int         net_getc(void);        // pop one received data byte, or -1
int         net_rx_level(void);     // bytes currently queued (scroll pacing)
void        net_send(const uint8_t *buf, uint32_t len);  // keystrokes -> server
const char *net_status(void);      // short human-readable status string

#endif // NET_H
