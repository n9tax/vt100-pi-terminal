# VT100-PI

Hardware VT100 terminal on a Raspberry Pi Pico 2 W (RP2350) — bare-metal, no
OS. This is the sibling of [VT100-PI-ZERO](../VT100-PI-ZERO), which does the
same job as a userspace program on a Pi Zero 2 W running Raspberry Pi OS.

- **Video**: PiCowBell HSTX DVI/HDMI out (GP12-19), 80x24 (or 80x25)
  text, Setup-selectable resolution and phosphor/color theme.
- **Serial**: Waveshare RS232 hat, UART0 (GP0/1) — the primary host link.
- **Input**: USB keyboard via PIO-USB (GP6/7), TinyUSB host running on core1.
- **Network** (optional): WiFi + Telnet via the Pico 2 W's CYW43 radio.
- **Setup**: Ctrl+F3 opens the on-screen settings menu (theme, cursor,
  baud, WiFi/Telnet, up to 8 speed-dial sites).

Prebuilt firmware from CI is attached to the repo's
[Releases](../../releases) — grab `VT100-PI.uf2` from the latest build if
you just want to flash it, no toolchain required.

## Hardware / pin map

| Signal | Pin | Purpose |
|---|---|---|
| UART0 TX/RX | GP0/GP1 | RS232 host link (Waveshare hat, primary) |
| UART1 TX/RX | GP4/GP5 | RS232 AUX (reserved; RX unhandled currently) |
| PIO-USB D+/D- | GP6/GP7 | USB keyboard host (PiCowBell host pads) |
| HSTX TMDS | GP12-GP19 | PiCowBell DVI/HDMI out |

Full detail (video mode selection, theme defaults, WiFi/Telnet defaults) is
in [src/config.h](src/config.h).

## Build

Raspberry Pi Pico SDK / CMake / Ninja. Normally driven through the VS Code
"Raspberry Pi Pico" extension (see `.vscode/tasks.json`), which manages the
SDK/toolchain paths for you. From the command line, with the SDK toolchain
on your PATH (or `PICO_SDK_PATH` set):

```
cmake -G Ninja -B build
ninja -C build
```

Output: `build/VT100-PI.uf2` (drag-and-drop flash) and `build/VT100-PI.elf`.

`lib/Pico-PIO-USB` is a git submodule — clone with
`git clone --recurse-submodules`, or run
`git submodule update --init --recursive` after a plain clone.

## Flash

- Hold BOOTSEL while plugging in USB (mounts as `RPI-RP2`), then drag-drop
  `VT100-PI.uf2` — or `picotool load VT100-PI.uf2 -fx`.
- Via a debug probe: `openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg
  -c "adapter speed 5000; program VT100-PI.elf verify reset exit"`.

These are also wired up as VS Code tasks ("Run Project", "Flash",
"Rescue Reset", "RISC-V Reset (RP2350)").

## Verification

No host-side test suite — this is embedded firmware, verified on hardware.
To test the host link without a real serial device on the other end,
connect the RS232 hat's DB9 to another machine via a null-modem cable and
run, on that machine:

```
sudo bash -c 'stty -F /dev/ttyS0 sane 9600 cs8 -cstopb -parenb clocal -crtscts; TERM=vt100 setsid bash -i <>/dev/ttyS0 >&0 2>&0'
```

## Architecture

Core0 runs one big polling loop (video, serial/telnet RX, keyboard TX, Setup
menu, DVI watchdog); core1 owns the USB host stack entirely and hands
decoded keystrokes to core0 over a lock-free queue. Host bytes flow
`serial.c`/`net.c` → `vt100_feed()` → `vtparse.c` (DEC/ANSI state machine) →
`vt100.c` (dispatch) → `screen.c` (cursor/SGR/scroll semantics) →
`textmode.c` (cell grid) → `hstx_dvi.c` (DMA scanout).

The cross-cutting hazards worth knowing before touching this code: DMA channel
ownership across the two cores, the core1 lockout during flash writes, and the
DVI watchdog's rate-deviation logic.
