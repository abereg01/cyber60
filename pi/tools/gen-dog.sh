#!/usr/bin/env bash
# Render the Shiba puppy in Blender and pack it for the screen:
#   pi/app/assets/dog.png          sprite sheet, loaded at runtime (pet.c)
#   pi/app/src/assets/dog_sheet.h  its layout
#   pi/draw/dog.png                every frame on white and on night, to look at
#
# Both outputs are committed, so this only needs to run when the puppy changes
# (pi/tools/puppy/puppy.py). Blender 4.5 LTS is downloaded once into the
# cache and run headless in Docker (Cycles on the CPU, ~20 s for every frame);
# Pillow packs the frames.
#
# Usage: pi/tools/gen-dog.sh [pose prefix]   (prefix: render only those, for trying things)
set -euo pipefail

PI=$(cd "$(dirname "$0")/.." && pwd)
CACHE=${DECK_CACHE:-/srv/mergerfs/pool/docker/appdata/zmk-west/.cache/deck}
BLENDER=blender-4.5.15-linux-x64
FRAMES=$CACHE/puppy-frames
mkdir -p "$CACHE/blender" "$CACHE/pylib" "$PI/draw" "$PI/app/assets"
[ -w "${HOME:-/}" ] || export DOCKER_CONFIG=${DOCKER_CONFIG:-/srv/mergerfs/pool/docker/appdata/.docker-config}

if [ ! -x "$CACHE/blender/$BLENDER/blender" ]; then
    echo "==> downloading Blender (once)"
    curl -fsSL https://download.blender.org/release/Blender4.5/$BLENDER.tar.xz | tar -xJ -C "$CACHE/blender"
fi
docker build -q --network=host -t deck-blender:local "$PI/tools/puppy" >/dev/null

dock() {
    docker run --rm --network host --user "$(id -u):$(id -g)" -e HOME=/tmp "$@"
}

echo "==> rendering"
rm -rf "$FRAMES" && mkdir -p "$FRAMES"
dock -v "$CACHE/blender":/blender:ro -v "$PI/tools/puppy":/src:ro -v "$FRAMES":/out deck-blender:local \
    /blender/$BLENDER/blender -b --factory-startup -P /src/puppy.py -- /out "${1:-}" |
    grep -E "Error|Traceback|line [0-9]" || true
[ -z "${1:-}" ] || { echo "frames in $FRAMES"; exit 0; }

echo "==> packing"
[ -d "$CACHE/pylib/PIL" ] || dock -v "$CACHE":/c python:3.12-slim pip install -q --target /c/pylib pillow==11.1.0
dock -e PYTHONPATH=/c/pylib -v "$CACHE":/c -v "$PI":/pi -v "$FRAMES":/f python:3.12-slim \
    python3 /pi/tools/puppy/sheet.py /f /pi/app/assets/dog.png /pi/app/src/assets/dog_sheet.h /pi/draw/dog.png
echo "done: $PI/app/assets/dog.png"
