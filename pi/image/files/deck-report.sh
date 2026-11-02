#!/bin/sh
# Writes deck-boot.txt on the SD card's boot partition 30 s after every boot:
# what the Pi saw of its display, touch and power, the screen's journal and
# the kernel log. Nobody can log in to this Pi, so when the screen stays black
# this file (on the "bootfs" drive, and on DECK) says why.
#   deck-report.sh [seconds to wait first]   (deck-report.service)
BOOT=${DECK_BOOT:-/boot/firmware}
OUT=$BOOT/deck-boot.txt

sleep "${1:-30}"

section() { printf '\n== %s\n' "$1"; }

{
    echo "Desk screen boot report, $(cut -d' ' -f1 /proc/uptime) s after power-on"
    echo "Screen version: $(head -n1 /opt/deck/VERSION 2>/dev/null)"
    echo "Pi:             $(tr -d '\0' 2>/dev/null </proc/device-tree/model)"
    echo "Kernel:         $(uname -r)"
    echo "Power:          $(vcgencmd get_throttled 2>/dev/null) (0x0 = fine; 0x50005 = undervoltage now)"

    section "Kernel command line"
    cat /proc/cmdline

    section "Display outputs (/sys/class/drm)"
    for c in /sys/class/drm/card*; do
        [ -e "$c/device" ] || continue
        printf '%s  driver %s\n' "${c##*/}" "$(basename "$(readlink -f "$c/device/driver")")"
    done
    for c in /sys/class/drm/card*-*; do
        [ -f "$c/status" ] || continue
        printf '  %-16s %-12s %s\n' "${c##*/}" "$(cat "$c/status")" "$(tr '\n' ' ' <"$c/modes")"
    done
    ls -l /dev/dri /dev/fb* 2>&1

    section "Input devices"
    grep -E '^N: ' /proc/bus/input/devices 2>&1

    section "The screen (deck.service)"
    systemctl status deck.service --no-pager -n 0 2>&1
    journalctl -b -u deck.service --no-pager -n 60 -o short-monotonic 2>&1

    section "Failed units"
    systemctl --failed --no-pager --plain 2>&1

    section "Kernel: display, touch, USB, power"
    dmesg 2>&1 | grep -iE 'drm|vc4|v3d|dsi|panel|backlight|attiny|ft5|edt|goodix|touch|regulator|dwc2|voltage|error|fail' | head -n 200

    section "Kernel log"
    dmesg 2>&1 | tail -n 400
} >"$OUT.new" 2>&1
mv -f "$OUT.new" "$OUT"
sync
