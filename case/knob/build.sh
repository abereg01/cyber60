#!/usr/bin/env bash
# Desk knob CONCEPT: model + renders (no print files yet).
#
#   ./build.sh          model, mock screen, all renders  (Docker; renders use
#                       the clipsnag:local image's headless Chromium)
#   ./build.sh model    just the model + report.json
#
# knob.py holds every dimension (top of the file). Change, rerun, look at
# renders/*.png. report.json: every check must stay 0 (no parts overlap).
# The mock screen comes from ui.py. Tools are shared with ../3d/tools.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
TOOLS=$HERE/../3d/tools
WORK=${KNOB_CACHE:-/srv/mergerfs/pool/docker/appdata/zmk-west/.cache/knob}/case
FONTS=${SHOKO_FONTS:-/srv/mergerfs/pool/docker/appdata/zmk-west/.cache/shoko/ttf} # from tools/shoko/gen-assets.sh
export DOCKER_CONFIG=${DOCKER_CONFIG:-/srv/mergerfs/pool/docker/appdata/.docker-config}

docker image inspect cadtools:local >/dev/null 2>&1 ||
    docker build --network=host -t cadtools:local -f "$TOOLS/Dockerfile.cadtools" "$TOOLS"

echo "==> model"
rm -rf "$WORK" && mkdir -p "$WORK/ui"
docker run --rm --user "$(id -u):$(id -g)" -v "$HERE":/src:ro -v "$WORK":/out cadtools:local \
    python3 /src/knob.py /out >"$HERE/report.json"
cat "$HERE/report.json"
[ "${1:-all}" = model ] && exit 0

echo "==> mock screen"
docker run --rm --user "$(id -u):$(id -g)" -v "$HERE":/src:ro -v "$WORK":/out -v "$FONTS":/fonts:ro \
    cadtools:local python3 /src/ui.py /out/ui/screen.png /fonts

echo "==> renders"
mkdir -p "$HERE/renders"
for spec in "$WORK"/spec-*.json; do
    docker run --rm --network host --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$TOOLS":/t:ro \
        -v "$WORK":/out -v "$HERE/renders":/renders -v "$WORK/ui":/ui:ro -w /app clipsnag:local \
        node /t/render.js "/out/$(basename "$spec")" | grep -E '^wrote|rror' || true
done
cp "$WORK/ui/screen.png" "$HERE/renders/screen-ui.png"
rm -f "$HERE/renders/section.png"
docker run --rm --user "$(id -u):$(id -g)" -v "$HERE":/src:ro -v "$WORK":/out -v "$HERE/renders":/renders \
    -v "$FONTS":/fonts:ro cadtools:local python3 /src/label.py /out /renders /fonts
ls "$HERE/renders"
