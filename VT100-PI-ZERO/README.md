# VT100-PI-ZERO

Hardware VT100 terminal on a Raspberry Pi Zero 2 W, running as a userspace
program on Raspberry Pi OS (Lite). This is the sibling of
[VT100-PI](../VT100-PI), which does the same thing bare-metal on a Pico 2 W;
the Zero build trades the Pico's instant boot for a real GPU (DRM/KMS video),
a real USB host stack (evdev), and a real network stack — headroom the Pico
build was structurally out of.

Current status: **MVP** — DRM video + evdev keyboard + RS232 serial host
link, wired straight through the ported VT100 parser/screen model. No
network (Telnet/SSH) and no on-screen Setup menu yet; see "Build order"
below.

## Hardware

- Raspberry Pi Zero 2 W
- HDMI/DVI monitor on the Zero 2 W's mini-HDMI port
- RS232 hat (Waveshare-style) wired to the GPIO UART, presented as
  `/dev/serial0`
- Any USB keyboard, plugged into the Zero 2 W's USB port

## Build

Native build on the Pi itself (no cross-compile toolchain needed — this is a
small C program, and the Zero 2 W's quad-core Cortex-A53 builds it in
seconds):

```
sudo apt install build-essential cmake libdrm-dev pkg-config
cmake -B build
cmake --build build
```

Output: `build/vt100-pi-zero`.

## Run

Needs raw access to `/dev/dri/card*` and `/dev/input/event*`, so run as
root (or grant those permissions via udev rules):

```
sudo ./build/vt100-pi-zero
```

`/dev/serial0` must be free — disable the serial console getty first
(`raspi-config` -> Interface Options -> Serial Port -> login shell: No,
hardware enabled: Yes), or the two will fight over the UART.

## Boot straight into the terminal (kiosk mode)

```
sudo cp build/vt100-pi-zero /usr/local/bin/vt100-pi-zero
sudo cp systemd/vt100-pi.service /etc/systemd/system/
sudo systemctl disable getty@tty1.service
sudo systemctl enable --now vt100-pi.service
```

This is the closest Linux equivalent to the Pico build's instant-boot-to-
terminal behavior. Actual boot time is still Raspberry Pi OS's normal
~15-20s to get to this point — that's an inherent cost of the Linux
approach, not something this project tries to optimize away.

## Architecture

Same terminal-logic/terminal-I/O split as the Pico build, which is what
makes this port tractable:

```
serial_linux.c --> vt100_feed --> vtparse.c (DEC/ANSI state machine)
                                       |
                                       v
                                 vt100.c (dispatch: SGR, cursor, modes, DSR/DA replies)
                                       |
                                       v
                                 screen.c (cursor/SGR/scroll-region semantics)
                                       |
                                       v
                             textmode.c (tm_cells grid, DRM/KMS glyph blit)
```

- `src/terminal/vtparse.c/.h`, `src/terminal/screen.c/.h` — ported
  **unmodified** from VT100-PI; both are hardware-agnostic already.
- `src/terminal/vt100.c/.h` — ported with one change: replies go through
  `src/io/host_link.c/.h` (`host_write()`) instead of calling the serial
  driver directly, so a later Telnet/SSH transport can be swapped in without
  touching the VT100 dispatcher.
- `src/video/textmode.c` — **new**, not a port. Same `textmode.h` API as the
  Pico build (so `screen.c` didn't need to change), implemented against
  DRM/KMS: single dumb buffer, mode-set to whatever the connected monitor's
  preferred resolution is, glyph blit scaled by the largest integer factor
  that fits an 80x24 grid on that display.
- `src/io/serial_linux.c/.h` — termios wrapper around `/dev/serial0`,
  replacing the Pico's UART-IRQ-plus-ring-buffer (the kernel tty driver
  already buffers RX).
- `src/io/kbd_evdev.c/.h` — raw evdev keyboard reader (no libinput/X11/
  Wayland dependency), replacing the Pico's TinyUSB HID decode on core1.
  Same DECCKM-aware key -> VT100-escape logic, against Linux keycodes.
- `src/video/font_8x16.h` — ported unmodified (glyph bitmap data).

Not ported / no equivalent (see [VT100-PI](../VT100-PI)'s `fault.c`,
`hstx_dvi.c`, `kbd_host.c`'s core1 split, `net.c`'s CYW43 bring-up): a
Linux process doesn't need a fault-capture handler (use `dmesg`/`journalctl`),
a hand-rolled DMA scanout, a second core for USB, or a hand-rolled network
stack.

## Build order

1. **MVP** (done): DRM video + evdev keyboard + RS232 serial, no network,
   no Setup menu.
2. **Networking**: POSIX-socket Telnet transport (`telnet.c` ports
   unmodified from VT100-PI — it's already decoupled from its transport via
   callbacks), then SSH via `posix_openpty` + `fork`/`exec ssh` so the
   system's own OpenSSH client does the protocol work; the PTY master fd
   then feeds `vt100_feed()` exactly like the serial/telnet paths do.
3. **Setup menu + settings persistence**: port `setup.c`'s field-editing
   model and `settings.c`'s struct shape from VT100-PI, file-backed under
   `~/.config/vt100-pi/` instead of a flash sector.
4. **Boot integration** (systemd unit is already checked in; verify a cold
   boot end-to-end once phases 2-3 land).

## Verification

No host-side unit test harness (matches VT100-PI — this is I/O-driven
system code, verified on hardware). Test the host link the same way as the
Pico build: from another machine connected to the RS232 hat's DB9 via a
null-modem cable,

```
sudo bash -c 'stty -F /dev/ttyS0 sane 9600 cs8 -cstopb -parenb clocal -crtscts; TERM=vt100 setsid bash -i <>/dev/ttyS0 >&0 2>&0'
```
