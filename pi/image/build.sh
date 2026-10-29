#!/usr/bin/env bash
# Build the Pi's SD card image and the update file, in the deck-tools
# container (Debian Trixie with the arm64 cross compiler), the same way
# GitHub Actions does:
#   pi/build/deck-<date>.img.xz   a whole new card: Raspberry Pi Imager, "Use custom"
#   pi/build/deck-update.tar.gz   just the screen: copy onto the DECK drive (or the card's bootfs)
#
# Usage: pi/image/build.sh [update]   (update: skip the image, only the update file)
set -euo pipefail

PI=$(cd "$(dirname "$0")/.." && pwd)
REPO=$(cd "$PI/.." && pwd)
CACHE=${DECK_CACHE:-/srv/mergerfs/pool/docker/appdata/zmk-west/.cache/deck}
mkdir -p "$CACHE" "$PI/build"
[ -w "${HOME:-/}" ] || export DOCKER_CONFIG=${DOCKER_CONFIG:-/srv/mergerfs/pool/docker/appdata/.docker-config}

docker build -q --network=host -t deck-tools:local -f "$PI/image/Dockerfile.tools" "$PI/image" >/dev/null

STEPS="pi/tools/fetch-deps.sh && pi/image/build-app.sh"
[ "${1:-}" = update ] || STEPS="$STEPS && pi/image/build-image.sh"
docker run --rm --network host --user "$(id -u):$(id -g)" -e HOME=/tmp -e DECK_CACHE=/cache -e GITHUB_SHA \
    -v "$REPO":/repo -v "$CACHE":/cache -w /repo deck-tools:local bash -c "$STEPS"
