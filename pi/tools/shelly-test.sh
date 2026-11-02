#!/usr/bin/env bash
# Run the real lights code (app/src/lights.c) here on OMV, before it goes on
# an SD card: against the real Shelly Cloud, or against a stand-in for it.
#
#   pi/tools/shelly-test.sh mock          the stand-in (app/test/shelly_mock.py): a fader drag
#                                         and a brightness set, checking Shelly's 1 request/s
#                                         limit and that a drag sends only its latest value
#   pi/tools/shelly-test.sh               every lamp's state, from the real cloud
#   pi/tools/shelly-test.sh on [lamp]     off [lamp] | <1-100> [lamp] | drag [lamp]   (lamp 0 = the first)
#
# The real cloud needs two files in the deck cache's shelly-test/ (mode 600,
# never in git):
#   shelly.conf   the same lines as deck.conf: shelly_server=… and light=NAME:deviceid
#   shelly-key    the cloud key alone (Shelly app → User settings → Authorization cloud key)
set -euo pipefail

PI=$(cd "$(dirname "$0")/.." && pwd)
WEST=${ZMK_WEST:-/srv/mergerfs/pool/docker/appdata/zmk-west}
CACHE=${DECK_CACHE:-$WEST/.cache/deck}
T=$CACHE/shelly-test
mkdir -p "$T" && chmod 700 "$T"

[ -w "${HOME:-/}" ] || export DOCKER_CONFIG=${DOCKER_CONFIG:-/srv/mergerfs/pool/docker/appdata/.docker-config}
docker build -q --network=host -t deck-tools:local -f "$PI/image/Dockerfile.tools" "$PI/image" >/dev/null
"$PI/tools/fetch-deps.sh"
[ -f "$CACHE/preview-build/liblvgl.a" ] || { echo "Run pi/tools/preview.sh once first (it builds LVGL)."; exit 1; }

# The preview's LVGL (x86, no display drivers) does for the timers.
BUILD='
L=/cache/deps/lvgl-9.3.0
gcc -O1 -Wall -Wno-unused-parameter -DDECK_PREVIEW -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE \
    -I/pi/app -I/pi/app/src -I$L -I/cache/deps/cJSON-1.7.18 /pi/app/test/lights_cli.c /pi/app/src/lights.c \
    /cache/deps/cJSON-1.7.18/cJSON.c /cache/preview-build/liblvgl.a -lcurl -lpthread -lm -o /tmp/lights_cli'
run() { docker run --rm -i --network host --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PI":/pi -v "$CACHE":/cache \
    deck-tools:local bash -c "$1" _ "${@:2}"; }

if [ "${1:-}" = mock ]; then
    run "set -e
$BUILD"'
echo test-key >/tmp/key
lamps=(DESK:a0b1c2d3e4f5 FLOOR:0a1b2c3d4e5f)
ok=0
for t in "drag 0 90" "40 1 40"; do
    set -- $t
    echo "==> $1 lamp $2"
    python3 /pi/app/test/shelly_mock.py 18080 "$3" & mock=$!
    sleep 0.5
    /tmp/lights_cli http://127.0.0.1:18080 /tmp/key "${lamps[@]}" -- "$1" "$2"
    kill -TERM $mock; wait $mock || ok=1
done
exit $ok'
    exit
fi

if [ ! -s "$T/shelly.conf" ] || [ ! -s "$T/shelly-key" ]; then
    echo "Put shelly_server=… and light=NAME:deviceid lines in $T/shelly.conf"
    echo "and the cloud key alone in $T/shelly-key, then run this again."
    exit 1
fi
chmod 600 "$T/shelly.conf" "$T/shelly-key"
SERVER=$(sed -n 's/^shelly_server=//p' "$T/shelly.conf" | head -1)
mapfile -t LAMPS < <(sed -n 's/^light=//p' "$T/shelly.conf")
[ -n "$SERVER" ] && [ "${#LAMPS[@]}" -gt 0 ] || { echo "shelly.conf needs shelly_server= and light= lines"; exit 1; }
run "set -e
$BUILD"'
exec /tmp/lights_cli "$@"' "$SERVER" /cache/shelly-test/shelly-key "${LAMPS[@]}" -- "${@:-status}"
