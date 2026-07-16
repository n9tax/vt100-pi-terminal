# VT100-PI

Hardware VT100 terminal emulators. Two implementations, same behavior and
feel (fullscreen VT100 emulator, RS232 host link, boots straight into the
terminal, on-screen Setup menu), built for two very different boards:

| | [VT100-PI](VT100-PI/) | [VT100-PI-ZERO](VT100-PI-ZERO/) |
|---|---|---|
| Board | Raspberry Pi Pico 2 W (RP2350) | Raspberry Pi Zero 2 W |
| Runtime | Bare-metal, Pico SDK | Raspberry Pi OS (Lite), userspace C |
| Video | Hand-rolled HSTX DVI/DMA scanout | DRM/KMS |
| Keyboard | TinyUSB host + PIO-USB (core1) | evdev |
| Network | CYW43 + raw lwIP | POSIX sockets (planned) |
| Status | Working | MVP: video + keyboard + serial |

CI: [![VT100-PI](https://github.com/n9tax/vt100-pi-terminal/actions/workflows/vt100-pi.yml/badge.svg)](https://github.com/n9tax/vt100-pi-terminal/actions/workflows/vt100-pi.yml) [![VT100-PI-ZERO](https://github.com/n9tax/vt100-pi-terminal/actions/workflows/vt100-pi-zero.yml/badge.svg)](https://github.com/n9tax/vt100-pi-terminal/actions/workflows/vt100-pi-zero.yml)

## Why two implementations

VT100-PI (the Pico build) works, but is up against the Pico's hardware
ceiling: hand-rolled DMA video scanout, a hand-rolled USB host stack, 520KB
SRAM total, and a hand-rolled network stack, all contending for DMA channels
and bus bandwidth. VT100-PI-ZERO ports the same terminal *behavior* to a Pi
Zero 2 W running Raspberry Pi OS, trading the Pico's instant boot for a real
GPU, a real USB host stack, a real network stack, and 512MB of RAM.

The two share no code at the build-system level, but VT100-PI-ZERO's
terminal core (`vtparse.c`, `screen.c`, and most of `vt100.c`) is a direct
port of VT100-PI's — see [VT100-PI-ZERO/README.md](VT100-PI-ZERO/README.md#architecture)
for exactly what ported unmodified versus what's new.

## Install

The fastest path for either board is a prebuilt binary from
[Releases](../../releases) — every push to `main` rebuilds both projects in
CI and attaches the output. No toolchain needed.

**VT100-PI (Pico 2 W):** download `VT100-PI.uf2` from the latest release,
hold BOOTSEL while plugging the Pico in over USB (it mounts as `RPI-RP2`),
then drag-drop the `.uf2` onto it. Full build-from-source and flashing
options (picotool, openocd/SWD) in [VT100-PI/README.md](VT100-PI/README.md).

**VT100-PI-ZERO (Pi Zero 2 W):** download `vt100-pi-zero-aarch64` from the
latest release, copy it to the Pi (`scp`, or straight off a USB stick), and
`chmod +x` it. It needs root for raw `/dev/dri`/`/dev/input` access:
`sudo ./vt100-pi-zero-aarch64`. Full build-from-source, dependencies, and
boot-to-terminal (systemd) setup in
[VT100-PI-ZERO/README.md](VT100-PI-ZERO/README.md).

Building from source instead of using a release build is documented in each
project's own README, linked above.

## Repo layout

```
VT100-PI/         Bare-metal Pico 2 W firmware (Pico SDK / CMake)
VT100-PI-ZERO/    Raspberry Pi OS userspace terminal (CMake)
.github/workflows/  CI: aarch64 build for VT100-PI-ZERO, Pico SDK cross-build for VT100-PI
```

`VT100-PI/lib/Pico-PIO-USB` is a git submodule (vendored third-party PIO-USB
host library) — clone with `git clone --recurse-submodules`, or run
`git submodule update --init --recursive` after a plain clone.
