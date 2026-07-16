#include "io/kbd_evdev.h"
#include "terminal/vt100.h"

#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>

static int fd = -1;
static int shift_down, ctrl_down, alt_down, caps_lock;

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

// ---- device discovery ------------------------------------------------------
static int looks_like_keyboard(int cand_fd) {
    unsigned long bits[KEY_MAX / (8 * sizeof(long)) + 1];
    memset(bits, 0, sizeof bits);
    if (ioctl(cand_fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) < 0) return 0;
    return (bits[KEY_A / (8 * sizeof(long))] >> (KEY_A % (8 * sizeof(long)))) & 1;
}

void kbd_init(void) {
    DIR *d = opendir("/dev/input");
    if (!d) { fprintf(stderr, "kbd: opendir(/dev/input) failed\n"); exit(1); }

    struct dirent *ent;
    char path[300];
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;
        snprintf(path, sizeof path, "/dev/input/%s", ent->d_name);
        int cfd = open(path, O_RDONLY | O_NONBLOCK);
        if (cfd < 0) continue;
        if (looks_like_keyboard(cfd)) { fd = cfd; break; }
        close(cfd);
    }
    closedir(d);

    if (fd < 0) {
        fprintf(stderr, "kbd: no keyboard found under /dev/input\n");
        exit(1);
    }
}

int kbd_fd(void) { return fd; }

// ---- Linux keycode -> VT100 byte sequence (US layout) ---------------------
static void emit_key(unsigned code) {
    // Ctrl+F3 opens/closes the local Setup screen; never sent to the host.
    // (Setup itself lands in a later phase; for now this is simply absorbed.)
    if (ctrl_down && code == KEY_F3) return;

    switch (code) {
        case KEY_A: case KEY_B: case KEY_C: case KEY_D: case KEY_E: case KEY_F:
        case KEY_G: case KEY_H: case KEY_I: case KEY_J: case KEY_K: case KEY_L:
        case KEY_M: case KEY_N: case KEY_O: case KEY_P: case KEY_Q: case KEY_R:
        case KEY_S: case KEY_T: case KEY_U: case KEY_V: case KEY_W: case KEY_X:
        case KEY_Y: case KEY_Z: {
            static const struct { unsigned code; char ch; } letters[] = {
                {KEY_A,'a'},{KEY_B,'b'},{KEY_C,'c'},{KEY_D,'d'},{KEY_E,'e'},{KEY_F,'f'},
                {KEY_G,'g'},{KEY_H,'h'},{KEY_I,'i'},{KEY_J,'j'},{KEY_K,'k'},{KEY_L,'l'},
                {KEY_M,'m'},{KEY_N,'n'},{KEY_O,'o'},{KEY_P,'p'},{KEY_Q,'q'},{KEY_R,'r'},
                {KEY_S,'s'},{KEY_T,'t'},{KEY_U,'u'},{KEY_V,'v'},{KEY_W,'w'},{KEY_X,'x'},
                {KEY_Y,'y'},{KEY_Z,'z'},
            };
            char c = 'a';
            for (unsigned i = 0; i < sizeof(letters) / sizeof(letters[0]); ++i)
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
        case KEY_RIGHT: emit(vt100_cursor_keys_app() ? "\x1bOC" : "\x1b[C"); return;
        case KEY_LEFT:  emit(vt100_cursor_keys_app() ? "\x1bOD" : "\x1b[D"); return;
        case KEY_DOWN:  emit(vt100_cursor_keys_app() ? "\x1bOB" : "\x1b[B"); return;
        case KEY_UP:    emit(vt100_cursor_keys_app() ? "\x1bOA" : "\x1b[A"); return;

        case KEY_INSERT:   emit("\x1b[2~"); return;
        case KEY_HOME:     emit("\x1b[H");  return;
        case KEY_PAGEUP:   emit("\x1b[5~"); return;
        case KEY_DELETE:   emit("\x1b[3~"); return;
        case KEY_END:      emit("\x1b[F");  return;
        case KEY_PAGEDOWN: emit("\x1b[6~"); return;

        case KEY_F1:  emit("\x1bOP");   return;
        case KEY_F2:  emit("\x1bOQ");   return;
        case KEY_F3:  emit("\x1bOR");   return;
        case KEY_F4:  emit("\x1bOS");   return;
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

void kbd_poll(void) {
    struct input_event ev[64];
    for (;;) {
        ssize_t n = read(fd, ev, sizeof ev);
        if (n <= 0) return;
        int count = (int)(n / (ssize_t)sizeof(struct input_event));
        for (int i = 0; i < count; ++i) {
            if (ev[i].type != EV_KEY) continue;
            unsigned code = ev[i].code;
            int value = ev[i].value;   // 0 = up, 1 = down, 2 = autorepeat

            switch (code) {
                case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: shift_down = (value != 0); continue;
                case KEY_LEFTCTRL:  case KEY_RIGHTCTRL:  ctrl_down  = (value != 0); continue;
                case KEY_LEFTALT:   case KEY_RIGHTALT:   alt_down   = (value != 0); continue;
                default: break;
            }
            if (value == 1 || value == 2) emit_key(code);   // press or autorepeat
        }
    }
}
