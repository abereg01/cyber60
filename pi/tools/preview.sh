#!/usr/bin/env bash
# Render the Pi desk screen on this box, without a Pi: compiles the real screen
# code (pi/app/src + assets) against LVGL, plays the session scripted in
# pi/app/preview/main.c and writes
#   pi/draw/*.png      stills (scenes, the dog, Spotify: login, player, lists, wifi setup)
#   pi/draw/deck.mp4   the whole session at 30 fps
#
# Usage: pi/tools/preview.sh [win|mac]   (modifier icon style, default win)
# Needs gcc on the host and Docker for ffmpeg.
set -euo pipefail

PI=$(cd "$(dirname "$0")/.." && pwd)
REPO=$(cd "$PI/.." && pwd)
WEST=${ZMK_WEST:-/srv/mergerfs/pool/docker/appdata/zmk-west}
CACHE=${DECK_CACHE:-$WEST/.cache/deck}
LVGL=${LVGL:-$CACHE/deps/lvgl-9.3.0} # the Pi build's, from fetch-deps.sh
FONTS=${DECK_FONTS:-$WEST/.cache/shoko/ttf} # fetched by tools/shoko/gen-assets.sh
SHARE=$CACHE/share # what the Pi has in /opt/deck/share: fonts + dog.png
FFMPEG_IMAGE=${FFMPEG_IMAGE:-ghcr.io/alexta69/metube:latest}
APP=$PI/app
SHOKO=$REPO/boards/shields/prospector_screen/src/layouts/shoko
B=$CACHE/preview-build
OUT=$PI/draw

CFLAGS=(-O2 -w -I"$APP" -I"$APP/src" -I"$LVGL" -I"$SHOKO" -DLV_CONF_INCLUDE_SIMPLE
    -DLV_LVGL_H_INCLUDE_SIMPLE -DDECK_PREVIEW)

"$PI/tools/fetch-deps.sh"

# LVGL itself is built once (again when lv_conf.h or the LVGL path changes).
if [ ! -f "$B/liblvgl.a" ] || [ "$APP/lv_conf.h" -nt "$B/liblvgl.a" ] ||
    [ "$(cat "$B/lvgl-path" 2>/dev/null)" != "$LVGL" ]; then
    echo "==> building LVGL (once)"
    rm -rf "$B/obj" "$B/liblvgl.a" && mkdir -p "$B/obj"
    export B LVGL
    export CF="${CFLAGS[*]}"
    find "$LVGL/src" -name '*.c' -print0 |
        xargs -0 -P"$(nproc)" -I{} sh -c 'gcc $CF -c "$1" -o "$B/obj/$(echo "$1" | md5sum | cut -c1-16).o"' _ {}
    ar rcs "$B/liblvgl.a" "$B"/obj/*.o
    echo "$LVGL" >"$B/lvgl-path"
fi

echo "==> building preview"
# The app's code minus wifi.c and spotify.c: preview/wifi_fake.c and
# spotify_fake.c stand in (OMV has no wifi; the preview shows made-up music).
SRCS=$(ls "$APP"/src/*.c | grep -vE '/(wifi|spotify)\.c$')
gcc "${CFLAGS[@]}" "$APP/preview/main.c" "$APP/preview/wifi_fake.c" "$APP/preview/spotify_fake.c" $SRCS \
    "$APP"/src/assets/*.c \
    "$B/liblvgl.a" -lm -o "$B/preview"

mkdir -p "$SHARE"
ln -sf "$FONTS"/DelaGothicOne-Regular.ttf "$FONTS"/MPLUSRounded1c-ExtraBold.ttf "$APP/assets/dog.png" "$SHARE/"

echo "==> rendering"
rm -f "$OUT"/[0-9]*-*.png "$OUT"/*.ppm "$OUT"/deck.mp4 # icons.png, dog.png: the gen scripts'
mkdir -p "$OUT"
"$B/preview" "$OUT" "$SHARE" "${1:-win}" |
    docker run -i --rm --user "$(id -u):$(id -g)" -v "$OUT":/o --entrypoint ffmpeg "$FFMPEG_IMAGE" \
        -loglevel error -y -f rawvideo -pixel_format rgb565le -video_size 800x480 -framerate 30 -i - \
        -c:v libx264 -pix_fmt yuv420p -crf 16 /o/deck.mp4

echo "==> stills"
docker run --rm --user "$(id -u):$(id -g)" -v "$OUT":/o -w /o --entrypoint sh "$FFMPEG_IMAGE" -ec '
for f in *.ppm; do ffmpeg -loglevel error -y -i "$f" "${f%.ppm}.png"; done'
rm -f "$OUT"/*.ppm
ls -la "$OUT"
