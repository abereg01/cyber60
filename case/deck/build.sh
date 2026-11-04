#!/usr/bin/env bash
# Build the desk-screen case: STLs + 3MF to print, PNG renders to look at.
#
#   ./build.sh          everything (needs Docker; renders use the clipsnag:local
#                       image's headless Chromium)
#   ./build.sh stl      just the print files + report.json
#   DECK_MAT=bottom ./build.sh   print files for the bottom-weighted mat
#
# case.py holds every dimension and tolerance. Change it, rerun, and check
# report.json: every "vs" check must stay 0 (no part touches another part or
# the stand-ins for the display, Pi, plug, cable, screws and SD card).
# Tools (Dockerfile, render.js) are shared with the Shoko case in ../3d/tools.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
TOOLS=$HERE/../3d/tools
WORK=${DECK_CACHE:-/srv/mergerfs/pool/docker/appdata/zmk-west/.cache/deck}/case
UI=${DECK_UI:-$HERE/../../pi/draw}
export DOCKER_CONFIG=${DOCKER_CONFIG:-/srv/mergerfs/pool/docker/appdata/.docker-config}

docker image inspect cadtools:local >/dev/null 2>&1 ||
    docker build --network=host -t cadtools:local -f "$TOOLS/Dockerfile.cadtools" "$TOOLS"

echo "==> model"
rm -rf "$WORK" && mkdir -p "$WORK"
docker run --rm --user "$(id -u):$(id -g)" -e DECK_MAT="${DECK_MAT:-even}" -v "$HERE":/src:ro \
    -v "$WORK":/out cadtools:local python3 /src/case.py /out >"$HERE/report.json"
cp "$WORK"/deck-*.stl "$WORK"/deck-case.3mf "$HERE/"
cat "$HERE/report.json"

[ "${1:-all}" = stl ] && exit 0

echo "==> renders"
mkdir -p "$HERE/renders"
for spec in "$WORK"/spec-*.json; do
    docker run --rm --network host --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$TOOLS":/t:ro \
        -v "$WORK":/out -v "$HERE/renders":/renders -v "$UI":/ui:ro -w /app clipsnag:local \
        node /t/render.js "/out/$(basename "$spec")" | grep -E '^wrote|rror' || true
done
ls "$HERE/renders"
