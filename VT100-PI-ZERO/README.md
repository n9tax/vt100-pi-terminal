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
sudo apt install build-essential cmake libdrm-dev libfreetype-dev pkg-config
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

## Configuration

On first run the program writes a documented settings file to
`~/.config/vt100-pi/vt100.conf` (compile-time defaults in `src/config.h`).
Edit it and restart to change the serial device/baud, colour/phosphor theme,
cursor style, local echo, or the font — no rebuild:

```
serial_dev = /dev/serial0
baud       = 9600        # 300 1200 2400 4800 9600 19200 38400 57600 115200
theme      = amber       # color amber green white blue red yellow c64 vic20 c128 borland custom
cursor     = block       # block | underline
local_echo = off         # on | off
smooth_scroll = on       # on | off  (slide text up instead of jumping)
scroll_speed  = 600      # pixels/second (a line is ~32px; bursts catch up)
fg_color   = #FFFFFF     # custom-theme foreground (used when theme = custom)
bg_color   = #000000     # custom-theme background
font       =             # empty = DejaVu (default), a bundled name, or an absolute .ttf path
ssh_host    =            # host or user@host: connect via the system ssh on boot
telnet_host =            # hostname/IP: connect over Telnet on boot
telnet_port = 23
```

Set `ssh_host` (`user@host`) or `telnet_host` and the terminal connects to it on
boot instead of using serial (`ssh_host` wins if both are set), routing keystrokes
to the connection and falling back to serial if it fails or drops. SSH runs the
Pi's own `ssh` client over a PTY, so host-key and password prompts appear in the
terminal as normal. Leave both empty for the serial host link.

Smooth scroll slides the screen up a few pixels per frame instead of jumping a
whole line. A single new line glides in over a few frames; when lines pile up it
slides faster to catch up (up to a row per frame), and a burst more than ~8 lines
behind falls back to an instant jump. `scroll_speed` sets the base rate; higher
is snappier, lower is more languid.

Themes: `color` is the full 16-colour ANSI palette; `amber`/`green`/`white`/
`blue`/`red`/`yellow` are monochrome phosphor looks; `c64`, `vic20`, `c128`, and
`borland` are retro machine palettes (text colour on a screen background); and
`custom` uses `fg_color`/`bg_color`. Retro/custom themes are two-colour — every
ANSI colour maps to a shade between background and foreground — so colourful
TUIs render monochrome under them (as with the phosphor themes). There's no
separate border colour: the grid fills the whole screen.

Eight fonts are bundled in `assets/` so they work on a bare Pi OS Lite: DejaVu
Sans Mono (default), Liberation Mono, Noto Sans Mono, Hack, JetBrains Mono, Fira
Code, Source Code Pro, and VT323 (a retro DEC-style face). The Setup menu's Font
field cycles through them, or point `font` at any `.ttf` on disk. Licenses and
sources are in [assets/FONTS.md](assets/FONTS.md).

Unknown keys are ignored (with a warning on stderr); a missing file is
recreated with defaults.

Or edit it on screen: **Ctrl+F3** opens the Setup menu. Up/Down move between
fields, Left/Right change a value (baud, theme, cursor, echo), typing edits the
text fields (serial device, font) with Backspace to delete, **Enter** saves and
applies, **Ctrl+F3** again cancels. Theme/cursor/echo apply instantly; a changed
serial device/baud reopens the link and a changed font rebuilds the glyph atlas
live — no restart.

## Boot straight into the terminal (kiosk mode)

```
sudo cp build/vt100-pi-zero /usr/local/bin/vt100-pi-zero
sudo mkdir -p /usr/local/share/vt100-pi/fonts
sudo cp assets/*.ttf /usr/local/share/vt100-pi/fonts/   # so bundled fonts resolve off-tree
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
  preferred resolution is. The 80x24 grid is stretched to fill the whole
  display (each cell's pixel rect derived from its row/col), and each cell
  alpha-blends a glyph from the atlas below.
- `src/video/glyphs.c` — **new**. Renders the 256 CP437 code points into an
  anti-aliased coverage atlas at the display's cell size via FreeType, from a
  bundled TrueType font (`assets/DejaVuSansMono.ttf`; override with
  `FONT_TTF_PATH`). The block/shade/box-drawing region (0xB0-0xDF) is drawn
  procedurally instead, edge-to-edge, so lines tile seamlessly across cells —
  a normal mono font's line glyphs don't, once squeezed into 80-column cells.
- `src/io/serial_linux.c/.h` — termios wrapper around `/dev/serial0`,
  replacing the Pico's UART-IRQ-plus-ring-buffer (the kernel tty driver
  already buffers RX).
- `src/io/kbd_evdev.c/.h` — raw evdev keyboard reader (no libinput/X11/
  Wayland dependency), replacing the Pico's TinyUSB HID decode on core1.
  Same DECCKM-aware key -> VT100-escape logic, against Linux keycodes.
- `src/video/font_8x16.h` — ported unmodified (VGA bitmap glyph data); no
  longer the render path (FreeType is), kept as a fallback / reference.

Not ported / no equivalent (see [VT100-PI](../VT100-PI)'s `fault.c`,
`hstx_dvi.c`, `kbd_host.c`'s core1 split, `net.c`'s CYW43 bring-up): a
Linux process doesn't need a fault-capture handler (use `dmesg`/`journalctl`),
a hand-rolled DMA scanout, a second core for USB, or a hand-rolled network
stack.

## Build order

1. **MVP** (done): DRM video + evdev keyboard + RS232 serial, no network,
   no Setup menu.
2. **Networking**: **done** — `src/net/telnet.c` (ported IAC filter) +
   `src/net/netlink.c`, a two-mode transport: a TCP Telnet client, or the
   system `ssh` client run over a PTY (`posix_openpt` + `fork`/`exec`), its
   master fd feeding the same ring as serial. Connects on boot from
   `ssh_host`/`telnet_host`, or set them in the Setup menu (Ctrl+F3) to connect
   live; the Setup screen also shows the Pi's own IP so you can ssh *into* it.
3. **Setup menu + settings persistence**: **done** — `src/settings.c` loads
   `~/.config/vt100-pi/vt100.conf`, and `src/setup.c` is the on-screen Ctrl+F3
   menu that edits, applies, and re-saves it live (see Configuration).
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
