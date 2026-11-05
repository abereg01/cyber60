#!/usr/bin/env bash
# Flash the Cyber60 through the Pi desk screen, from the OMV box.
#
# The keyboard sits on one of the Pi's USB-A ports. Put it in bootloader mode
# (SYSTEM + Ctrl/Bsp, or double-tap the reset button on the PCB): a small UF2
# drive appears on the Pi, and this copies the firmware onto it over SSH. The
# keyboard restarts with it by itself.
#
# Usage: tools/flash.sh [file.uf2]     (default: firmware/cyber60.uf2)
#        DECK_HOST=<ssh host> tools/flash.sh   (default host: deck)
#
# The .uf2 can hold the password macros (local builds), so on the Pi it only
# ever sits in /run (RAM) and is deleted again whatever happens.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
FW=${1:-$REPO/firmware/cyber60.uf2}
HOST=${DECK_HOST:-deck}
WAIT_S=${WAIT_S:-120}

[ -f "$FW" ] || { echo "no such file: $FW" >&2; exit 1; }
echo "==> $(basename "$FW") → $HOST"
scp -q "$FW" "$HOST:/run/cyber60-flash.uf2"

# shellcheck disable=SC2029  # WAIT_S is meant to expand here, on OMV
ssh "$HOST" "WAIT_S=$WAIT_S bash -s" <<'EOF'
set -eu
FW=/run/cyber60-flash.uf2
MNT=/run/uf2
# The firmware goes first: under set -e a failing umount/rmdir would end the
# trap before it got there.
cleanup() {
    rm -f "$FW"
    if mountpoint -q "$MNT"; then umount "$MNT" 2>/dev/null || true; fi
    rmdir "$MNT" 2>/dev/null || true
}
trap cleanup EXIT

# A UF2 bootloader is a small FAT drive with INFO_UF2.TXT on it.
find_uf2() {
    for dev in /dev/disk/by-id/usb-*; do
        [ -e "$dev" ] || continue
        [ "$(lsblk -no FSTYPE "$dev" 2>/dev/null | head -1)" = vfat ] || continue
        mkdir -p "$MNT"
        mount -t vfat "$dev" "$MNT" 2>/dev/null || continue
        if [ -f "$MNT/INFO_UF2.TXT" ]; then
            echo "$dev"
            return 0
        fi
        umount "$MNT"
    done
    return 1
}

echo "    waiting for the keyboard's bootloader drive (SYSTEM + Ctrl/Bsp, or double-tap reset)..."
dev=
for _ in $(seq "$WAIT_S"); do
    if dev=$(find_uf2); then break; fi
    dev=
    sleep 1
done
[ -n "$dev" ] || { echo "    no UF2 drive turned up within ${WAIT_S}s" >&2; exit 1; }

echo "    found $(basename "$dev")"
sed 's/^/      /' "$MNT/INFO_UF2.TXT"
# The bootloader restarts the keyboard as soon as the last block lands, which
# can make the final sync/umount fail. That's fine: the drive vanishing is
# the sign it worked.
cp "$FW" "$MNT/" 2>/dev/null || true
sync 2>/dev/null || true
umount "$MNT" 2>/dev/null || true
for _ in $(seq 15); do
    [ -e "$dev" ] || { echo "    flashed: the keyboard is restarting"; exit 0; }
    sleep 1
done
echo "    the drive is still there after 15 s: the copy may not have taken" >&2
exit 1
EOF
