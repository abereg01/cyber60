#!/usr/bin/env bash
# Regenerate the Shoko screen layout's fonts and icons (C files for LVGL) into
# boards/shields/prospector_screen/src/layouts/shoko/assets/.
#
# The generated .c files are committed, so this only needs to run when a font
# size, glyph set or icon changes. Everything runs in Docker (node for
# lv_font_conv, python for Pillow); downloads are cached in $CACHE.
#
# Fonts (both SIL Open Font License, from github.com/google/fonts):
#   Dela Gothic One           big numbers, layer titles, katakana watermark
#   M PLUS Rounded 1c ExtraBold  small labels
set -euo pipefail

REPO=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$REPO/boards/shields/prospector_screen/src/layouts/shoko/assets
CACHE=${SHOKO_CACHE:-/srv/mergerfs/pool/docker/appdata/zmk-west/.cache/shoko}
mkdir -p "$CACHE/ttf" "$CACHE/npm" "$CACHE/pylib" "$OUT"

fetch() {
    local f=$CACHE/ttf/$(basename "$1")
    [ -s "$f" ] || curl -fsSL -o "$f" "https://raw.githubusercontent.com/google/fonts/main/$1"
}
fetch ofl/delagothicone/DelaGothicOne-Regular.ttf
fetch ofl/mplusrounded1c/MPLUSRounded1c-ExtraBold.ttf

dock() {
    docker run --rm --network host --user "$(id -u):$(id -g)" -e HOME=/tmp \
        -v "$CACHE":/c -v "$OUT":/out -v "$REPO/tools/shoko":/tools:ro "$@"
}

grep -qs "\"version\": \"1.5.3\"" "$CACHE/npm/node_modules/lv_font_conv/package.json" ||
    dock -w /c/npm -e npm_config_cache=/c/npm/.cache node:22-alpine \
        sh -c 'npm init -y >/dev/null && npm install --no-audit --no-fund lv_font_conv@1.5.3'

[ -d "$CACHE/pylib/PIL" ] ||
    dock python:3.12-slim pip install -q --target /c/pylib pillow==11.1.0

DELA=/c/ttf/DelaGothicOne-Regular.ttf
ROUND=/c/ttf/MPLUSRounded1c-ExtraBold.ttf
KANA='ベースエフヌシテムクロルゲー'

echo "==> fonts"
dock node:22-alpine sh -ec "
conv() { name=\$1; shift; /c/npm/node_modules/.bin/lv_font_conv --format lvgl --no-compress --lv-include lvgl.h --lv-font-name \$name -o /out/\$name.c \"\$@\"; echo \"    \$name\"; }
conv shoko_dela_64  --font $DELA  --size 64  --bpp 4 -r 0x30-0x39,0x41-0x5A
conv shoko_dela_36  --font $DELA  --size 36  --bpp 4 -r 0x20,0x2D,0x30-0x39,0x41-0x5A
conv shoko_dela_22  --font $DELA  --size 22  --bpp 4 -r 0x20,0x25,0x2D,0x30-0x39,0x41-0x5A --symbols '・スクービー'
conv shoko_kana_120 --font $DELA  --size 120 --bpp 2 --symbols '$KANA'
conv shoko_ui_16    --font $ROUND --size 16  --bpp 4 -r 0x20-0x7E
conv shoko_ui_12    --font $ROUND --size 12  --bpp 4 -r 0x20-0x7E
"

echo "==> icons"
dock -e PYTHONPATH=/c/pylib python:3.12-slim python3 /tools/icons.py /out/shoko_icons.c

echo "done: $OUT"
