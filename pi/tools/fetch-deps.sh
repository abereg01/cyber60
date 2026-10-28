#!/usr/bin/env bash
# Fetch the third-party sources the Pi app builds against, pinned, into
# $DECK_DEPS (default: the deck cache; GitHub Actions points it at its
# workspace). Prints nothing when they're already there.
#   lvgl   9.3.0  (MIT)  the UI library, same version as the Prospector's ZMK
set -euo pipefail

DEPS=${DECK_DEPS:-${DECK_CACHE:-/srv/mergerfs/pool/docker/appdata/zmk-west/.cache/deck}/deps}
mkdir -p "$DEPS"

fetch() { # name url
    [ -d "$DEPS/$1" ] && return
    echo "==> fetching $1"
    mkdir -p "$DEPS/$1.tmp"
    curl -fsSL "$2" | tar -xz -C "$DEPS/$1.tmp" --strip-components=1
    mv "$DEPS/$1.tmp" "$DEPS/$1"
}

fetch lvgl-9.3.0 https://github.com/lvgl/lvgl/archive/refs/tags/v9.3.0.tar.gz
