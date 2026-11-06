#!/usr/bin/env bash
# Test the keyboard link (app/pi/kblink.c) here on OMV: app/test/kblink_test.c
# runs it against a stand-in Pi in a temporary directory (the Cyber60's
# hidraw nodes as pipes, the computer's /dev/hidg0 and hidg1 as files, a
# fake USB port, rfkill and busctl): passthrough, the merge with what the Pi
# types, sv/us text, Caps Lock, capture, Win+L, sleep, unplugging.
#
# Usage: pi/tools/kblink-test.sh        (exit code 0 = all passed)
set -euo pipefail

PI=$(cd "$(dirname "$0")/.." && pwd)
WEST=${ZMK_WEST:-/srv/mergerfs/pool/docker/appdata/zmk-west}
CACHE=${DECK_CACHE:-$WEST/.cache/deck}
"$PI/tools/fetch-deps.sh"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

# deck.h needs LVGL's headers only; the test stands in for the screen.
gcc -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -I"$PI/app" -I"$PI/app/src" \
    -I"$PI/app/pi" -I"$CACHE/deps/lvgl-9.3.0" -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE \
    "$PI/app/test/kblink_test.c" "$PI/app/pi/kbkeys.c" -lpthread -o "$T/kblink_test"
mkdir "$T/pi"
"$T/kblink_test" "$T/pi"
