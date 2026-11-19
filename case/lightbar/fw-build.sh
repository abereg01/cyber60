#!/usr/bin/env bash
# Light bar firmware: host tests, then a PlatformIO build in Docker.
#   ./fw-build.sh          tests + build   (first build downloads ~600 MB; cache on the pool, not /tmp)
#   ./fw-build.sh test     host tests only
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
CACHE=${LIGHTBAR_PIO_CACHE:-/srv/mergerfs/pool/docker/appdata/zmk-west/.cache/pio-lightbar}
export DOCKER_CONFIG=${DOCKER_CONFIG:-/srv/mergerfs/pool/docker/appdata/.docker-config}
echo "==> host tests"
docker run --rm -v "$HERE/fw":/w:ro -w /w gcc:13 sh -c 'g++ -std=c++17 -Wall -Isrc test/test_light.cpp -o /tmp/t && /tmp/t'
[ "${1:-all}" = test ] && exit 0
echo "==> firmware build"
mkdir -p "$CACHE/home" "$CACHE/work"
docker run --rm --network host --user "$(id -u):$(id -g)" -e HOME=/pio -e PLATFORMIO_CORE_DIR=/pio/core \
    -v "$CACHE/home":/pio -v "$HERE/fw":/src:ro -v "$CACHE/work":/work -w /work python:3.12-slim sh -c '
    export PATH=$PATH:/pio/.local/bin
    pip install --quiet --user platformio >/dev/null 2>&1 || pip install --user platformio
    rm -rf /work/* && cp -r /src/. /work/ && pio run 2>&1 | tail -25'
echo "firmware.bin: $CACHE/work/.pio/build/c3zero/firmware.bin"
