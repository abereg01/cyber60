#!/usr/bin/env bash
# Test the calendar code (app/src/calendar.c) here on OMV, before it goes on
# an SD card.
#
#   pi/tools/calendar-test.sh                       the checks: app/test/calendar_test.c against the
#                                                   made-up calendars in app/test/ics/ (exit 0 = all passed)
#   pi/tools/calendar-test.sh show <file.ics> ["2026-11-05 09:00"]
#                                                   what the screen would show from a calendar file
#   pi/tools/calendar-test.sh live                  the real published calendar, fetched like the Pi does
#
# live needs the Outlook ICS link alone in the deck cache's
# calendar-test/calendar-url (mode 600, never in git: anyone with it can read
# the calendar).
set -euo pipefail

PI=$(cd "$(dirname "$0")/.." && pwd)
WEST=${ZMK_WEST:-/srv/mergerfs/pool/docker/appdata/zmk-west}
CACHE=${DECK_CACHE:-$WEST/.cache/deck}

[ -w "${HOME:-/}" ] || export DOCKER_CONFIG=${DOCKER_CONFIG:-/srv/mergerfs/pool/docker/appdata/.docker-config}
docker build -q --network=host -t deck-tools:local -f "$PI/image/Dockerfile.tools" "$PI/image" >/dev/null
"$PI/tools/fetch-deps.sh"
[ -f "$CACHE/preview-build/liblvgl.a" ] || { echo "Run pi/tools/preview.sh once first (it builds LVGL)."; exit 1; }

MOUNT=()
ARGS=(/pi/app/test/ics)
case ${1:-} in
show)
    [ -f "${2:-}" ] || { echo "usage: $0 show <file.ics> [\"YYYY-MM-DD HH:MM\"]"; exit 2; }
    MOUNT=(-v "$(realpath "$2")":/in.ics:ro)
    ARGS=(show /in.ics "${@:3}")
    ;;
live)
    T=$CACHE/calendar-test
    mkdir -p "$T" && chmod 700 "$T"
    [ -s "$T/calendar-url" ] || { echo "Put the Outlook ICS link alone in $T/calendar-url, then run this again."; exit 1; }
    chmod 600 "$T/calendar-url"
    ARGS=(live /cache/calendar-test/calendar-url)
    ;;
esac

# The preview's LVGL (x86, no display drivers) does for the timer. TZ: the
# Pi's zone as a POSIX rule, so it works without tzdata in the container.
docker run --rm -i --network host --user "$(id -u):$(id -g)" -e HOME=/tmp -e TZ=CET-1CEST,M3.5.0,M10.5.0/3 \
    -v "$PI":/pi -v "$CACHE":/cache "${MOUNT[@]}" deck-tools:local bash -c '
set -e
L=/cache/deps/lvgl-9.3.0
gcc -O1 -Wall -Wextra -Wno-unused-parameter -DDECK_PREVIEW -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE \
    -I/pi/app -I/pi/app/src -I$L /pi/app/test/calendar_test.c /pi/app/src/calendar.c \
    /cache/preview-build/liblvgl.a -lcurl -lpthread -lm -o /tmp/calendar_test
[ "$1" = /pi/app/test/ics ] || exec /tmp/calendar_test "$@"
/tmp/calendar_test "$@"
# The worker too (fetch, the HTML link swapped for the ICS one, the hand-over
# to the screen), against a stand-in for Outlook on this machine.
echo "==> the worker, against a stand-in server"
python3 -m http.server 18089 --bind 127.0.0.1 --directory /pi/app/test/ics >/dev/null 2>&1 & srv=$!
sleep 0.5
echo http://127.0.0.1:18089/outlook.html >/tmp/link
/tmp/calendar_test live /tmp/link | head -n 4
echo "==> a link that goes nowhere"
echo http://127.0.0.1:18090/calendar.ics >/tmp/link
if /tmp/calendar_test live /tmp/link; then echo "FAIL: that should not have worked"; exit 1; fi
kill $srv' _ "${ARGS[@]}"
