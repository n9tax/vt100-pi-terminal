// Closed-loop scroll pacing: a PID controller that trims the scroll speed to
// hold the backlog at a small setpoint.
//
// Holding a constant backlog means consuming lines exactly as fast as the host
// produces them, so in practice this rate-matches the scroll to the host: a
// burst speeds it up, quiet output slows it down, and the buffer neither runs
// away (overrun, screen dump-resets) nor runs dry mid-slide (underrun). The
// configured scroll_speed becomes the starting point and the resting speed
// rather than a fixed rate.
//
// Off by default -- it is a different feel, not a fix, and a fixed speed is the
// calmer choice for reading. Watch the Setup buffer line to see it working.
#ifndef AUTOSPEED_H
#define AUTOSPEED_H

// Enable/disable and (re)anchor to the configured base speed. Call at startup
// and whenever Setup saves; it resets the integrator, so a settings change never
// inherits the previous run's wind-up.
void autospeed_set(int on, int base_pps, long long now_ms);

// Feed one sample from the main loop's terminal branch. Self-timed: it only
// recomputes every AUTO_SPEED_PERIOD_MS and applies its own slew limit, so
// calling it every iteration is correct and cheap. A no-op when disabled.
//   lines_in  monotonic count of lines *received* from the host (not displayed)
//   backlog   lines currently buffered
//   fed       1 if anything reached the terminal this iteration
void autospeed_update(unsigned long lines_in, int backlog, int fed, long long now_ms);

int autospeed_enabled(void);
int autospeed_pps(void);      // the speed currently in effect, for Setup's display

#endif // AUTOSPEED_H
