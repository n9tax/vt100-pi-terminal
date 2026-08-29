// Flow monitor for the smooth-scroll pipeline: is the host outrunning the
// scroll, or is the scroll starved?
//
// Host bytes land in main.c's ring and are metered into the terminal at the
// paced scroll rate. Two things can go wrong with that, and they pull the
// pacing settings in opposite directions:
//
//   overrun  -- output arrives faster than the paced feed can display it. The
//               ring grows past scroll_dump and the screen dump-resets, which
//               is the visible "it gave up and jumped" behaviour. Fix by
//               raising scroll_speed, or scroll_dump to ride out longer bursts.
//   underrun -- the ring runs dry while a slide is still in flight, so the
//               animation finishes with nothing behind it. Normal at the end of
//               output; frequent mid-stream it means data arrives in bursts too
//               small to keep the slide fed. Fix by raising scroll_depth so
//               more is buffered before the slide starts, or lowering speed.
//
// Setup (Ctrl+F3) shows the verdict live so the pacing can be tuned by looking
// at it rather than by guessing.
#ifndef FLOW_H
#define FLOW_H

// Sample the pipeline once per main-loop iteration, from the terminal branch
// only (not while Setup owns the screen -- the ring isn't being drained then,
// so it would read as a growing overrun that isn't real).
//   backlog     lines currently buffered in the ring
//   scroll_busy 1 while a slide is in flight
//   fed         1 if this iteration fed any bytes into the terminal
void flow_sample(int backlog, int scroll_busy, int fed);

// The ring exceeded the dump threshold and was flushed to the screen at once.
void flow_note_overrun(void);

// One-line verdict plus counters for the Setup header, e.g.
// "keeping up (peak 12, 0 over / 3 under)". Valid until the next flow_sample().
const char *flow_status(void);

// Forget the counters (Setup offers this so a tuning change can be judged on
// its own, rather than against the whole session's history).
void flow_reset(void);

#endif // FLOW_H
