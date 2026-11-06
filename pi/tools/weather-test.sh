#!/usr/bin/env bash
# Run the real weather code (app/src/weather.c) here on OMV, before it goes on
# an SD card.
#
#   pi/tools/weather-test.sh              a made-up forecast (app/test/weather/rain.json):
#                                         now, today's high/low, minutes to rain;
#                                         then SMHI's real forecast for Stockholm city centre
#   pi/tools/weather-test.sh <lat,lon>    the real forecast for that spot instead
#
# Exit code 0 = the checks passed and SMHI answered.
set -euo pipefail

PI=$(cd "$(dirname "$0")/.." && pwd)
WEST=${ZMK_WEST:-/srv/mergerfs/pool/docker/appdata/zmk-west}
CACHE=${DECK_CACHE:-$WEST/.cache/deck}
WHERE=${1:-59.3293,18.0686} # Sergels torg: public, not the desk's spot

[ -w "${HOME:-/}" ] || export DOCKER_CONFIG=${DOCKER_CONFIG:-/srv/mergerfs/pool/docker/appdata/.docker-config}
docker build -q --network=host -t deck-tools:local -f "$PI/image/Dockerfile.tools" "$PI/image" >/dev/null
"$PI/tools/fetch-deps.sh"
[ -f "$CACHE/preview-build/liblvgl.a" ] || { echo "Run pi/tools/preview.sh once first (it builds LVGL)."; exit 1; }

# The preview's LVGL (x86, no display drivers) does for the timer. TZ: the
# Pi's zone as a POSIX rule, so it works without tzdata in the container.
docker run --rm -i --network host --user "$(id -u):$(id -g)" -e HOME=/tmp -e TZ=CET-1CEST,M3.5.0,M10.5.0/3 \
    -v "$PI":/pi -v "$CACHE":/cache deck-tools:local bash -c '
set -e
L=/cache/deps/lvgl-9.3.0
gcc -O1 -Wall -Wextra -Wno-unused-parameter -DDECK_PREVIEW -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE \
    -I/pi/app -I/pi/app/src -I$L -I/cache/deps/cJSON-1.7.18 /pi/app/test/weather_cli.c /pi/app/src/weather.c \
    /cache/deps/cJSON-1.7.18/cJSON.c /cache/preview-build/liblvgl.a -lcurl -lpthread -lm -o /tmp/weather_cli
echo "==> a made-up forecast"
/tmp/weather_cli test /pi/app/test/weather/rain.json
echo "==> SMHI, for real"
exec /tmp/weather_cli live "$0"' "$WHERE"
