#include "io/kbd_evdev.h"
#include "terminal/vt100.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <linux/input.h>

static int shift_down, ctrl_down, alt_down, caps_lock;
static int setup_toggle_flag;   // set on Ctrl+F3, consumed by the main loop
static int break_flag;          // set on Pause/Break, consumed by the main loop
static int no_scroll;           // NO SCROLL (Scroll Lock) state: 1 = XOFF sent

// Every open typing node. A composite keyboard contributes more than one.
static struct { int fd; char path[512]; } devs[KBD_MAX_DEVS];
static int ndevs;

// Hotplug watch on /dev/input. Rescanning on a timer meant re-open()ing and
// re-probing every non-keyboard node (mouse, HDMI CEC, power button) once a
// second, in the middle of the render loop -- enough to blow the frame budget
// and put a visible stutter into a smooth scroll at a steady ~1 Hz. inotify
// costs nothing until a device actually appears.
static int hotplug_fd = -1;
static int hotplug_dirty;         // a device changed; a scan is owed
static void hotplug_init(void);   // defined below, next to the other hotplug code

// Small output ring; a human typing (or even pasting) can't outrun this
// between poll() iterations.
#define KBDQ_SIZE 256
static uint8_t q[KBDQ_SIZE];
static int q_head, q_tail;

static void push(uint8_t c) {
    int next = (q_head + 1) % KBDQ_SIZE;
    if (next == q_tail) return;   // full: drop rather than block
    q[q_head] = c;
    q_head = next;
}
static void emit(const char *s) { while (*s) push((uint8_t)*s++); }

int kbd_getc(void) {
    if (q_tail == q_head) return -1;
    uint8_t c = q[q_tail];
    q_tail = (q_tail + 1) % KBDQ_SIZE;
    return c;
}

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// US-layout letter keys. Linux keycodes for them are not contiguous (three
// staggered rows), so keep the explicit table — it serves both the decoder and
// the "is this really a typing keyboard?" capability test below.
static const struct { unsigned code; char ch; } letters[] = {
    {KEY_Q,'q'},{KEY_W,'w'},{KEY_E,'e'},{KEY_R,'r'},{KEY_T,'t'},{KEY_Y,'y'},
    {KEY_U,'u'},{KEY_I,'i'},{KEY_O,'o'},{KEY_P,'p'},{KEY_A,'a'},{KEY_S,'s'},
    {KEY_D,'d'},{KEY_F,'f'},{KEY_G,'g'},{KEY_H,'h'},{KEY_J,'j'},{KEY_K,'k'},
    {KEY_L,'l'},{KEY_Z,'z'},{KEY_X,'x'},{KEY_C,'c'},{KEY_V,'v'},{KEY_B,'b'},
    {KEY_N,'n'},{KEY_M,'m'},
};
#define NLETTERS ((int)(sizeof letters / sizeof letters[0]))

// ---- device discovery ------------------------------------------------------
static int has_bit(const unsigned long *bits, unsigned n) {
    return (bits[n / (8 * sizeof(long))] >> (n % (8 * sizeof(long)))) & 1;
}

// A node worth reading advertises the whole alphabet plus Enter and Space.
// That is deliberately strict: a composite keyboard's mouse / System Control /
// Consumer Control interfaces also carry EV_KEY (and udev still symlinks some
// of them as *-event-kbd), but none of them declares a full typing layout, so
// they are filtered out here. We do NOT reject EV_REL devices — a keyboard with
// an integrated trackpoint is still a keyboard, and since we now read every
// matching node rather than betting on one, a false positive would only cost an
// idle fd instead of losing all typing.
static int is_typing_keyboard(int cand_fd) {
    unsigned long evbits[EV_MAX / (8 * sizeof(long)) + 1];
    unsigned long keybits[KEY_MAX / (8 * sizeof(long)) + 1];
    memset(evbits, 0, sizeof evbits);
    memset(keybits, 0, sizeof keybits);
    if (ioctl(cand_fd, EVIOCGBIT(0, sizeof evbits), evbits) < 0) return 0;
    if (!has_bit(evbits, EV_KEY)) return 0;
    if (ioctl(cand_fd, EVIOCGBIT(EV_KEY, sizeof keybits), keybits) < 0) return 0;
    for (int i = 0; i < NLETTERS; ++i)
        if (!has_bit(keybits, letters[i].code)) return 0;
    return has_bit(keybits, KEY_ENTER) && has_bit(keybits, KEY_SPACE);
}

