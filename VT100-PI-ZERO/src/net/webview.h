// Web view: mirrors the terminal screen to a browser, for OBS (a Browser
// Source) or a projector. The Pi serves one page on WEB port; the page opens a
// Server-Sent Events stream of screen changes (cells, palette, cursor, scrolls)
// and redraws them with the Pi's own glyph atlas and the same blit maths as
// textmode.c, so the browser shows the same pixels the HDMI output does, at
// a few kB/s instead of a video stream. Read-only: nothing typed in the
// browser reaches the host.
//
// Runs inside the main poll() loop like everything else (no threads).
#ifndef WEBVIEW_H
#define WEBVIEW_H

#include <poll.h>
#include "config.h"   // WEB_MAX_CLIENTS

// Poll slots webview uses: the listening socket plus one per client.
#define WEBVIEW_POLL_SLOTS (1 + WEB_MAX_CLIENTS)

// Open (or close, enable = 0) the listener; reopens it when the port changed.
// Safe to call repeatedly with the same values.
void webview_configure(int enable, int port);

// Fill WEBVIEW_POLL_SLOTS pollfds before poll() (unused slots get fd -1), and
// service them afterwards.
void webview_fill_pollfds(struct pollfd *p);
void webview_service(const struct pollfd *p);

// Send any screen changes to the connected browsers. Call once per main-loop
// pass, after the screen has been updated; it rate-limits itself and costs a
// single compare when nothing changed (and nothing at all with no viewers).
void webview_tick(void);

// For the Setup menu: "off", "http://<ip>:8080/", "port 8080 in use", ...
// ip is this Pi's address as Setup already shows it.
void webview_describe(char *out, unsigned n, const char *ip);

#endif // WEBVIEW_H
