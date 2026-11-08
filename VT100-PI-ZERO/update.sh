#!/usr/bin/env bash
#
# Check GitHub for a newer VT100-PI-ZERO, or install it. Installed by install.sh
# as /usr/local/bin/vt100-pi-update and run as root by systemd:
#
#   vt100-pi-update check    fetch, and record whether an update is waiting
#   vt100-pi-update apply    pull, build, install, restart the terminal
#
# Nothing installs on its own: the daily timer (vt100-pi-update-check.timer)
# only runs `check`, and the terminal shows the result in Setup (Ctrl+F3 ->
# Updates) and on the boot splash. `apply` runs when that row is Entered, as the
# vt100-pi-update@apply unit -- never as a child of the terminal, because
# installing restarts vt100-pi.service and would kill its own updater.
#
# Updates come from the git clone install.sh was run from (recorded in
# /etc/vt100-pi/update.conf). git and the build run as that clone's owner, so
# the tree never gets root-owned files; only the install step runs as root.
# Any it does get (from a git run as root by hand) are handed back first.
# Only commits that touch VT100-PI-ZERO/ count as an update.
#
# The result goes to /var/lib/vt100-pi/update.status (key=value) for the
# terminal to read. Logs: journalctl -u 'vt100-pi-update@*'

# Everything runs inside main(), which bash parses in full before running it:
# install.sh replaces this very file during `apply`, and bash reads a script
# as it goes.
main() {
set -uo pipefail

CONF=/etc/vt100-pi/update.conf
STATE_DIR=/var/lib/vt100-pi
STATUS=$STATE_DIR/update.status
INSTALLED=$STATE_DIR/installed-commit
ERR=$STATE_DIR/last-git-error
SUBDIR=VT100-PI-ZERO
SERVICE=vt100-pi.service
BIN=/usr/local/bin/vt100-pi-zero

mkdir -p "$STATE_DIR"

# Status fields, rewritten whole (via rename) so the terminal never reads half
# a file. state: checking | current | available | updating | failed
state="" behind=0 installed="" latest="" subject="" msg=""
write_status() {
    local tmp="$STATUS.tmp"
    {
        echo "state=$state"
        echo "behind=$behind"
        echo "installed=$installed"
        echo "latest=$latest"
        echo "subject=$subject"
        echo "checked=$(date +%s)"
        echo "msg=$msg"
    } >"$tmp" && mv -f "$tmp" "$STATUS"
}
fail() {
    state=failed msg="$1"
    write_status
    echo "vt100-pi-update: $1" >&2
    exit 1
}

[ -r "$CONF" ] || fail "not set up (run install.sh)"
# shellcheck source=/dev/null
. "$CONF"
[ -d "${REPO:-}/.git" ] || fail "no git clone at ${REPO:-?}"
OWNER=${OWNER:-}
OWNER_HOME=$(getent passwd "$OWNER" | cut -d: -f6)
[ -n "$OWNER" ] && [ -n "$OWNER_HOME" ] || fail "unknown clone owner '$OWNER'"

# git and the build as the clone's owner. Never prompt: there is nobody at a
# keyboard to answer, and a prompt would hang the unit until it times out.
as_owner() {
    runuser -u "$OWNER" -- env HOME="$OWNER_HOME" GIT_TERMINAL_PROMPT=0 \
        GIT_SSH_COMMAND="ssh -o BatchMode=yes" "$@"
}
git_() { as_owner git -C "$REPO" "$@"; }

# A git or build run as root in the clone (sudo, or a root shell) leaves files
# the owner cannot write, and the next fetch dies with "unpack-objects failed".
# We are root, so give them back before touching the tree.
fix_ownership() {
    local n
    n=$(find "$REPO" ! -user "$OWNER" -print -quit 2>/dev/null)
    [ -n "$n" ] || return 0
    echo "vt100-pi-update: returning root-owned files in $REPO to $OWNER"
    find "$REPO" ! -user "$OWNER" -exec chown -h "$OWNER": {} +
}

# git's real complaint: the first error:/fatal: line (the last line is often
# just a generic "unpack-objects failed"), else whatever it said last.
git_error() {
    local e
    e=$(grep -m1 -E '^(error|fatal):' "$ERR")
    echo "${e:-$(tail -1 "$ERR")}"
}

# What is installed: the commit install.sh recorded, or HEAD for an install
# that predates the record.
base_commit() {
    local c
    c=$(cat "$INSTALLED" 2>/dev/null)
    if [ -n "$c" ] && git_ cat-file -e "$c^{commit}" 2>/dev/null; then echo "$c"
    else git_ rev-parse HEAD; fi
}

check() {
    state=checking msg="checking GitHub"
    write_status
    fix_ownership
    git_ fetch --quiet 2>"$ERR" \
        || fail "fetch failed: $(git_error)"
    git_ rev-parse --verify --quiet '@{u}' >/dev/null \
        || fail "branch has no upstream to update from"
    local base
    base=$(base_commit)
    installed=$(git_ rev-parse --short "$base")
    latest=$(git_ rev-parse --short '@{u}')
    behind=$(git_ rev-list --count "$base..@{u}" -- "$SUBDIR")
    if [ "$behind" -gt 0 ]; then
        state=available
        subject=$(git_ log -1 --format=%s '@{u}' -- "$SUBDIR")
        msg=""
    else
        state=current subject="" msg=""
    fi
    write_status
    echo "vt100-pi-update: $state (installed $installed, upstream $latest, $behind behind)"
}

apply() {
    check
    [ "$state" = available ] || { echo "vt100-pi-update: nothing to install"; return 0; }

    state=updating msg="pulling"
    write_status
    git_ pull --ff-only --quiet 2>"$ERR" \
        || fail "pull failed (local changes?): $(git_error)"

    # Built before anything is stopped: a broken build leaves the running
    # terminal alone.
    msg="building (a minute or two)"
    write_status
    as_owner "$REPO/$SUBDIR/install.sh" --build-only || fail "build failed (see journal)"

    # Reinstall with the same kiosk / start-at-boot choices this box has now.
    local flags=(--allow-root --no-build)
    systemctl is-enabled getty@tty1.service 2>/dev/null | grep -q masked || flags+=(--no-kiosk)
    systemctl is-enabled --quiet "$SERVICE" || flags+=(--no-enable)

    msg="installing (the terminal will restart)"
    write_status
    cp -f "$BIN" "$BIN.prev" 2>/dev/null
    cp -f "$INSTALLED" "$INSTALLED.prev" 2>/dev/null
    local ok=1
    "$REPO/$SUBDIR/install.sh" "${flags[@]}" || ok=0
    # install.sh only sees the first second; a binary that crashes on start
    # shows up as Restart=always climbing NRestarts.
    if [ "$ok" -eq 1 ]; then
        sleep 5
        systemctl is-active --quiet "$SERVICE" || ok=0
        [ "$(systemctl show -p NRestarts --value "$SERVICE")" -eq 0 ] || ok=0
    fi
    if [ "$ok" -eq 0 ] && [ -x "$BIN.prev" ]; then
        systemctl stop "$SERVICE"
        cp -f "$BIN.prev" "$BIN"
        cp -f "$INSTALLED.prev" "$INSTALLED" 2>/dev/null
        systemctl start "$SERVICE"
        fail "new build would not run -- rolled back to $installed"
    fi
    [ "$ok" -eq 1 ] || fail "install failed (see journal)"

    installed=$(git_ rev-parse --short HEAD)
    state=current behind=0 subject="" msg="updated to $installed"
    write_status
    echo "vt100-pi-update: updated to $installed"
}

case "${1:-}" in
    check) check ;;
    apply) apply ;;
    *)     echo "usage: vt100-pi-update check|apply" >&2; exit 2 ;;
esac
}

main "$@"; exit
