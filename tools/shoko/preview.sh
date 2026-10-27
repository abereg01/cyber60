#!/usr/bin/env bash
# Render the Shoko screen on this box, without flashing anything: compiles the
# real screen code (layouts/shoko/shoko_ui.c + assets) against the LVGL from the
# ZMK west workspace, plays the session scripted in preview/main.c and writes
#   draw/shoko/*.png        stills (one per scene)
#   draw/shoko/shoko.mp4    the whole session at 30 fps, 2x size
#   draw/shoko/shoko.gif    same, smaller
#
# Usage: tools/shoko/preview.sh [win|mac]   (modifier icon style, default win)
# Needs gcc on the host and Docker for ffmpeg.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/../.." && pwd)
WEST=${ZMK_WEST:-/srv/mergerfs/pool/docker/appdata/zmk-west}
LVGL=$WEST/modules/lib/gui/lvgl
CACHE=${SHOKO_CACHE:-$WEST/.cache/shoko}
FFMPEG_IMAGE=${FFMPEG_IMAGE:-ghcr.io/alexta69/metube:latest}
SRC=$REPO/boards/shields/prospector_screen/src/layouts/shoko
P=$REPO/tools/shoko/preview
B=$CACHE/preview-build
OUT=$REPO/draw/shoko

CFLAGS=(-O1 -w -I"$P" -I"$LVGL" -I"$SRC" -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE)

# LVGL itself is built once (again only when lv_conf.h changes).
if [ ! -f "$B/liblvgl.a" ] || [ "$P/lv_conf.h" -nt "$B/liblvgl.a" ]; then
    echo "==> building LVGL (once)"
    rm -rf "$B/obj" && mkdir -p "$B/obj"
    export B LVGL
    export CF="${CFLAGS[*]}"
    find "$LVGL/src" -name '*.c' -print0 |
        xargs -0 -P"$(nproc)" -I{} sh -c 'gcc $CF -c "$1" -o "$B/obj/$(echo "$1" | md5sum | cut -c1-16).o"' _ {}
    ar rcs "$B/liblvgl.a" "$B"/obj/*.o
fi

echo "==> building preview"
gcc "${CFLAGS[@]}" "$P/main.c" "$SRC/shoko_ui.c" "$SRC"/assets/*.c "$B/liblvgl.a" -lm -o "$B/preview"

echo "==> rendering"
rm -rf "$OUT" && mkdir -p "$OUT/frames"
"$B/preview" "$OUT" "${1:-win}"

echo "==> encoding"
docker run --rm --user "$(id -u):$(id -g)" -v "$OUT":/o -w /o --entrypoint sh "$FFMPEG_IMAGE" -ec '
for f in *.ppm; do ffmpeg -loglevel error -y -i "$f" -vf scale=560:480:flags=neighbor "${f%.ppm}.png"; done
ffmpeg -loglevel error -y -framerate 30 -i frames/f%05d.ppm -vf scale=560:480:flags=neighbor \
    -c:v libx264 -pix_fmt yuv420p -crf 16 shoko.mp4
ffmpeg -loglevel error -y -framerate 30 -i frames/f%05d.ppm \
    -vf "fps=25,split[a][b];[a]palettegen=max_colors=96[p];[b][p]paletteuse=dither=none" shoko.gif
# One contact sheet per layer change: the 18 frames (600 ms) after it.
n=0
while read -r name start; do
    n=$((n + 1))
    ffmpeg -loglevel error -y -start_number "$start" -i frames/f%05d.ppm -frames:v 1 \
        -vf "tile=6x3:padding=4:color=0x808080" "wipe-$n-$name.png"
done < marks.txt
'
rm -rf "$OUT"/frames "$OUT"/*.ppm "$OUT"/marks.txt
ls -la "$OUT"
