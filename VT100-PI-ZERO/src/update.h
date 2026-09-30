// GitHub update check / install, as seen from the terminal. The work is done by
// /usr/local/bin/vt100-pi-update (update.sh) under systemd; this side only
// starts it and reads back the status file it writes.
#ifndef UPDATE_H
#define UPDATE_H

#include <stddef.h>

// 1 if install.sh set the updater up (it needs a git clone to update from).
int  update_installed(void);

// 1 if the last check found newer commits waiting to be installed.
int  update_available(void);

// Kick off a check, or pull+build+install when an update is waiting. Returns at
// once; the result lands in the status file. Installing restarts the terminal.
void update_start(void);

// Subject of the newest waiting commit, or "" when nothing is waiting.
const char *update_subject(void);

// One line for the Setup row: "3 new (a1b2c3d) -- Enter to install", ...
void update_describe(char *out, size_t n);

#endif // UPDATE_H