// Grab the device exclusively. Without this, keystrokes ALSO reach the kernel
// console on tty1 -- which wakes fbcon and makes it repaint the text console
// (the leftover boot "Wait" banner) right over our KMS output, and lets stray
// Ctrl+Alt+Fn switch VTs. The grab is released automatically when fd is closed.
static void grab_device(int the_fd, const char *path) {
    if (ioctl(the_fd, EVIOCGRAB, (void *)1) < 0)
        fprintf(stderr, "kbd: EVIOCGRAB on %s failed: %s (keys may leak to the console)\n",
                path, strerror(errno));
}

static void drop_device(int i) {
    fprintf(stderr, "kbd: released %s\n", devs[i].path);
    close(devs[i].fd);
    for (int j = i; j < ndevs - 1; ++j) devs[j] = devs[j + 1];
    --ndevs;
}

static int already_open(const char *path) {
    for (int i = 0; i < ndevs; ++i)
        if (strcmp(devs[i].path, path) == 0) return 1;
    return 0;
}

// Scan /dev/input/event* and adopt every typing node not already open. Nodes
// that have gone away (unplug / re-enumeration) are reaped first, so a device
// number can be reused without us clinging to a dead fd for it.
static void scan_devices(void) {
    for (int i = 0; i < ndevs; ) {
        struct input_id id;
        if (ioctl(devs[i].fd, EVIOCGID, &id) < 0) drop_device(i);   // gone
        else ++i;
    }

    DIR *d = opendir("/dev/input");
    if (!d) {
        fprintf(stderr, "kbd: opendir(/dev/input) failed: %s\n", strerror(errno));
        return;
    }

    struct dirent *ent;
    while (ndevs < KBD_MAX_DEVS && (ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;

        char path[512];
        snprintf(path, sizeof path, "/dev/input/%s", ent->d_name);
        if (already_open(path)) continue;

        int cfd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (cfd < 0) continue;
        if (!is_typing_keyboard(cfd)) { close(cfd); continue; }

        char name[256] = "?";
        ioctl(cfd, EVIOCGNAME(sizeof name), name);
        grab_device(cfd, path);
        devs[ndevs].fd = cfd;
        snprintf(devs[ndevs].path, sizeof devs[ndevs].path, "%s", path);
        ++ndevs;
        fprintf(stderr, "kbd: using %s (%s)\n", path, name);
    }
    closedir(d);
}

void kbd_init(void) {
    hotplug_init();
    scan_devices();
    if (ndevs == 0)
        fprintf(stderr, "kbd: no keyboard found under /dev/input — "
                        "the terminal will run without one and keep looking\n");
}

void kbd_rescan(void) { scan_devices(); }

int kbd_hotplug_fd(void) { return hotplug_fd; }

static void hotplug_init(void) {
    hotplug_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (hotplug_fd < 0) {
        fprintf(stderr, "kbd: inotify_init1 failed: %s (falling back to periodic rescans)\n",
                strerror(errno));
        return;
    }
    // IN_ATTRIB matters as much as IN_CREATE: udev creates the node and then
    // fixes its permissions, so the node can exist a moment before it is usable.
    if (inotify_add_watch(hotplug_fd, "/dev/input",
                          IN_CREATE | IN_ATTRIB | IN_MOVED_TO | IN_DELETE) < 0) {
        fprintf(stderr, "kbd: watch on /dev/input failed: %s (falling back to periodic rescans)\n",
                strerror(errno));
        close(hotplug_fd);
        hotplug_fd = -1;
    }
}

// Always drain when the fd is readable, even if the scan has to wait: an
// undrained inotify fd stays readable forever, which would make poll() return
// immediately every iteration and spin the main loop at 100% CPU.
void kbd_handle_hotplug(void) {
    if (hotplug_fd < 0) return;
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    for (;;) {
        ssize_t n = read(hotplug_fd, buf, sizeof buf);
        if (n > 0) { hotplug_dirty = 1; continue; }
        break;                    // drained (EAGAIN), or an error we retry next poll
    }
}

// Do the owed scan. Separate from the drain so the caller can keep it off the
// critical path of a slide; a burst of events (udev creating several nodes for
// one keyboard) collapses into a single scan here.
void kbd_hotplug_settle(void) {
    if (!hotplug_dirty) return;
    hotplug_dirty = 0;
    scan_devices();
}

int kbd_fd_count(void) { return ndevs; }
int kbd_fd_at(int i) { return (i >= 0 && i < ndevs) ? devs[i].fd : -1; }

int kbd_take_break(void) { int b = break_flag; break_flag = 0; return b; }

// VT100 PF1-PF4: SS3 P..S in ANSI mode, ESC P..S in VT52 mode.
static void emit_pf(char k) {
    push(0x1b);
    if (!vt100_vt52_mode()) push('O');
    push((uint8_t)k);
}

// A VT100 keypad key. Numeric mode sends what is printed on the key; keypad
// application mode sends SS3 + a letter (ESC ? + letter in VT52 mode).
static void emit_keypad(char numeric, char app) {
    if (!vt100_keypad_app()) {
        push((uint8_t)numeric);
        if (numeric == '\r' && vt100_newline_mode()) push('\n');
        return;
    }
    push(0x1b);
    push(vt100_vt52_mode() ? '?' : 'O');
    push((uint8_t)app);
}

// Arrow keys: ESC A..D in VT52 mode, else per DECCKM.
static void emit_arrow(char k) {
    push(0x1b);
    if (!vt100_vt52_mode()) push(vt100_cursor_keys_app() ? 'O' : '[');
    push((uint8_t)k);
}

int kbd_take_setup_toggle(void) {
    int t = setup_toggle_flag;
    setup_toggle_flag = 0;
    return t;
}

// ---- Linux keycode -> VT100 byte sequence (US layout) ---------------------
static void emit_key(unsigned code) {
    // Ctrl+F3 opens/closes the local Setup screen; never sent to the host.
    if (ctrl_down && code == KEY_F3) { setup_toggle_flag = 1; return; }

    switch (code) {
        case KEY_A: case KEY_B: case KEY_C: case KEY_D: case KEY_E: case KEY_F:
        case KEY_G: case KEY_H: case KEY_I: case KEY_J: case KEY_K: case KEY_L:
        case KEY_M: case KEY_N: case KEY_O: case KEY_P: case KEY_Q: case KEY_R:
        case KEY_S: case KEY_T: case KEY_U: case KEY_V: case KEY_W: case KEY_X:
        case KEY_Y: case KEY_Z: {
            char c = 'a';
            for (int i = 0; i < NLETTERS; ++i)
                if (letters[i].code == code) { c = letters[i].ch; break; }
            if (ctrl_down) { push((uint8_t)(c & 0x1f)); return; }
            if (shift_down ^ caps_lock) c -= 32;
            if (alt_down) push(0x1b);
            push((uint8_t)c);
            return;
        }

        case KEY_1: case KEY_2: case KEY_3: case KEY_4: case KEY_5:
        case KEY_6: case KEY_7: case KEY_8: case KEY_9: case KEY_0: {
            static const unsigned codes[] = { KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9,KEY_0 };
            static const char un[] = "1234567890";
            static const char sh[] = "!@#$%^&*()";
            for (unsigned i = 0; i < 10; ++i) if (codes[i] == code) {
                if (alt_down) push(0x1b);
                push((uint8_t)(shift_down ? sh[i] : un[i]));
                return;
            }
            return;
        }

        case KEY_ENTER: push('\r'); if (vt100_newline_mode()) push('\n'); return;
        case KEY_ESC:       push(0x1b); return;
        case KEY_BACKSPACE: push(0x7f); return;
        case KEY_TAB:        push('\t'); return;
        case KEY_SPACE:       push(' ');  return;
        case KEY_MINUS:      push(shift_down ? '_' : '-');  return;
        case KEY_EQUAL:      push(shift_down ? '+' : '=');  return;
        case KEY_LEFTBRACE:  push(shift_down ? '{' : '[');  return;
        case KEY_RIGHTBRACE: push(shift_down ? '}' : ']');  return;
        case KEY_BACKSLASH:  push(shift_down ? '|' : '\\'); return;
        case KEY_SEMICOLON:  push(shift_down ? ':' : ';');  return;
        case KEY_APOSTROPHE: push(shift_down ? '"' : '\''); return;
        case KEY_GRAVE:      push(shift_down ? '~' : '`');  return;
        case KEY_COMMA:      push(shift_down ? '<' : ',');  return;
        case KEY_DOT:        push(shift_down ? '>' : '.');  return;
        case KEY_SLASH:      push(shift_down ? '?' : '/');  return;
        case KEY_CAPSLOCK:   caps_lock = !caps_lock; return;

        // Arrows honour DECCKM (application cursor keys).
        case KEY_RIGHT: emit_arrow('C'); return;
        case KEY_LEFT:  emit_arrow('D'); return;
        case KEY_DOWN:  emit_arrow('B'); return;
        case KEY_UP:    emit_arrow('A'); return;

        // The VT100 keypad on a PC one. Its top row (NumLock / *) is PF1-PF3
        // (F1-F4 are PF1-PF4 too); '-' stays '-', and '+' is the VT100's ','
        // key, which sits in the same place. NumLock state is ignored.
        case KEY_NUMLOCK:    emit_pf('P'); return;
        case KEY_KPSLASH:    emit_pf('Q'); return;
        case KEY_KPASTERISK: emit_pf('R'); return;
        case KEY_KP0: emit_keypad('0', 'p'); return;
        case KEY_KP1: emit_keypad('1', 'q'); return;
        case KEY_KP2: emit_keypad('2', 'r'); return;
        case KEY_KP3: emit_keypad('3', 's'); return;
        case KEY_KP4: emit_keypad('4', 't'); return;
        case KEY_KP5: emit_keypad('5', 'u'); return;
        case KEY_KP6: emit_keypad('6', 'v'); return;
        case KEY_KP7: emit_keypad('7', 'w'); return;
        case KEY_KP8: emit_keypad('8', 'x'); return;
        case KEY_KP9: emit_keypad('9', 'y'); return;
        case KEY_KPMINUS: emit_keypad('-', 'm'); return;
        case KEY_KPPLUS:  emit_keypad(',', 'l'); return;
        case KEY_KPDOT:   emit_keypad('.', 'n'); return;
        case KEY_KPENTER: emit_keypad('\r', 'M'); return;

        // NO SCROLL: alternately XOFF and XON, as the VT100 key does.
        case KEY_SCROLLLOCK: no_scroll ^= 1; push(no_scroll ? 0x13 : 0x11); return;
        // BREAK: a line break on the serial port (the main loop sends it).
        case KEY_PAUSE: break_flag = 1; return;

        case KEY_INSERT:   emit("\x1b[2~"); return;
        case KEY_HOME:     emit("\x1b[H");  return;
        case KEY_PAGEUP:   emit("\x1b[5~"); return;
        case KEY_DELETE:   emit("\x1b[3~"); return;
        case KEY_END:      emit("\x1b[F");  return;
        case KEY_PAGEDOWN: emit("\x1b[6~"); return;

        case KEY_F1:  emit_pf('P'); return;
        case KEY_F2:  emit_pf('Q'); return;
        case KEY_F3:  emit_pf('R'); return;
        case KEY_F4:  emit_pf('S'); return;
        case KEY_F5:  emit("\x1b[15~"); return;
        case KEY_F6:  emit("\x1b[17~"); return;
        case KEY_F7:  emit("\x1b[18~"); return;
        case KEY_F8:  emit("\x1b[19~"); return;
        case KEY_F9:  emit("\x1b[20~"); return;
        case KEY_F10: emit("\x1b[21~"); return;
        case KEY_F11: emit("\x1b[23~"); return;
        case KEY_F12: emit("\x1b[24~"); return;
        default: return;
    }
}

// A keyboard that mirrors a keypress onto two of its own nodes (a boot-protocol
// interface plus an NKRO one, say) would otherwise type every character twice
// now that we read them all. Suppress a press of the same keycode arriving from
// a *different* node within a few tens of ms; repeats from the node that owns
// the key still get through, since the owner is re-stamped on every emit.
#define DUP_WINDOW_MS 30
static long long key_last_ms[KEY_CNT];
static int       key_last_fd[KEY_CNT];   // 0 = no owner yet (never one of ours)

static int mirrored_elsewhere(int the_fd, unsigned code) {
    if (code >= KEY_CNT) return 0;
    long long t = now_ms();
    if (key_last_fd[code] && key_last_fd[code] != the_fd &&
        t - key_last_ms[code] < DUP_WINDOW_MS)
        return 1;
    key_last_fd[code] = the_fd;
    key_last_ms[code] = t;
    return 0;
}

static void handle_event(int the_fd, const struct input_event *e) {
    if (e->type != EV_KEY) return;
    unsigned code = e->code;
    int value = e->value;   // 0 = up, 1 = down, 2 = autorepeat

    switch (code) {
        // Modifier state is level-triggered, so a mirrored node setting it
        // again is harmless — no de-duplication needed here.
        case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: shift_down = (value != 0); return;
        case KEY_LEFTCTRL:  case KEY_RIGHTCTRL:  ctrl_down  = (value != 0); return;
        case KEY_LEFTALT:   case KEY_RIGHTALT:   alt_down   = (value != 0); return;
        default: break;
    }
    if (value != 1 && value != 2) return;             // press or autorepeat only
    if (mirrored_elsewhere(the_fd, code)) return;
    emit_key(code);
}

void kbd_poll(void) {
    struct input_event ev[64];
    for (int i = 0; i < ndevs; ) {
        int alive = 1;
        for (;;) {
            ssize_t n = read(devs[i].fd, ev, sizeof ev);
            if (n < 0) {
                // ENODEV etc. means the device is gone; EAGAIN just means drained.
                if (errno != EAGAIN && errno != EINTR) alive = 0;
                break;
            }
            if (n == 0) { alive = 0; break; }
            int count = (int)(n / (ssize_t)sizeof(struct input_event));
            for (int j = 0; j < count; ++j) handle_event(devs[i].fd, &ev[j]);
        }
        if (alive) ++i;
        else {
            // Modifiers could be stuck "held" on a device that vanished.
            shift_down = ctrl_down = alt_down = 0;
            drop_device(i);
        }
    }
}
