// See update.h. The status file is key=value lines written whole (via rename)
// by update.sh, so a read never sees half of one.
#include "update.h"
#include "config.h"   // UPDATE_CONF_PATH

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define UPDATE_CONF   UPDATE_CONF_PATH
#define UPDATE_STATUS "/var/lib/vt100-pi/update.status"

typedef struct {
    char state[16];    // checking | current | available | updating | failed
    int  behind;
    char installed[16];
    char latest[16];
    char subject[128];
    long checked;      // epoch seconds
    char msg[128];
} status_t;

static int read_status(status_t *s) {
    memset(s, 0, sizeof *s);
    FILE *f = fopen(UPDATE_STATUS, "r");
    if (!f) return -1;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *k = line, *v = eq + 1;
        if      (!strcmp(k, "state"))     snprintf(s->state, sizeof s->state, "%s", v);
        else if (!strcmp(k, "behind"))    s->behind = atoi(v);
        else if (!strcmp(k, "installed")) snprintf(s->installed, sizeof s->installed, "%s", v);
        else if (!strcmp(k, "latest"))    snprintf(s->latest, sizeof s->latest, "%s", v);
        else if (!strcmp(k, "subject"))   snprintf(s->subject, sizeof s->subject, "%s", v);
        else if (!strcmp(k, "checked"))   s->checked = atol(v);
        else if (!strcmp(k, "msg"))       snprintf(s->msg, sizeof s->msg, "%s", v);
    }
    fclose(f);
    return 0;
}

int update_installed(void) { return access(UPDATE_CONF, R_OK) == 0; }

int update_available(void) {
    status_t s;
    return read_status(&s) == 0 && !strcmp(s.state, "available");
}

const char *update_subject(void) {
    static status_t s;
    if (read_status(&s) != 0 || strcmp(s.state, "available") != 0) return "";
    return s.subject;
}

void update_start(void) {
    if (!update_installed()) return;
    // One at a time: a check started mid-install would fetch under the pull
    // and overwrite the install's progress in the status file.
    status_t s;
    // A busy state older than the unit's 30 min timeout is left over from a run
    // that never finished (power cut mid-build), so it doesn't block forever.
    if (read_status(&s) == 0 && (!strcmp(s.state, "checking") || !strcmp(s.state, "updating"))
        && (long)time(NULL) - s.checked < 30 * 60)
        return;
    // --no-block: the check fetches over the network and an install builds for
    // minutes; the terminal keeps running and polls the status file meanwhile.
    // A separate unit, not a child process: installing restarts vt100-pi.service,
    // which would kill anything running inside it.
    if (!strcmp(s.state, "available"))
        system("systemctl start --no-block vt100-pi-update@apply.service");
    else
        system("systemctl start --no-block vt100-pi-update@check.service");
}

// "5m ago", "3h ago", "2d ago".
static void age(long then, char *out, size_t n) {
    long d = (long)time(NULL) - then;
    if (d < 0) d = 0;
    if      (d < 90)        snprintf(out, n, "just now");
    else if (d < 90 * 60)   snprintf(out, n, "%ldm ago", d / 60);
    else if (d < 36 * 3600) snprintf(out, n, "%ldh ago", d / 3600);
    else                    snprintf(out, n, "%ldd ago", d / 86400);
}

void update_describe(char *out, size_t n) {
    if (!update_installed()) { snprintf(out, n, "not set up (run install.sh)"); return; }
    status_t s;
    if (read_status(&s) != 0) { snprintf(out, n, "never checked -- Enter to check"); return; }
    char when[32];
    age(s.checked, when, sizeof when);
    if (!strcmp(s.state, "available"))
        snprintf(out, n, "%d new (%s) -- Enter to install", s.behind, s.latest);
    else if (!strcmp(s.state, "current") && s.msg[0])   // "updated to a1b2c3d"
        snprintf(out, n, "%s (%s)", s.msg, when);
    else if (!strcmp(s.state, "current"))
        snprintf(out, n, "up to date at %s (checked %s)", s.installed, when);
    else if (!strcmp(s.state, "checking") || !strcmp(s.state, "updating"))
        snprintf(out, n, "%s...", s.msg);
    else
        snprintf(out, n, "failed: %s", s.msg);
}
