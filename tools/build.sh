#!/usr/bin/env bash
# Build the Cyber60 firmware locally in ZMK's Docker image, with the real
# password macros if config/secrets/passwords.dtsi exists (CI never has them).
#
# Usage: tools/build.sh [keyboard|dongle|reset|keyboard-pi|update|all]   (default: all)
#   keyboard     cyber60@rev_e, the Prospector's peripheral  → firmware/cyber60.uf2
#   dongle       xiao_ble + prospector_dongle(_screen)       → firmware/dongle.uf2
#   reset        settings_reset for both boards              → firmware/reset-*.uf2
#   keyboard-pi  cyber60@rev_e standalone, for the Pi desk
#                screen (not part of "all")                  → firmware/cyber60-pi.uf2
#   update       refresh ZMK/Zephyr sources (west update)
#
# The west workspace (ZMK + Zephyr, a few GB) lives outside the repo, in
# $ZMK_WEST (default below). firmware/ is gitignored and kept private (700),
# because dongle.uf2 and cyber60-pi.uf2 contain the passwords in plain bytes.
# tools/flash.sh flashes the keyboard through the Pi when it hangs off the Pi.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
WEST=${ZMK_WEST:-/srv/mergerfs/pool/docker/appdata/zmk-west}
IMAGE=zmkfirmware/zmk-build-arm:stable

# The standalone keyboard for the Pi = config/cyber60.conf with these on top
# (command-line Kconfig wins over .conf files). Same list as build.yaml's
# cyber60-pi entry. Layer glow turns itself on once the board isn't split.
KEYBOARD_PI=(-DCONFIG_ZMK_SPLIT=n -DCONFIG_ZMK_STUDIO=y -DCONFIG_ZMK_STUDIO_LOCKING=y
    -DCONFIG_ZMK_POINTING=y)

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

# build <output name> <board> [shield] [snippet] [extra cmake args...]
build() {
    local name=$1 board=$2 shield=${3:-} snippet=${4:-}
    shift $(($# < 4 ? $# : 4))
    local args=(west build -p auto -s zmk/app -d "build/$name" -b "$board")
    [ -n "$snippet" ] && args+=(-S "$snippet")
    args+=(-- -DZMK_CONFIG=/repo/config -DZMK_EXTRA_MODULES=/repo)
    [ -n "$shield" ] && args+=("-DSHIELD=$shield")
    args+=("$@")

    echo "==> $name ($board${shield:+ / $shield})"
    # ZMK caches which keymap and .conf it picked (and -D options stick in the
    # cache too); drop the cache so every build starts from what's on disk now.
    # Objects are kept, so it's still quick.
    rm -f "$WEST/build/$name/CMakeCache.txt"
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
    keyboard)    build cyber60 cyber60@rev_e ;;
    dongle)      build dongle xiao_ble//zmk "prospector_dongle prospector_screen" studio-rpc-usb-uart ;;
    reset)       build reset-cyber60 cyber60@rev_e settings_reset
                 build reset-dongle xiao_ble//zmk settings_reset ;;
    keyboard-pi) build cyber60-pi cyber60@rev_e "" "" "${KEYBOARD_PI[@]}" ;;
    update)      update ;;
    all)         "$0" keyboard && "$0" dongle && "$0" reset ;;
    *)           echo "usage: $0 [keyboard|dongle|reset|keyboard-pi|update|all]" >&2; exit 2 ;;
esac
