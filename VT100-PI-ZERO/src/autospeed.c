// See autospeed.h.
#include "autospeed.h"
#include "video/textmode.h"
#include "settings.h"
#include "config.h"

static int  enabled;
static int  base_pps;          // configured speed: resting rate and floor anchor
static int  out_pps;           // what the renderer is currently running at

// History of (time, received-line count) taken once per control period. The
// host rate is measured across the whole averaging window as a true moving
// average, rather than with a one-pole lag: for the same delay a boxcar rejects
// far more of the burst-to-burst noise, and noise here is speed wobble.
#define RATE_HIST ((AUTO_SPEED_AVG_MAX / AUTO_SPEED_PERIOD_MS) + 2)
static unsigned long hist_lines[RATE_HIST];
static long long     hist_t[RATE_HIST];
static int hist_head, hist_n;

static long long rate_mlps;        // measured host rate, milli-lines per second
static long long ema_ml;           // smoothed backlog, milli-lines
static long long acc_sum;          // backlog samples since the last update
static int       acc_n;

static long long last_run;     // last controller update
static long long last_busy;    // last time output was actually flowing

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int autospeed_enabled(void) { return enabled; }
int autospeed_pps(void)     { return out_pps; }

void autospeed_set(int on, int base, long long now_ms) {
    enabled = on ? 1 : 0;
    base_pps = clampi(base, SCROLL_SPEED_MIN, SCROLL_SPEED_MAX);
    out_pps = base_pps;
    hist_head = 0;
    hist_n = 0;
    rate_mlps = 0;
    ema_ml = 0;
    acc_sum = 0;
    acc_n = 0;
    last_run = now_ms;
    last_busy = now_ms;
    textmode_set_scroll_speed(out_pps);
}

static long avg_window(void) {
    long w = g_settings.speed_avg_ms;
    if (w < AUTO_SPEED_AVG_MIN) w = AUTO_SPEED_AVG_MIN;
    if (w > AUTO_SPEED_AVG_MAX) w = AUTO_SPEED_AVG_MAX;
    return w;
}

