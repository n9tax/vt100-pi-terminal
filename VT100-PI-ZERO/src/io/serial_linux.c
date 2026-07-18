#include "io/serial_linux.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static int fd = -1;

static speed_t baud_to_speed(int baud) {
    switch (baud) {
        case 300:    return B300;
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B9600;
    }
}

static void apply_termios(int baud) {
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        fprintf(stderr, "serial: tcgetattr failed: %s\n", strerror(errno));
        exit(1);
    }
    cfmakeraw(&tio);
    speed_t sp = baud_to_speed(baud);
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~PARENB;    // 8N1
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        fprintf(stderr, "serial: tcsetattr failed: %s\n", strerror(errno));
        exit(1);
    }
}

void serial_init(const char *path, int baud) {
    fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "serial: open(%s) failed: %s\n", path, strerror(errno));
        exit(1);
    }
    apply_termios(baud);
}

int serial_fd(void) { return fd; }

int serial_getc(void) {
    uint8_t c;
    ssize_t n = read(fd, &c, 1);
    return (n == 1) ? c : -1;
}

void serial_putc(uint8_t c) { serial_write(&c, 1); }

void serial_write(const uint8_t *buf, uint32_t len) {
    // Host link writes are small and infrequent (echoed keystrokes, DSR/DA
    // replies) — a short retry loop is simpler than a write-side ring buffer
    // and never blocks the poll loop for more than a few bytes' worth of time.
    uint32_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n > 0) { off += (uint32_t)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        break;   // real error: drop the rest rather than spin forever
    }
}

void serial_set_baud(int baud) { apply_termios(baud); }

int serial_reconfigure(const char *path, int baud) {
    // Open the new device before dropping the old one, so a bad path from the
    // Setup menu leaves the existing link intact rather than killing serial.
    int nf = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (nf < 0) {
        fprintf(stderr, "serial: reconfigure open(%s) failed: %s\n", path, strerror(errno));
        return -1;
    }
    if (fd >= 0) close(fd);
    fd = nf;
    apply_termios(baud);
    return 0;
}
