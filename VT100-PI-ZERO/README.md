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
- HDMI/DVI monitor on the Zero 2 W's mini-HDMI port. This build uses a
  [VSDISPLAY 10.4" 1024x768 IPS panel](https://www.amazon.com/dp/B07WCH53TK)
  with its HDMI controller board — 4:3 like the original VT100, and 768/24
  divides to exactly 32 pixels per text row. Any HDMI display works though:
  the mode is negotiated at runtime via DRM/KMS, not fixed at compile time.
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

`/dev/serial0` must exist *and* be free. On a fresh Raspberry Pi OS the UART is
off, so the device is not there at all — and if the serial console getty is
running, it fights the terminal for the port. Both are the same setting:
`raspi-config` -> Interface Options -> Serial Port -> login shell: **No**,
hardware enabled: **Yes**, then reboot. Add `dtoverlay=disable-bt` to
`/boot/firmware/config.txt` as well, so `/dev/serial0` is the PL011 rather than
the mini-UART, whose baud drifts with the core clock.

A missing port is not fatal: the terminal starts without a serial host link, logs
why, and Setup shows the device as `(not open)`. Fix the UART and reopen it from
Setup (Ctrl+F3 -> Enter on Serial device), or use ssh / telnet / local shell
instead — no restart needed either way.

## Configuration

On first run the program writes a documented settings file to
`~/.config/vt100-pi/vt100.conf` (compile-time defaults in `src/config.h`).
Edit it and restart to change the serial device/baud, colour/phosphor theme,
cursor style, local echo, or the font — no rebuild:

```
serial_dev = /dev/serial0
baud       = 9600        # 300 1200 2400 4800 9600 19200 38400 57600 115200
format     = 8N1         # 8N1 7E1 7O1 7M1 7S1 -- match the host's frame format
xon_xoff   = on          # send XOFF/XON when the input buffer fills
theme      = amber       # color amber green white blue red yellow c64 vic20 c128 borland custom
cursor     = block       # block | underline
local_echo = off         # on | off
smooth_scroll = on       # on | off  (slide text up instead of jumping)
scroll_speed  = 600      # pixels/second (a line is ~32px; bursts catch up)
scroll_depth  = 2        # lines of slide in flight (pacing depth)
scroll_dump   = 80       # backlog in lines past which the screen dump-resets
auto_speed    = off      # closed loop: trim the speed to match the host's rate
speed_avg_ms  = 4000     # how long auto_speed averages the host rate over
fg_color   = #FFFFFF     # custom-theme foreground (used when theme = custom)
bg_color   = #000000     # custom-theme background
font       =             # empty = DejaVu (default), a bundled name, or an absolute .ttf path
ssh_host    =            # host or user@host: connect via the system ssh on boot
ssh_port    = 22         # set for an ssh server on a non-standard port
telnet_host =            # hostname/IP: connect over Telnet on boot
telnet_port = 23
auto_reconnect = on      # re-dial the link on its own when it drops
```

Set `ssh_host` (`user@host`) or `telnet_host` and the terminal connects to it on
boot instead of using serial (`ssh_host` wins if both are set), routing keystrokes
to the connection and falling back to serial if it fails or drops. SSH runs the
Pi's own `ssh` client over a PTY, so host-key and password prompts appear in the
terminal as normal. For an ssh server on a non-standard port, set `ssh_port`
(Setup: **SSH port**) rather than typing an `ssh://host:port` URI; at the default
22 no `-p` is passed, so a `Port` in `/root/.ssh/config` still applies. Leave both
empty for the serial host link.

### When the session drops

A network session ends for all sorts of reasons — the server reboots, ssh times
out, a cable comes loose, you type `exit`. The terminal notices and prints a
line saying so, then:

- **`auto_reconnect = on`** (the default) re-dials on its own, backing off 1s,
  2s, 4s … up to 30s between attempts, and says what it is doing each time. A
  link that stayed up for at least 10 seconds gets a fresh 1s backoff on its
  next drop; one that dies on contact (bad password, wrong port) keeps backing
  off rather than hammering the server.
- **`auto_reconnect = off`** leaves the link down until you reconnect by hand.

Either way, Setup (**Ctrl+F3**) has a **Host link** row showing the live state —
`connected: ssh pi@10.0.0.5`, `closed - retry in 8s`, and so on — and **Enter**
on that row re-dials immediately. That is the manual reconnect: it works even
when nothing about the destination has changed, so you no longer have to edit
the host address and edit it back to force a reconnect.

Serial keeps working the whole time: while a network link is down the terminal
falls back to the RS232 host link, and picks the network link back up when it
returns. TCP keepalives and ssh's `ServerAlive` options are set on the
connection, so a peer that vanishes without hanging up is still detected (within
about a minute) rather than leaving a wedged session.

Smooth scroll slides the screen up a few pixels per frame instead of jumping a
whole line. A single new line glides in over a few frames; when lines pile up it
slides faster to catch up (up to a row per frame), and a burst more than ~8 lines
behind falls back to an instant jump. `scroll_speed` sets the base rate; higher
is snappier, lower is more languid. Setup steps it in 5 px/s increments over
20-1500, so it can be trimmed precisely; the keyboard's auto-repeat is passed
through to the menu, so hold the arrow to cross the range and tap it to nudge.

### Tuning the pacing

Two knobs control the buffer between the host and the glass, and they pull in
opposite directions:

- **`scroll_depth`** (1-8, default 2) - how many lines of slide may be in flight
  at once. Higher lets more of a burst animate smoothly, at the cost of the
  screen trailing further behind the host; lower keeps the terminal closer to
  live but gives the animation less to work with, so bursty output starves it.
- **`scroll_dump`** (10-400, default 80) - the backlog past which a constant-speed
  scroll can no longer catch up, so the whole buffer is dumped to the screen at
  once and the animation resets, the way a real terminal overruns. Raise it to
  ride out longer bursts smoothly at the cost of more lag; lower it to stay
  closer to live and jump more often.

Setup's header shows which way the pipeline is actually leaning, live:

```
Buffer: keeping up  0/80 peak 12  0 over / 3 under
```

- **falling behind** - output is arriving faster than the scroll displays it and
  the buffer is filling. Raise `scroll_speed`, or `scroll_dump` to absorb longer
  bursts before the screen gives up and jumps.
- **starved** - the buffer keeps running dry mid-slide, so the animation finishes
  with nothing behind it. Raise `scroll_depth` so more is queued before a slide
  starts, or lower `scroll_speed`.
- **keeping up** / **idle** - nothing to fix.

The counters are `over` (times the screen dump-reset) and `under` (times a slide
ran dry). **R** resets them, so a change can be judged on its own rather than
against the whole session.

The reading is live while the menu is open: the buffer genuinely keeps filling
behind it, since nothing is draining it, and the line is marked `[feed paused]`
so a rising number is read for what it is. The verdict and the counters hold
still while paused — they describe the terminal running, not the menu being open.

### Auto speed (closed loop)

`auto_speed = on` puts a PID controller on the scroll speed, holding the backlog
at a small setpoint. Holding a constant backlog means consuming lines exactly as
fast as the host produces them, so in effect it rate-matches the scroll to the
host: a burst speeds it up, quiet output eases it down, and the buffer neither
runs away nor runs dry. `scroll_speed` stops being a fixed rate and becomes the
starting point and the resting speed; the Scroll speed row shows both
(`300 px/s  (auto: 465)`).

**`speed_avg_ms`** (250–8000, default 4000) is how long the host's rate is
averaged over before the speed follows it. Longer is steadier and slower to
react. It is the knob to reach for if the speed looks restless.

Two things stop it looking restless by default:

- **It measures the host, not itself.** The rate is taken from lines *received*
  over the averaging window — never from lines displayed, which the scroll
  itself throttles, so feeding that back would only measure its own output.
- **A 20% deadband.** A real VT100 scrolls at one fixed rate; a speed that
  faithfully tracks every fluctuation in the host is accurate and looks
  restless. So it holds the current speed until the requirement differs by more
  than a fifth, then steps and holds again. In simulation that is the difference
  between changing speed ~230 times a minute and ~30, holding one steady rate
  ~85% of the time.

It is also deliberately sluggish — recomputing 4x a second, ramping at most
15 px/s per step. It can rise to the maximum but will only fall to half the
configured speed: rate-matching a slow host all the way down would make each
line crawl, which is fine for watching a log and awful at a shell prompt. After
two seconds of silence it unwinds back to the configured speed, so every burst
starts from a known rate.

There is **no integral and no derivative term**, despite the name. The plant is
already an integrator — a speed error accumulates into backlog — so an integral
controller on top makes a double integrator, and that hunts: adding one was what
made the speed visibly swing, and adding *more* filtering to it made the swing
worse, not better. A rate feedforward does the work; a small capped proportional
trim on the backlog stops it drifting.

Off by default: it is a different feel, not a fix, and a fixed speed is the
calmer choice for reading.

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
live — no restart. On the **Host link** row **Enter** reconnects instead of
saving (see "When the session drops" above).

### VT100 fidelity

The emulation follows the DEC VT100 (with the Advanced Video Option), so old
host software and `.vt` animations behave as they did on the real terminal:

- **Double-width and double-height lines** (`ESC #3`/`#4`/`#6`, back with
  `ESC #5`). Such a line holds 40 characters; making a line wide drops its right
  half, and erasing the whole screen (ED) returns lines to single width.
- **DEC Special Graphics** with the VT100's own glyphs, including scan lines
  1/3/5/7/9 (`o p q r s`), the checkerboard, and the small two-letter control
  pictures (HT, FF, CR, LF, NL, VT). The UK set (`ESC ( A`, `#` = £) too.
- **VT52 mode** (`ESC [ ? 2 l`, back with `ESC <`): `ESC A`-`D`, `H`, `I`, `J`,
  `K`, `Y` row col, `F`/`G` graphics, `Z` identify. The keyboard follows it.
- **DECSCLM** (`ESC [ ? 4 h`/`l`) switches smooth/jump scroll from the host, as
  the VT100 does. The Smooth scroll setting is what RIS and Setup restore.
- **DECCOLM** (`ESC [ ? 3 h`/`l`) clears the screen and resets the margins.
  The grid stays 80 columns: 132-column mode is not emulated.
- **DECALN** (`ESC # 8`), **DECID** (`ESC Z`), **DECREQTPARM** (`ESC [ x`,
  reports the baud and format below).
- DEL is ignored, and SUB cancels a sequence and shows the checkerboard.
- **New line** (Setup row, `newline`), the VT100 SET-UP's NEWLINE (LNM): a
  received LF also returns the carriage, and Return sends CR LF. Turn it on for
  files with bare-LF line ends (Unix text, many `.vt` animations), which
  otherwise stair-step across the screen. Hosts can still set or reset it
  (`ESC [ 20 h`/`l`); RIS goes back to the Setup value. Leave it off for hosts
  that send CR LF themselves -- with it on, Return's extra LF can show up as
  blank lines there.
- **Serial format** (Setup row, `format`): 8N1, or 7 data bits with even, odd,
  mark or space parity. With 7 bits the eighth bit is ignored, as on a VT100,
  so a host sending parity no longer shows up as stray graphics characters.
  Received parity is not checked.
- **XON/XOFF** (Setup row, `xon_xoff`): XOFF at three-quarters of the 64 KB
  input buffer, XON at a quarter. This only fires on a deep backlog, or with
  Setup open on a busy host (serial keeps being read while the menu is up).
  It needs a host that honors ^S/^Q (RT-11: `SET TT PAGE`; Linux: `ixon`).

The keyboard maps the VT100's keys onto a PC one:

| VT100 | PC |
|---|---|
| PF1 PF2 PF3 PF4 | F1 F2 F3 F4, and Num Lock / `/` / `*` for PF1-PF3 |
| keypad 0-9 `.` `-` Enter | keypad 0-9 `.` `-` Enter |
| keypad `,` | keypad `+` (same position) |
| NO SCROLL | Scroll Lock (sends XOFF, then XON) |
| BREAK | Pause/Break (serial only) |

The keypad sends digits in numeric mode and `ESC O p`... in application mode
(`ESC =`), as editors like KED and EDT expect. Num Lock state is ignored.

### Web view (OBS, presentations)

The terminal can mirror its screen to a web page, for recording videos or
putting it on a projector. In Setup (Ctrl+F3) turn **Web view** on (and
change **Web port** if 8080 is taken), press Enter, and the row shows the
address: `http://<this Pi>:8080/`. Or set `web_view = on` / `web_port` in
`vt100.conf`.

- **OBS:** add a *Browser Source* with that URL, and set its width and height
  to the Pi's display mode (e.g. 1920 x 1080) so it shows 1:1.
- **A browser:** open the URL. Double-click for full screen.

The page does not receive video. The Pi streams screen *changes* (cells,
colours, cursor, scrolls) and the page redraws them with the Pi's own glyph
atlas and the same drawing code as the HDMI renderer. The browser therefore
shows the same pixels as the monitor, including themes, smooth scroll,
double-size lines and DEC graphics, and it stays sharp when OBS scales it.
That costs the Pi almost nothing (a few kB/s while text is moving) and works
over the Zero's Wi-Fi, which could not carry 1080p video. Up to 6 browsers can
watch at once; each one that connects gets the current screen straight away.

The page is view-only, and has **no login**: anyone who can reach the Pi's
port can watch the screen, so leave it off on networks you don't trust.

## Boot straight into the terminal (kiosk mode)

```
./install.sh --deps     # first time on a fresh Pi
./install.sh            # every time after that (git pull && ./install.sh)
```

`install.sh` builds, installs the binary/fonts/unit, enables the service and
starts it. It is safe to re-run: on a machine that already has it, it stops the
running terminal, reinstalls and starts it again. The build happens *before* the
service is stopped, so a failed build leaves the running terminal untouched, and
the stop-before-copy is what avoids `cp`'s "Text file busy" on the live binary.

Run it as your normal user, **not** under `sudo` — it escalates only for the
steps that need root, and building as root would leave root-owned files in the
source tree that break the next ordinary build or `git pull`. It refuses to run
as root for that reason.

**Kiosk is the default.** `getty@tty1` is masked, so nothing else draws on the
screen and the Pi boots straight into the terminal — that is the whole point of
the machine. It is masked rather than disabled because logind spawns an autovt
getty on demand, and a merely disabled one comes back and takes the VT, leaving
the monitor on a login prompt. ssh is unaffected; this only removes the console
on the attached screen. Undo with:

```
sudo systemctl unmask --now getty@tty1.service
```

| flag | effect |
|---|---|
| `--deps` | `apt-get` the build dependencies first |
| `--no-kiosk` | leave the tty1 login console in place (it will fight for the display) |
| `--no-enable` | install and start, but don't start at boot |
| `--clean` | wipe the build directory first |
| `--build-only` | build and install nothing (no root needed) |
| `--no-build` | install the existing build without rebuilding (the updater uses this) |

<details><summary>Equivalent manual steps</summary>

```
cmake -S . -B build && cmake --build build
sudo systemctl stop vt100-pi.service            # before replacing a running binary
sudo install -Dm755 build/vt100-pi-zero /usr/local/bin/vt100-pi-zero
sudo mkdir -p /usr/local/share/vt100-pi/fonts
sudo cp assets/*.ttf /usr/local/share/vt100-pi/fonts/   # so bundled fonts resolve off-tree
sudo cp systemd/vt100-pi.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl mask --now getty@tty1.service
sudo systemctl enable --now vt100-pi.service
```
</details>

Note the service runs as `root`, so the settings file it reads is
`/root/.config/vt100-pi/vt100.conf` — not the one under your own `$HOME`.

### Updating from GitHub

`install.sh` also installs an updater that follows `main` in the clone you ran
it from, so keep that clone where it is. Once a day (and 3 minutes after boot)
a timer fetches from GitHub and records whether any commits touching
`VT100-PI-ZERO/` are waiting. **It only reports; it never installs on its own.**
A waiting update shows up in two places:

- the boot splash, under the `VT100 OK` box
- **Setup (Ctrl+F3) -> Updates**, at the bottom of the list, which reads e.g.
  `3 new (7f9c413) -- Enter to install`; the help line shows the newest commit

Enter on **Updates** runs a check right away. When an update is waiting, Enter
installs it instead: it pulls, builds as the clone's owner (a minute or two on
the Zero 2 W; the row shows progress), installs, and restarts the terminal. That
restart drops any live ssh/telnet session. The kiosk and start-at-boot choices
the box has are kept. A failed build leaves the running terminal alone, and a
new binary that won't stay up is rolled back to the previous one.

From a shell, the same thing:

```
sudo vt100-pi-update check          # or: apply
journalctl -u 'vt100-pi-update@*'   # what it did
```

The status it reports is in `/var/lib/vt100-pi/update.status`. The fetch must
work without a prompt: a clone over `https://` of this public repo does. An
ssh clone needs a key with no passphrase for the clone's owner, or it reports
`fetch failed`. Uncommitted edits in the clone make the pull fail. Stash or
discard them.

### Boot time

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
- `src/net/webview.c/.h` + `web/index.html` — **new**. The web view: a
  small non-blocking HTTP server in the main `poll()` loop that streams
  screen diffs as Server-Sent Events. `textmode_get_view()` exposes what the
  renderer is drawing (palette, cursor, line sizes, smooth-scroll slides).
  The page ports `draw_cell`/`blit_glyph` to JavaScript, so a change to how
  textmode.c draws must be mirrored there. The page is compiled into the
  binary by `cmake/embed.cmake`.
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