void autospeed_update(unsigned long lines_in, int backlog, int fed, long long now_ms) {
    if (!enabled) return;

    if (fed || backlog > 0) last_busy = now_ms;
    acc_sum += backlog;
    ++acc_n;

    long dt = (long)(now_ms - last_run);
    if (dt < AUTO_SPEED_PERIOD_MS) return;
    last_run = now_ms;
    if (dt > 4 * AUTO_SPEED_PERIOD_MS) dt = 4 * AUTO_SPEED_PERIOD_MS;   // after a stall

    long w = avg_window();
    long lag = dt < w ? dt : w;

    // ---- measure the host rate ---------------------------------------------
    // From lines *received*, never lines displayed: the displayed rate is
    // throttled by the scroll itself, so feeding that back would only measure
    // our own output. Rate is taken across the oldest sample still inside the
    // averaging window, so the window length directly sets how much is averaged.
    hist_lines[hist_head] = lines_in;
    hist_t[hist_head] = now_ms;
    hist_head = (hist_head + 1) % RATE_HIST;
    if (hist_n < RATE_HIST) ++hist_n;

    // Walk back from the newest sample to the furthest one still inside the
    // window; samples are in time order, so the first one that falls outside
    // ends the walk.
    int newest = (hist_head - 1 + RATE_HIST) % RATE_HIST;
    int oldest = -1;
    for (int i = 1; i < hist_n; ++i) {
        int idx = (newest - i + RATE_HIST) % RATE_HIST;
        if (now_ms - hist_t[idx] > w) break;
        oldest = idx;
    }
    if (oldest < 0 && hist_n >= 2)                     // window shorter than one
        oldest = (newest - 1 + RATE_HIST) % RATE_HIST; // period: use the last pair
    if (oldest >= 0) {
        long long span = now_ms - hist_t[oldest];
        if (span >= AUTO_SPEED_PERIOD_MS) {
            long long dl = (long long)(lines_in - hist_lines[oldest]);
            rate_mlps = dl * 1000 * 1000 / span;
        }
    }

    long long raw_ml = acc_n ? (acc_sum * 1000 / acc_n) : ema_ml;
    acc_sum = 0;
    acc_n = 0;
    ema_ml += (raw_ml - ema_ml) * lag / w;

    int target;
    if (now_ms - last_busy > AUTO_SPEED_IDLE_MS) {
        rate_mlps = 0;                 // quiet: rest at the configured speed, so
        ema_ml = 0;                    // every burst starts from a known rate
        hist_n = 0;                    // and from a clean measurement window
        target = base_pps;
    } else {
        // ---- feedforward ---------------------------------------------------
        // Convert the measured line rate straight into a pixel rate. This is
        // most of the controller, and it is open-loop: it chases no error, so it
        // cannot ring. Averaging genuinely helps here -- a steadier estimate of
        // the rate is a steadier speed. That is not true of a feedback term,
        // where filter lag costs phase margin and makes the hunting worse.
        int line_h = textmode_line_height();
        if (line_h < 1) line_h = 1;
        long long ff = rate_mlps * line_h / 1000;

        // ---- trim ----------------------------------------------------------
        // A gentle proportional nudge so the backlog cannot drift without bound
        // when the feedforward is slightly off. Proportional only: the plant is
        // already an integrator (a speed error accumulates into backlog), so an
        // integrator here makes a double integrator, which is exactly what made
        // the speed visibly hunt. P on an integrating plant is a stable
        // first-order response, and the standing offset it leaves is harmless --
        // matching the rate is the job, not hitting a particular backlog number.
        long long err_ml = ema_ml - (long long)AUTO_SPEED_TARGET_LINES * 1000;
        long long trim = (long long)AUTO_SPEED_KP * err_ml / 1000;

        // Capped. Between bursts the backlog pins at zero, so the error
        // saturates at -TARGET lines and the trim would otherwise push the speed
        // down at a constant rate until a backlog rebuilt -- a relaxation
        // oscillator, and the visible hunting. Bounded, it can only nudge.
        if (trim >  AUTO_SPEED_TRIM_MAX) trim =  AUTO_SPEED_TRIM_MAX;
        if (trim < -AUTO_SPEED_TRIM_MAX) trim = -AUTO_SPEED_TRIM_MAX;

        target = (int)(ff + trim);
    }

    // Floored at base/N overall so a slow host cannot make the scroll crawl;
    // free to rise, because falling behind is the failure that actually shows.
    int floor_pps = base_pps / AUTO_SPEED_FLOOR_DIV;
    if (floor_pps < SCROLL_SPEED_MIN) floor_pps = SCROLL_SPEED_MIN;
    target = clampi(target, floor_pps, SCROLL_SPEED_MAX);

    // Deadband: once the speed is close enough, stop moving it. Tracking every
    // wiggle of the host's rate is accurate and looks restless; holding one rate
    // until the requirement has really changed is what reads as natural. Only
    // the upper edge is enforced while catching up, so falling behind still gets
    // a prompt response.
    // Evaluated every update, with no "keep going until we arrive" state: the
    // target is itself moving, so chasing it to within a couple of px/s means
    // never stopping. Stepping toward it only while outside the band, and
    // stopping the moment we are inside, is what produces long stretches of one
    // constant speed instead of a correction four times a second.
    int band = out_pps * AUTO_SPEED_DEADBAND_PCT / 100;
    if (target > out_pps - band && target < out_pps + band) return;

    // Slew limit: the scroll is being watched, and ramping is the difference
    // between "it kept up" and a visible surge.
    int delta = target - out_pps;
    if (delta >  AUTO_SPEED_SLEW) delta =  AUTO_SPEED_SLEW;
    if (delta < -AUTO_SPEED_SLEW) delta = -AUTO_SPEED_SLEW;
    if (delta == 0) return;

    out_pps += delta;
    textmode_set_scroll_speed(out_pps);
}
