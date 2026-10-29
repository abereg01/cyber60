#!/usr/bin/env bash
# Run the real Spotify code (app/src/spotify.c) here on OMV against the real
# Spotify, before it goes on an SD card. The login is the Pi's own: open the
# address it prints on a phone on the home wifi, log in, tap Agree. The token
# it gets is kept in the deck cache (spotify-test/spotify.json, mode 600).
#
#   pi/tools/spotify-test.sh                       log in / show what's playing for 20 s
#   pi/tools/spotify-test.sh next                  playpause | next | prev | shuffle | repeat | like
#   pi/tools/spotify-test.sh list devices          playlists | albums | queue | devices
#   pi/tools/spotify-test.sh play <uri>            transfer <device id>
#   pi/tools/spotify-test.sh cover                 saves the current cover to pi/draw/cover-test.png
set -euo pipefail

PI=$(cd "$(dirname "$0")/.." && pwd)
WEST=${ZMK_WEST:-/srv/mergerfs/pool/docker/appdata/zmk-west}
CACHE=${DECK_CACHE:-$WEST/.cache/deck}
ID=$(sed -n 's/^spotify_client_id=//p' "$PI/image/files/deck.conf")
mkdir -p "$CACHE/spotify-test" && chmod 700 "$CACHE/spotify-test"
[ -w "${HOME:-/}" ] || export DOCKER_CONFIG=${DOCKER_CONFIG:-/srv/mergerfs/pool/docker/appdata/.docker-config}
docker build -q --network=host -t deck-tools:local -f "$PI/image/Dockerfile.tools" "$PI/image" >/dev/null
"$PI/tools/fetch-deps.sh"

ARGS=("$@")
[ "${1:-}" = cover ] && ARGS=(cover /cache/spotify-test/cover.ppm)
# The preview's LVGL (x86, no display drivers) does for the timers and images.
docker run --rm -i --network host --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PI":/pi -v "$CACHE":/cache \
    deck-tools:local bash -c '
set -e
L=/cache/deps/lvgl-9.3.0
gcc -O1 -w -DDECK_PREVIEW -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -I/pi/app -I/pi/app/src -I$L \
    -I/cache/deps/cJSON-1.7.18 /pi/app/test/spotify_cli.c /pi/app/src/spotify.c /cache/deps/cJSON-1.7.18/cJSON.c \
    /cache/preview-build/liblvgl.a -lcurl -ljpeg -lpthread -lm -o /tmp/spotify_cli
exec /tmp/spotify_cli "$0" /cache/spotify-test "${@}"' "$ID" "${ARGS[@]}"

if [ "${1:-}" = cover ] && [ -f "$CACHE/spotify-test/cover.ppm" ]; then
    docker run --rm --user "$(id -u):$(id -g)" -v "$CACHE/spotify-test":/c -v "$PI/draw":/o --entrypoint ffmpeg \
        ghcr.io/alexta69/metube:latest -loglevel error -y -i /c/cover.ppm /o/cover-test.png
    echo "cover: $PI/draw/cover-test.png"
fi
