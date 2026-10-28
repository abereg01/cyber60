#!/usr/bin/env bash
# Redraw the Shiba: pi/app/assets/dog.png (sprite sheet, loaded at runtime),
# pi/app/src/assets/dog_sheet.h (its layout) and a preview pi/draw/dog.png.
#
# Both outputs are committed, so this only needs to run when the dog changes
# (pi/tools/dog.py). Runs in Docker (python + Pillow, cached in $CACHE).
set -euo pipefail

PI=$(cd "$(dirname "$0")/.." && pwd)
CACHE=${DECK_CACHE:-/srv/mergerfs/pool/docker/appdata/zmk-west/.cache/deck}
mkdir -p "$CACHE/pylib" "$PI/draw" "$PI/app/assets"

dock() {
    docker run --rm --network host --user "$(id -u):$(id -g)" -e HOME=/tmp \
        -v "$CACHE":/c -v "$PI":/pi "$@"
}

[ -d "$CACHE/pylib/PIL" ] ||
    dock python:3.12-slim pip install -q --target /c/pylib pillow==11.1.0

dock -e PYTHONPATH=/c/pylib python:3.12-slim python3 /pi/tools/dog.py \
    /pi/app/assets/dog.png /pi/app/src/assets/dog_sheet.h /pi/draw/dog.png

echo "done: $PI/app/assets/dog.png"
