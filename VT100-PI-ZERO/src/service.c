// See service.h. Installs the running binary to a stable path, writes the
// systemd unit, and enables/disables it. Uses systemctl via system().
#include "service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define UNIT_NAME "vt100-pi.service"
#define UNIT_PATH "/etc/systemd/system/" UNIT_NAME
#define BIN_PATH  "/usr/local/bin/vt100-pi-zero"

// Early-boot banner: prints "Wait." on tty1 (console font) while Linux boots,
// like a real VT320's self-test, until our app grabs the screen. See service.c.
#define WAIT_NAME      "vt100-wait.service"
#define WAIT_UNIT_PATH "/etc/systemd/system/" WAIT_NAME
#define WAIT_BIN       "/usr/local/bin/vt100-wait"

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

// ---- boot quieting (mask the Linux boot chatter behind our own splash) -----
// All edits are on the Pi's boot partition and are undone from *.vt100.bak on
// disable. None of these flags affect *whether* the Pi boots, only what prints.

// Locate a boot-partition file: /boot/firmware (Bookworm) or /boot (older).
static const char *find_boot_file(const char *name, char *out, size_t n) {
    snprintf(out, n, "/boot/firmware/%s", name);
    if (access(out, F_OK) == 0) return out;
    snprintf(out, n, "/boot/%s", name);
    if (access(out, F_OK) == 0) return out;
    return NULL;
}

// Copy path -> path.vt100.bak once (never clobbers an existing backup).
static void backup_once(const char *path) {
    char cmd[600];
    snprintf(cmd, sizeof cmd, "cp -n '%s' '%s.vt100.bak' 2>/dev/null", path, path);
    system(cmd);
}

// Whitespace-delimited exact-token membership test.
static int has_token(const char *line, const char *tok) {
    size_t tl = strlen(tok);
    for (const char *p = line; *p; ) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        const char *s = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        if ((size_t)(p - s) == tl && strncmp(s, tok, tl) == 0) return 1;
    }
    return 0;
}

// Rewrite cmdline.txt (must stay one line): send console text to tty3, silence
// the kernel, drop the boot logos and the console cursor. Leaves tty1 blank for
// our app to take over.
static void quiet_cmdline(void) {
    char path[128];
    if (!find_boot_file("cmdline.txt", path, sizeof path)) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[1024];
    if (!fgets(line, sizeof line, f)) { fclose(f); return; }
    fclose(f);
    line[strcspn(line, "\r\n")] = '\0';
    backup_once(path);

    char out[1400] = "";
    int  first = 1, saw_console_tty = 0;
    char work[1024];
    snprintf(work, sizeof work, "%s", line);
    char *save = NULL;
    for (char *t = strtok_r(work, " \t", &save); t; t = strtok_r(NULL, " \t", &save)) {
        const char *emit = t;
        if (strcmp(t, "console=tty1") == 0) { emit = "console=tty3"; saw_console_tty = 1; }
        else if (strcmp(t, "console=tty3") == 0) saw_console_tty = 1;
        if (!first) strncat(out, " ", sizeof out - strlen(out) - 1);
        strncat(out, emit, sizeof out - strlen(out) - 1);
        first = 0;
    }
    const char *want[] = { "quiet", "loglevel=3", "logo.nologo", "vt.global_cursor_default=0" };
    for (int i = 0; i < 4; ++i)
        if (!has_token(out, want[i])) {
            strncat(out, " ", sizeof out - strlen(out) - 1);
            strncat(out, want[i], sizeof out - strlen(out) - 1);
        }
    if (!saw_console_tty) strncat(out, " console=tty3", sizeof out - strlen(out) - 1);

    FILE *w = fopen(path, "w");
    if (!w) { fprintf(stderr, "service: cannot write %s\n", path); return; }
    fprintf(w, "%s\n", out);
    fclose(w);
}

// Append disable_splash=1 to config.txt (kills the firmware rainbow square).
static void quiet_config(void) {
    char path[128];
    if (!find_boot_file("config.txt", path, sizeof path)) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char buf[8192];
    size_t nrd = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[nrd] = '\0';
    if (strstr(buf, "disable_splash")) return;   // already set
    backup_once(path);
    FILE *w = fopen(path, "a");
    if (!w) return;
    fputs("\n# Added by VT100-PI\ndisable_splash=1\n", w);
    fclose(w);
}

static void quiet_boot(void)   { quiet_cmdline(); quiet_config(); }

// Restore the boot files from the backups we made (undo the quieting).
static void restore_boot(void) {
    const char *names[] = { "cmdline.txt", "config.txt" };
    for (int i = 0; i < 2; ++i) {
        char path[128];
        if (!find_boot_file(names[i], path, sizeof path)) continue;
        char bak[160];
        snprintf(bak, sizeof bak, "%s.vt100.bak", path);
        if (access(bak, F_OK) != 0) continue;
        char cmd[600];
        snprintf(cmd, sizeof cmd, "cp -f '%s' '%s'", bak, path);
        system(cmd);
    }
}

// ---- boot "Wait." banner (removed) -----------------------------------------
// The early-boot "Wait" banner was dropped: it was more trouble than it was
// worth (it made the tty1 text console visible, which surfaced other issues).
// remove_wait_banner() stays so enabling/disabling start-at-boot cleans up any
// previously installed vt100-wait.service / /usr/local/bin/vt100-wait.
static void remove_wait_banner(void) {
    system("systemctl disable " WAIT_NAME " 2>/dev/null");
    unlink(WAIT_UNIT_PATH);
    unlink(WAIT_BIN);
    system("systemctl daemon-reload");
}

int service_boot_enabled(void) {
    return system("systemctl is-enabled --quiet " UNIT_NAME) == 0;
}

int service_set_boot(int on) {
    if (!on) {
        system("systemctl disable " UNIT_NAME " 2>/dev/null");
        system("systemctl enable getty@tty1.service 2>/dev/null");   // restore console login
        remove_wait_banner();                                        // drop the Wait. banner
        restore_boot();                                              // un-quiet the boot
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
    quiet_boot();                                                 // hide the Linux boot chatter
    remove_wait_banner();                                         // ensure the old banner is gone
    return 0;
}
