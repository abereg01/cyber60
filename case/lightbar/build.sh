#!/usr/bin/env bash
# Monitor light bar, 700 mm: STLs + 3MF. Reads Jade's original files from ../../ (not in git).
#   ./build.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
WORK=${LIGHTBAR_CACHE:-/srv/mergerfs/pool/docker/appdata/zmk-west/.cache/lightbar}
export DOCKER_CONFIG=${DOCKER_CONFIG:-/srv/mergerfs/pool/docker/appdata/.docker-config}
docker image inspect cadtools:local >/dev/null 2>&1 ||
    docker build --network=host -t cadtools:local -f "$HERE/../3d/tools/Dockerfile.cadtools" "$HERE/../3d/tools"
IN=$HERE/original
[ -f "$IN/Monitor_Light_Bar.3mf" ] || IN=$ROOT
rm -rf "$WORK" && mkdir -p "$WORK"
docker run --rm -e MPLCONFIGDIR=/tmp --user "$(id -u):$(id -g)" -v "$HERE":/src:ro -v "$IN":/in:ro -v "$WORK":/out cadtools:local \
    python3 /src/lightbar.py /in /out >"$HERE/report.json"
cp "$WORK"/lightbar-pod.stl "$WORK"/lightbar-cap.stl "$HERE/"
cp "$WORK"/lightbar-tube-175.stl "$WORK"/lightbar-diffuser-233.stl "$WORK"/Monitor+Light+Bar+-+Diffuser-700.stl "$WORK"/Monitor_Light_Bar-700.3mf "$HERE/"
cat "$HERE/report.json"
