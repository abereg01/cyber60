#!/usr/bin/env bash
# Build the Cyber60 firmware locally in ZMK's Docker image, with the real
# password macros if config/secrets/passwords.dtsi exists (CI never has them).
#
# Usage: tools/build.sh [keyboard|dongle|reset|update|all]   (default: all)
#   keyboard  cyber60@rev_e                        → firmware/cyber60.uf2
#   dongle    xiao_ble + prospector_dongle(_screen)    → firmware/dongle.uf2
#   reset     settings_reset for both boards        → firmware/reset-*.uf2
#   update    refresh ZMK/Zephyr sources (west update)
#
# The west workspace (ZMK + Zephyr, a few GB) lives outside the repo, in
# $ZMK_WEST (default below). firmware/ is gitignored and kept private (700),
# because dongle.uf2 contains the passwords in plain bytes.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
WEST=${ZMK_WEST:-/srv/mergerfs/pool/docker/appdata/zmk-west}
IMAGE=zmkfirmware/zmk-build-arm:stable

# HOME inside the workspace, so `west zephyr-export` (which registers Zephyr
# under ~/.cmake) persists between container runs.
run() {
    mkdir -p "$WEST/.home"
    docker run --rm --network host --user "$(id -u):$(id -g)" -e HOME=/w/.home \
        -v "$WEST":/w -v "$REPO":/repo -w /w "$IMAGE" "$@"
}

update() {
    mkdir -p "$WEST/config"
    cp "$REPO/config/west.yml" "$WEST/config/west.yml"
    [ -d "$WEST/.west" ] || run west init -l config
    run sh -c 'west update --fetch-opt=--filter=tree:0 && west zephyr-export'
}

# build <output name> <board> [shield] [snippet]
build() {
    local name=$1 board=$2 shield=${3:-} snippet=${4:-}
    local args=(west build -p auto -s zmk/app -d "build/$name" -b "$board")
    [ -n "$snippet" ] && args+=(-S "$snippet")
    args+=(-- -DZMK_CONFIG=/repo/config -DZMK_EXTRA_MODULES=/repo)
    [ -n "$shield" ] && args+=("-DSHIELD=$shield")

    echo "==> $name ($board${shield:+ / $shield})"
    run "${args[@]}"
    install -m 600 "$WEST/build/$name/zephyr/zmk.uf2" "$REPO/firmware/$name.uf2"
    echo "    firmware/$name.uf2"
}

mkdir -p "$REPO/firmware" && chmod 700 "$REPO/firmware"
cmp -s "$REPO/config/west.yml" "$WEST/config/west.yml" 2>/dev/null || update

if [ -f "$REPO/config/secrets/passwords.dtsi" ]; then
    echo "Password macros: real (config/secrets/passwords.dtsi)"
else
    echo "Password macros: placeholders (run tools/make-passwords.py first for the real ones)"
fi

case "${1:-all}" in
    keyboard) build cyber60 cyber60@rev_e ;;
    dongle)   build dongle xiao_ble//zmk "prospector_dongle prospector_screen" studio-rpc-usb-uart ;;
    reset)    build reset-cyber60 cyber60@rev_e settings_reset
              build reset-dongle xiao_ble//zmk settings_reset ;;
    update)   update ;;
    all)      "$0" keyboard && "$0" dongle && "$0" reset ;;
    *)        echo "usage: $0 [keyboard|dongle|reset|update|all]" >&2; exit 2 ;;
esac
