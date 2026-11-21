#!/usr/bin/env bash
# Build the Magic Trackpad tray with wrist rest: STLs + 3MF to print, PNG renders.
#
#   ./build.sh        everything (needs Docker; renders use the clipsnag:local
#                     image's headless Chromium)
#   ./build.sh stl    just the print files + report.json
#
# tray.py holds every dimension. Change it, rerun, and read report.json.
# Tools (Dockerfile, render.js) are shared with the Shoko case in ../3d/tools.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
TOOLS=$HERE/../3d/tools
FONTS=${SHOKO_FONTS:-/srv/mergerfs/pool/docker/appdata/zmk-west/.cache/shoko/ttf}
WORK=${TRAY_CACHE:-/srv/mergerfs/pool/docker/appdata/zmk-west/.cache/trackpad}/case
export DOCKER_CONFIG=${DOCKER_CONFIG:-/srv/mergerfs/pool/docker/appdata/.docker-config}

docker image inspect cadtools:local >/dev/null 2>&1 ||
    docker build --network=host -t cadtools:local -f "$TOOLS/Dockerfile.cadtools" "$TOOLS"

echo "==> model"
rm -rf "$WORK" && mkdir -p "$WORK"
docker run --rm --user "$(id -u):$(id -g)" -v "$HERE":/src:ro -v "$HERE/../..":/repo:ro -v "$FONTS":/fonts:ro -v "$WORK":/out cadtools:local \
    python3 /src/tray.py /out /repo >"$HERE/report.json"
cp "$WORK"/trackpad-wordmark-blue.stl "$WORK"/trackpad-tray.stl "$WORK"/trackpad-tray.3mf "$HERE/"
cat "$HERE/report.json"

[ "${1:-all}" = stl ] && exit 0

echo "==> renders"
mkdir -p "$HERE/renders"
for spec in "$WORK"/spec-*.json; do
    docker run --rm --network host --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$TOOLS":/t:ro \
        -v "$WORK":/out -v "$HERE/renders":/renders -w /app clipsnag:local \
        node /t/render.js "/out/$(basename "$spec")" | grep -E '^wrote|rror' || true
done
ls "$HERE/renders"
