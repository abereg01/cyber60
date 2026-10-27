#!/usr/bin/env bash
# Build the Shoko keycap case: STLs + 3MF to print, PNG renders to look at.
#
#   ./build.sh        everything (needs Docker; renders use the clipsnag:local
#                     image's headless Chromium)
#   ./build.sh stl    just the print files
#
# case.py holds every dimension and tolerance. Change it, rerun, and check
# report.json: every "vs" check must stay 0 (no part touches another part or
# the stand-ins for the screen, XIAO, screws and wires).
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
CACHE=${SHOKO_CACHE:-/srv/mergerfs/pool/docker/appdata/zmk-west/.cache/shoko}
WORK=$CACHE/case
FONTS=$CACHE/ttf
UI=${SHOKO_UI:-/srv/mergerfs/pool/VARIOUS/keyboard/CYBER60/draw/shoko}
export DOCKER_CONFIG=${DOCKER_CONFIG:-/srv/mergerfs/pool/docker/appdata/.docker-config}

docker image inspect cadtools:local >/dev/null 2>&1 ||
    docker build --network=host -t cadtools:local -f "$HERE/tools/Dockerfile.cadtools" "$HERE/tools"

font=$FONTS/DelaGothicOne-Regular.ttf
[ -s "$font" ] || { mkdir -p "$FONTS"; curl -fsSL -o "$font" \
    https://raw.githubusercontent.com/google/fonts/main/ofl/delagothicone/DelaGothicOne-Regular.ttf; }

echo "==> model"
rm -rf "$WORK" && mkdir -p "$WORK"
docker run --rm --user "$(id -u):$(id -g)" -e MPLCONFIGDIR=/tmp -v "$HERE":/src:ro \
    -v "$FONTS":/fonts:ro -v "$WORK":/out cadtools:local python3 /src/case.py /out >"$HERE/report.json"
cp "$WORK"/shoko-*.stl "$WORK"/shoko-case.3mf "$HERE/"
cat "$HERE/report.json"

[ "${1:-all}" = stl ] && exit 0

echo "==> renders"
mkdir -p "$HERE/renders"
docker run --rm --user "$(id -u):$(id -g)" -v "$HERE/tools":/t:ro -v "$WORK":/out cadtools:local \
    python3 /t/mkspec.py /out /ui/1-base-typing.png
for spec in "$WORK"/spec-*.json; do
    docker run --rm --network host --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$HERE/tools":/t:ro -v "$WORK":/out -v "$HERE/renders":/renders \
        -v "$UI":/ui:ro -w /app clipsnag:local node /t/render.js "/out/$(basename "$spec")" |
        grep -E '^wrote|rror' || true
done
ls "$HERE/renders"
