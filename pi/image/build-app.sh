#!/usr/bin/env bash
# Cross-compile the screen for the Pi (arm64, Raspberry Pi OS Trixie) and pack
# what goes into /opt/deck:
#   pi/build/opt-deck/          deck, deck-update.sh, share/ (fonts, dog.png)
#   pi/build/deck-update.tar.gz the same, to drop on the SD card's boot partition
# Runs inside the deck-tools container (pi/image/build.sh starts it).
set -euo pipefail

PI=$(cd "$(dirname "$0")/.." && pwd)
REPO=$(cd "$PI/.." && pwd)
CACHE=${DECK_CACHE:?}
LVGL=$CACHE/deps/lvgl-9.3.0
FONTS=$CACHE/fonts
OUT=$PI/build
APP=$PI/app
SHOKO=$REPO/boards/shields/prospector_screen/src/layouts/shoko
CC=aarch64-linux-gnu-gcc

CFLAGS=(-O2 -ffunction-sections -fdata-sections -I"$APP" -I"$APP/src" -I"$APP/pi" -I"$LVGL"
    -I"$SHOKO" -I/usr/include/libdrm -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE)

# Fonts (SIL OFL), the same files the Prospector's assets come from.
mkdir -p "$FONTS"
for f in ofl/delagothicone/DelaGothicOne-Regular.ttf ofl/mplusrounded1c/MPLUSRounded1c-ExtraBold.ttf; do
    [ -s "$FONTS/$(basename "$f")" ] ||
        curl -fsSL -o "$FONTS/$(basename "$f")" "https://raw.githubusercontent.com/google/fonts/main/$f"
done

# LVGL, once (again when lv_conf.h changes).
L=$CACHE/pi-lvgl
if [ ! -f "$L/liblvgl.a" ] || [ "$APP/lv_conf.h" -nt "$L/liblvgl.a" ]; then
    echo "==> building LVGL for arm64 (once)"
    rm -rf "$L" && mkdir -p "$L/obj"
    export L CC
    export CF="${CFLAGS[*]}"
    find "$LVGL/src" -name '*.c' -print0 |
        xargs -0 -P"$(nproc)" -I{} sh -c '$CC $CF -w -c "$1" -o "$L/obj/$(echo "$1" | md5sum | cut -c1-16).o"' _ {}
    aarch64-linux-gnu-ar rcs "$L/liblvgl.a" "$L"/obj/*.o
fi

# Staged on the container's own disk: tar on the mergerfs pool can see a
# directory "change as we read it" and fail.
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
mkdir -p "$S/opt-deck/share"

echo "==> building deck"
$CC "${CFLAGS[@]}" -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
    "$APP"/src/*.c "$APP"/src/assets/*.c "$APP"/pi/*.c "$L/liblvgl.a" \
    -Wl,--gc-sections -ldrm -lpthread -lm -o "$S/opt-deck/deck"
aarch64-linux-gnu-strip "$S/opt-deck/deck"
install -m 755 "$PI/image/files/deck-update.sh" "$S/opt-deck/"
install -m 644 "$FONTS"/*.ttf "$APP/assets/dog.png" "$S/opt-deck/share/"

mkdir -p "$OUT"
tar -czf "$S/deck-update.tar.gz" -C "$S/opt-deck" --owner=0 --group=0 .
rm -rf "$OUT/opt-deck"
cp -r "$S/opt-deck" "$S/deck-update.tar.gz" "$OUT/"
ls -la "$OUT/opt-deck" "$OUT/deck-update.tar.gz"
