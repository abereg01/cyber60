#!/usr/bin/env bash
# Regenerate the Pi screen's icons (LVGL A8 images) into
# pi/app/src/assets/deck_icons.c, plus a contact sheet pi/draw/icons.png.
#
# The generated .c file is committed, so this only needs to run when an icon
# changes. Runs in Docker (python + Pillow, cached in $CACHE).
set -euo pipefail

PI=$(cd "$(dirname "$0")/.." && pwd)
REPO=$(cd "$PI/.." && pwd)
CACHE=${DECK_CACHE:-/srv/mergerfs/pool/docker/appdata/zmk-west/.cache/deck}
mkdir -p "$CACHE/pylib" "$PI/draw"

dock() {
    docker run --rm --network host --user "$(id -u):$(id -g)" -e HOME=/tmp \
        -v "$CACHE":/c -v "$REPO":/repo "$@"
}

[ -d "$CACHE/pylib/PIL" ] ||
    dock python:3.12-slim pip install -q --target /c/pylib pillow==11.1.0

dock -e PYTHONPATH=/c/pylib python:3.12-slim python3 /repo/pi/tools/deck_icons.py \
    /repo/tools/shoko /repo/pi/app/src/assets/deck_icons.c /repo/pi/draw/icons.png

echo "done: $PI/app/src/assets/deck_icons.c"
