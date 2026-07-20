// See service.h. Installs the running binary to a stable path, writes the
// systemd unit, and enables/disables it. Uses systemctl via system().
#include "service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UNIT_NAME "vt100-pi.service"
#define UNIT_PATH "/etc/systemd/system/" UNIT_NAME
#define BIN_PATH  "/usr/local/bin/vt100-pi-zero"

static const char *UNIT_TEXT =
    "[Unit]\n"
    "Description=VT100-PI-ZERO hardware terminal\n"
    "Conflicts=getty@tty1.service\n"
    "After=getty@tty1.service systemd-udevd.service\n"
    "Wants=systemd-udevd.service\n"
    "\n"
    "[Service]\n"
    "ExecStart=" BIN_PATH "\n"
    "Restart=always\n"
    "RestartSec=1\n"
    "User=root\n"
    "TTYPath=/dev/tty1\n"
    "StandardInput=tty\n"
    "StandardOutput=tty\n"
    "StandardError=journal\n"
    "\n"
    "[Install]\n"
    "WantedBy=multi-user.target\n";

int service_boot_enabled(void) {
    return system("systemctl is-enabled --quiet " UNIT_NAME) == 0;
}

int service_set_boot(int on) {
    if (!on) {
        system("systemctl disable " UNIT_NAME " 2>/dev/null");
        system("systemctl enable getty@tty1.service 2>/dev/null");   // restore console login
        return 0;
    }

    // Install the currently-running binary to a stable path (unless it's already
    // there), so the service has something fixed to exec.
    char self[512];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) return -1;
    self[n] = '\0';
    if (strcmp(self, BIN_PATH) != 0) {
        char cmd[1200];
        snprintf(cmd, sizeof cmd, "cp -f '%s' '%s' && chmod +x '%s'", self, BIN_PATH, BIN_PATH);
        if (system(cmd) != 0) { fprintf(stderr, "service: install binary failed\n"); return -1; }
    }

    // Clear any prior mask (a symlink to /dev/null) BEFORE writing, otherwise
    // fopen() below would just write through the symlink into /dev/null and the
    // unit would stay masked / unstartable.
    system("systemctl unmask " UNIT_NAME " 2>/dev/null");

    FILE *f = fopen(UNIT_PATH, "w");
    if (!f) { fprintf(stderr, "service: cannot write %s\n", UNIT_PATH); return -1; }
    fputs(UNIT_TEXT, f);
    fclose(f);

    system("systemctl daemon-reload");

    // Enable OUR unit first. Only if that succeeds do we take the console away
    // from getty — so a failure here can never leave the Pi with no console.
    if (system("systemctl enable " UNIT_NAME) != 0) {
        fprintf(stderr, "service: enable %s failed; leaving getty@tty1 in place\n", UNIT_NAME);
        return -1;
    }
    system("systemctl disable getty@tty1.service 2>/dev/null");   // free tty1 for next boot
    return 0;
}
