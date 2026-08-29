// See flow.h.
#include "flow.h"
#include "settings.h"
#include "config.h"

#include <stdio.h>

static int peak_backlog;
static int overruns;
static int underruns;
static int backlog_now;

// Verdict hysteresis. A single sample is meaningless -- the ring is empty
// between every keystroke -- so the live verdict is based on how the last
// SAMPLE_WIN samples went, not on the instant.
#define SAMPLE_WIN 64
static int win_n;          // samples taken in the current window
static int win_pressed;    // samples where the ring held a real backlog
static int win_starved;    // samples where a slide ran with nothing behind it
static int win_active;     // samples where anything was happening at all
enum { V_IDLE, V_OK, V_STARVED, V_BEHIND };
static int verdict = V_IDLE;
static char line[128];

// An underrun only counts while a slide is actually in flight: an empty ring
// with no slide is just an idle terminal, not a starved animation.
void flow_sample(int backlog, int scroll_busy, int fed) {
    backlog_now = backlog;
    if (backlog > peak_backlog) peak_backlog = backlog;

    if (win_n >= SAMPLE_WIN) {                 // roll the window
        // Order matters: being behind is the more serious condition, and idle
        // means genuinely nothing happening -- a terminal that is feeding
        // steadily and comfortably must read as "keeping up", not as idle.
        verdict = win_pressed > SAMPLE_WIN / 4 ? V_BEHIND
                : win_starved > SAMPLE_WIN / 2 ? V_STARVED
                : win_active                   ? V_OK
                                               : V_IDLE;
        win_n = win_pressed = win_starved = win_active = 0;
    }
    ++win_n;
    if (fed || scroll_busy) ++win_active;

    // "Pressed" = the ring is more than half way to the dump threshold, i.e.
    // the feed is measurably behind rather than just briefly non-empty.
    if (backlog > g_settings.scroll_dump / 2) ++win_pressed;

    if (scroll_busy && backlog == 0 && !fed) {
        ++win_starved;
        ++underruns;
    }
}

void flow_note_overrun(void) { ++overruns; }

void flow_reset(void) {
    peak_backlog = overruns = underruns = 0;
    win_n = win_pressed = win_starved = win_active = 0;
    verdict = V_IDLE;
}

const char *flow_status(void) {
    const char *v = verdict == V_BEHIND  ? "falling behind"
                  : verdict == V_STARVED ? "starved"
                  : verdict == V_OK      ? "keeping up"
                                         : "idle";
    snprintf(line, sizeof line, "%s  %d/%d peak %d  %d over / %d under",
             v, backlog_now, g_settings.scroll_dump, peak_backlog, overruns, underruns);
    return line;
}
