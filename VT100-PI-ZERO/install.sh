#!/usr/bin/env bash
#
# Build, install and enable VT100-PI-ZERO. Safe to re-run: on a machine that
# already has it, this stops the running terminal, reinstalls, and starts it
# again.
#
#   ./install.sh              build, install, enable at boot, start
#   ./install.sh --deps       apt-get the build dependencies first
#   ./install.sh --kiosk      also mask getty@tty1 (console kiosk)
#   ./install.sh --no-enable  install and start, but don't start at boot
#   ./install.sh --clean      wipe the build directory first
#   ./install.sh --build-only build, install nothing (no root needed)
#
# The build runs before the service is stopped, so a failed build leaves the
# running terminal untouched.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

SERVICE=vt100-pi.service
BIN_SRC_NAME=vt100-pi-zero
BIN_DST=/usr/local/bin/vt100-pi-zero
FONT_DST=/usr/local/share/vt100-pi/fonts
UNIT_DST=/etc/systemd/system/$SERVICE

DO_DEPS=0 DO_KIOSK=0 DO_ENABLE=1 DO_CLEAN=0 BUILD_ONLY=0 ALLOW_ROOT=0

say()  { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
info() { printf '    %s\n' "$*"; }
die()  { printf '\n\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

# Print the header comment block (everything from line 2 up to the first
# non-comment line), so --help can never drift from the comment above.
usage() {
    awk 'NR > 1 { if (/^#/) { sub(/^# ?/, ""); print } else { exit } }' "${BASH_SOURCE[0]}"
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --deps)       DO_DEPS=1 ;;
        --kiosk)      DO_KIOSK=1 ;;
        --no-enable)  DO_ENABLE=0 ;;
        --clean)      DO_CLEAN=1 ;;
        --build-only) BUILD_ONLY=1 ;;
        --allow-root) ALLOW_ROOT=1 ;;
        -h|--help)    usage ;;
        *)            die "unknown option '$1' (try --help)" ;;
    esac
    shift
done

# ---- privilege ------------------------------------------------------------
# Deliberately refuse `sudo ./install.sh`: cmake would leave root-owned objects
# in the source tree, and the next ordinary build (or git operation) then fails
# with "Permission denied". The script escalates only for the steps that need it.
if [ "$(id -u)" -eq 0 ] && [ "$ALLOW_ROOT" -eq 0 ]; then
    die "don't run this under sudo -- it would leave root-owned files in $SCRIPT_DIR.
       Run it as your normal user; it will ask for sudo only when installing.
       (Override with --allow-root if you really are a root-only system.)"
fi
SUDO=""
if [ "$(id -u)" -ne 0 ]; then SUDO="sudo"; fi

# ---- dependencies ---------------------------------------------------------
if [ "$DO_DEPS" -eq 1 ]; then
    say "Installing build dependencies"
    $SUDO apt-get update
    $SUDO apt-get install -y build-essential cmake libdrm-dev libfreetype-dev pkg-config
fi

say "Checking build dependencies"
missing=""
command -v cmake >/dev/null 2>&1 || missing="$missing cmake"
command -v pkg-config >/dev/null 2>&1 || missing="$missing pkg-config"
if command -v pkg-config >/dev/null 2>&1; then
    pkg-config --exists libdrm    || missing="$missing libdrm-dev"
    pkg-config --exists freetype2 || missing="$missing libfreetype-dev"
fi
if [ -n "$missing" ]; then
    die "missing:$missing
       Re-run with --deps to install them, or:
       sudo apt install build-essential cmake libdrm-dev libfreetype-dev pkg-config"
fi
info "all present"

# ---- build ----------------------------------------------------------------
# Before touching the running service, so a broken build costs nothing.
if [ "$DO_CLEAN" -eq 1 ]; then
    say "Removing $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

say "Building"
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build "$BUILD_DIR" -j "$(nproc 2>/dev/null || echo 2)"

BIN_SRC="$BUILD_DIR/$BIN_SRC_NAME"
[ -x "$BIN_SRC" ] || die "build produced no $BIN_SRC"
info "built $BIN_SRC"

if [ "$BUILD_ONLY" -eq 1 ]; then
    say "Build only -- nothing installed"
    exit 0
fi

# ---- stop the running terminal --------------------------------------------
# Must happen before the binary is replaced: copying over a running executable
# fails with ETXTBSY ("Text file busy"). An explicit stop also wins over the
# unit's Restart=always, so it stays down until we start it again.
WAS_ACTIVE=0
if systemctl list-unit-files "$SERVICE" >/dev/null 2>&1 \
   && systemctl is-active --quiet "$SERVICE"; then
    WAS_ACTIVE=1
    say "Stopping $SERVICE"
    $SUDO systemctl stop "$SERVICE"
else
    say "$SERVICE not running (fresh install or already stopped)"
fi

# ---- install --------------------------------------------------------------
say "Installing"
$SUDO install -Dm755 "$BIN_SRC" "$BIN_DST"
info "$BIN_DST"

# Bundled fonts, so they resolve when running off-tree (fonts.c also falls back
# to system directories).
if compgen -G "$SCRIPT_DIR/assets/*.ttf" >/dev/null; then
    $SUDO mkdir -p "$FONT_DST"
    $SUDO install -Dm644 "$SCRIPT_DIR"/assets/*.ttf "$FONT_DST/"
    info "$FONT_DST ($(ls -1 "$SCRIPT_DIR"/assets/*.ttf | wc -l) fonts)"
else
    info "no bundled fonts found -- skipping (system fonts will be used)"
fi

$SUDO install -Dm644 "$SCRIPT_DIR/systemd/$SERVICE" "$UNIT_DST"
info "$UNIT_DST"
$SUDO systemctl daemon-reload

# ---- kiosk ----------------------------------------------------------------
# Masked, not just disabled: logind can spawn an autovt getty on demand, which
# would fight the terminal for tty1.
if [ "$DO_KIOSK" -eq 1 ]; then
    say "Kiosk mode: masking getty@tty1"
    $SUDO systemctl disable --now getty@tty1.service 2>/dev/null || true
    $SUDO systemctl mask getty@tty1.service
    info "text console on tty1 is now off (unmask to undo)"
fi

# ---- enable + start -------------------------------------------------------
if [ "$DO_ENABLE" -eq 1 ]; then
    say "Enabling at boot"
    $SUDO systemctl enable "$SERVICE"
else
    info "left as-is at boot (--no-enable)"
fi

say "Starting $SERVICE"
$SUDO systemctl restart "$SERVICE"
sleep 1

if systemctl is-active --quiet "$SERVICE"; then
    say "Running"
    systemctl --no-pager --lines=0 status "$SERVICE" || true
else
    printf '\n\033[1;31mservice did not come up.\033[0m Recent log:\n\n'
    journalctl -u "$SERVICE" -n 30 --no-pager || true
    if [ "$WAS_ACTIVE" -eq 1 ]; then info "(it was running before this install)"; fi
    exit 1
fi

cat <<EOF

Done.
  Settings   /root/.config/vt100-pi/vt100.conf   (the unit runs as root, so it
             is root's config -- not \$HOME/.config -- and Ctrl+F3 writes there)
  Logs       journalctl -u $SERVICE -f
  Restart    sudo systemctl restart $SERVICE
  Update     git pull && ./install.sh
EOF
