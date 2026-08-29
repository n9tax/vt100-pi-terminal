// See autospeed.h.
#include "autospeed.h"
#include "video/textmode.h"
#include "settings.h"
#include "config.h"

static int  enabled;
static int  base_pps;          // the configured speed: setpoint anchor and idle rest
static int  out_pps;           // what the renderer is currently running at
static long integral;          // accumulated error, in line-milliseconds
static int  last_err;          // for the D term
static long long last_run;     // last controller update
static long long last_busy;    // last time output was actually flowing

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int autospeed_enabled(void) { return enabled; }
int autospeed_pps(void)     { return out_pps; }

void autospeed_set(int on, int base, long long now_ms) {
    enabled = on ? 1 : 0;
    base_pps = clampi(base, SCROLL_SPEED_MIN, SCROLL_SPEED_MAX);
    out_pps = base_pps;
    integral = 0;
    last_err = 0;
    last_run = now_ms;
    last_busy = now_ms;
    textmode_set_scroll_speed(out_pps);   // start from the configured speed
}

void autospeed_update(int backlog, int fed, long long now_ms) {
    if (!enabled) return;

    if (fed || backlog > 0) last_busy = now_ms;

    long dt = (long)(now_ms - last_run);
    if (dt < AUTO_SPEED_PERIOD_MS) return;      // fixed control period
    last_run = now_ms;
    if (dt > 4 * AUTO_SPEED_PERIOD_MS) dt = 4 * AUTO_SPEED_PERIOD_MS;   // after a stall

    int target;
    if (now_ms - last_busy > AUTO_SPEED_IDLE_MS) {
        // Quiet for a while: unwind and ease back to the configured speed, so a
        // burst always starts from a known rate rather than from wherever the
        // last burst happened to leave the controller.
        integral = 0;
        last_err = 0;
        target = base_pps;
    } else {
        int err = backlog - AUTO_SPEED_TARGET_LINES;   // >0 = falling behind

        // Integrate in line-milliseconds, clamped both ways: without the clamp a
        // long burst winds the term up to a speed that then takes just as long
        // to unwind, overshooting far past the setpoint.
        integral += (long)err * dt;
        long i_clamp = (long)AUTO_SPEED_I_CLAMP * 1000;
        if (integral >  i_clamp) integral =  i_clamp;
        if (integral < -i_clamp) integral = -i_clamp;

        long p_term = (long)AUTO_SPEED_KP * err;
        long i_term = (long)AUTO_SPEED_KI * integral / 1000;
        long d_term = (long)AUTO_SPEED_KD * (err - last_err) * 1000 / dt;
        last_err = err;

        target = (int)(base_pps + p_term + i_term + d_term);
    }

    // Asymmetric range: unlimited upward, but only down to base/N (see
    // AUTO_SPEED_FLOOR_DIV) so a slow host cannot make the scroll crawl.
    int floor_pps = base_pps / AUTO_SPEED_FLOOR_DIV;
    if (floor_pps < SCROLL_SPEED_MIN) floor_pps = SCROLL_SPEED_MIN;
    target = clampi(target, floor_pps, SCROLL_SPEED_MAX);

    // Slew limit. The controller may want a large correction, but the scroll is
    // being watched: ramping is the difference between "it kept up" and a
    // visible surge.
    int delta = target - out_pps;
    if (delta >  AUTO_SPEED_SLEW) delta =  AUTO_SPEED_SLEW;
    if (delta < -AUTO_SPEED_SLEW) delta = -AUTO_SPEED_SLEW;
    if (delta == 0) return;

    out_pps += delta;
    textmode_set_scroll_speed(out_pps);
}
