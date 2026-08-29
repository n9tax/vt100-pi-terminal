// Host-link supervisor: decides which transport should be carrying the session
// and keeps it alive.
//
// netlink.c knows how to *open* a link; this knows *which* one the settings ask
// for (priority: local shell > ssh > telnet > serial), notices when it drops,
// and re-dials it with a backoff when auto-reconnect is on. It also owns the
// host_write() indirection, so the rest of the program never has to reason
// about which transport is live: main.c and setup.c call in here instead of
// duplicating the connect chain.
//
// A dropped link falls back to serial for writes, so the RS232 host link keeps
// working while the network link is retried in the background.
#ifndef HOSTCONN_H
#define HOSTCONN_H

#include <stdint.h>

// Where received bytes go (main.c points this at its input ring). Status
// notices ("[link closed]", "reconnecting...") are written through the same
// callback so they land in the terminal like any other host output.
void hostconn_set_emit(void (*emit)(uint8_t byte));

// Open the link the current settings describe, cancelling any pending retry.
// Called at boot and after Setup saves. If nothing non-serial is configured
// this just parks on serial. A failed attempt schedules a retry when
// auto-reconnect is on.
void hostconn_start(void);

// Call every main-loop iteration: drains the link, detects a drop, and runs
// the retry timer. Cheap when nothing is connected.
void hostconn_poll(void);

// 1 while a non-serial link is up (serial is the fallback, not a "connection").
int hostconn_connected(void);

// The live link's fd for the main poll() set; -1 when nothing is connected.
// Changes on every connect/disconnect, so re-read it each time round the loop.
int hostconn_fd(void);

// 1 if the settings name a non-serial destination at all.
int hostconn_have_target(void);

// Setup's manual "reconnect now": drop the current link and re-dial straight
// away, resetting the backoff. Works whether or not auto-reconnect is on.
void hostconn_reconnect_now(void);

// One-line state for the Setup menu, e.g. "connected: ssh pi@10.0.0.5" or
// "closed - retry in 8s". Valid until the next hostconn_poll().
const char *hostconn_status(void);

#endif // HOSTCONN_H
