#!/usr/bin/env bash
# shellcheck disable=SC2016 # the commands in single quotes run on the Pi
# The live loop while building the desk screen (pi/PLAN.md, "How we work"):
# cross-compile only the app, incrementally, put it on the Pi over `ssh deck`
# and restart the screen. Never builds SD images; official updates still come
# from GitHub Actions.
#
#   pi/tools/dev.sh             build, push, restart, show the end of the log
#   pi/tools/dev.sh --watch     the same after every save under pi/app (polls)
#   pi/tools/dev.sh build       only build (pi/build/dev/deck)
#   pi/tools/dev.sh check F...  only compile-check these files for the Pi
#   pi/tools/dev.sh log         follow the screen's log on the Pi
#   pi/tools/dev.sh release     back to the version the Pi had before the first
#                               dev push (the last real update)
#
# A push goes through the Pi's own deck-update.sh, with the scripts and units
# from pi/image/files too, so /opt/deck.prev and the units behave as after a
# real update. VERSION says "dev".
set -euo pipefail

PI=$(cd "$(dirname "$0")/.." && pwd)
REPO=$(cd "$PI/.." && pwd)
CACHE=${DECK_CACHE:-/srv/mergerfs/pool/docker/appdata/zmk-west/.cache/deck}
HOST=${DECK_HOST:-deck}
OUT=$PI/build/dev
[ -w "${HOME:-/}" ] || export DOCKER_CONFIG=${DOCKER_CONFIG:-/srv/mergerfs/pool/docker/appdata/.docker-config}
SSH=(ssh -o ControlMaster=auto -o "ControlPath=/tmp/deck-ssh-$(id -u)-%C" -o ControlPersist=120
    -o ConnectTimeout=8)

# Same flags as pi/image/build-app.sh, paths as the container sees them.
CFLAGS="-O2 -ffunction-sections -fdata-sections -I/repo/pi/app -I/repo/pi/app/src -I/repo/pi/app/pi \
 -I/cache/deps/lvgl-9.3.0 -I/repo/boards/shields/prospector_screen/src/layouts/shoko \
 -I/cache/deps/cJSON-1.7.18 -I/cache/deps/monocypher-4.0.2/src -I/usr/include/libdrm \
 -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE \
 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-format-truncation"

in_tools() {
    docker run --rm --network host --user "$(id -u):$(id -g)" -e HOME=/tmp -e DECK_CACHE=/cache \
        -v "$REPO":/repo -v "$CACHE":/cache -w /repo deck-tools:local bash -c "$1"
}

build() {
    mkdir -p "$OUT"
    if [ ! -f "$CACHE/pi-lvgl/liblvgl.a" ] || [ "$PI/app/lv_conf.h" -nt "$CACHE/pi-lvgl/liblvgl.a" ]; then
        echo "==> LVGL for arm64 is missing or old: building it (once) with build-app.sh's recipe"
        in_tools "pi/tools/fetch-deps.sh && pi/image/build-app.sh >/dev/null"
    fi
    # One object per source in the cache, rebuilt when the source or a header
    # it includes changed (gcc -MMD).
    cat >"$CACHE/dev.mk" <<EOF
CC := aarch64-linux-gnu-gcc
CFLAGS := $CFLAGS
O := /cache/dev-obj
SRCS := \$(wildcard /repo/pi/app/src/*.c /repo/pi/app/src/assets/*.c /repo/pi/app/pi/*.c) \
        /cache/deps/cJSON-1.7.18/cJSON.c /cache/deps/monocypher-4.0.2/src/monocypher.c
OBJS := \$(patsubst /%,\$(O)/%.o,\$(SRCS))
/repo/pi/build/dev/deck: \$(OBJS) /cache/pi-lvgl/liblvgl.a
	\$(CC) \$^ -Wl,--gc-sections -ldrm -lcurl -ljpeg -lpthread -lm -o \$@
	aarch64-linux-gnu-strip \$@
\$(O)/%.o: /%
	@mkdir -p \$(dir \$@)
	@echo "  cc \$(notdir \$<)"
	@\$(CC) \$(CFLAGS) -MMD -MP -c \$< -o \$@
-include \$(OBJS:.o=.d)
EOF
    in_tools "make --no-print-directory -s -j\$(nproc) -f /cache/dev.mk"
}

push() {
    echo "$(date -u '+%Y-%m-%d %H:%M') dev" >"$OUT/VERSION"
    local F=$PI/image/files
    # Stage a copy of what's installed, lay the new files over it, install
    # that through deck-update.sh, restart.
    "${SSH[@]}" "$HOST" 'set -e; [ -d /opt/deck.release ] || cp -a /opt/deck /opt/deck.release
        rm -rf /run/deck-dev && cp -a /opt/deck /run/deck-dev'
    tar -cf - -C "$OUT" deck VERSION -C "$F" deck-update.sh deck-drive.sh deck-report.sh deck-net.sh \
        deck-ssh.sh DRIVE-README.txt |
        "${SSH[@]}" "$HOST" 'tar -xf - -C /run/deck-dev'
    tar -cf - -C "$F" deck.service deck-drive.service deck-report.service deck-net.service deck-ssh.service |
        "${SSH[@]}" "$HOST" 'tar -xf - -C /run/deck-dev/units'
    "${SSH[@]}" "$HOST" 'set -e; rm -f /run/deck/ready
        /opt/deck/deck-update.sh /run/deck-dev >/dev/null; rm -rf /run/deck-dev
        systemctl restart deck
        for i in $(seq 1 30); do [ -f /run/deck/ready ] && break; sleep 0.5; done
        [ -f /run/deck/ready ] && echo "==> up: $(head -n1 /opt/deck/VERSION)" || echo "==> NOT UP after 15 s"
        tail -n 12 /boot/firmware/deck-log.txt'
}

case ${1:-} in
build)
    build
    ls -la "$OUT/deck"
    ;;
check)
    shift
    [ $# -gt 0 ] || { echo "usage: dev.sh check pi/app/src/x.c ..." >&2; exit 2; }
    files=()
    for f in "$@"; do files+=("/repo/$(realpath --relative-to="$REPO" "$f")"); done
    in_tools "for f in ${files[*]}; do aarch64-linux-gnu-gcc -fsyntax-only $CFLAGS \$f && echo \"ok  \$f\"; done"
    ;;
log)
    "${SSH[@]}" "$HOST" 'tail -n 30 -f /boot/firmware/deck-log.txt'
    ;;
release)
    "${SSH[@]}" "$HOST" 'set -e; [ -d /opt/deck.release ] || { echo "no dev push yet: nothing to undo"; exit 1; }
        rm -rf /opt/deck.prev && mv /opt/deck /opt/deck.prev && mv /opt/deck.release /opt/deck
        systemctl restart deck && sleep 3 && echo "back to $(head -n1 /opt/deck/VERSION)"'
    ;;
--watch)
    stamp=$OUT/.watch
    mkdir -p "$OUT"
    if build; then push || true; fi
    touch "$stamp"
    echo "==> watching pi/app and pi/image/files (Ctrl+C stops)"
    while sleep 1; do
        if [ -n "$(find "$PI/app" "$PI/image/files" -newer "$stamp" -type f \( -name '*.[ch]' -o -name '*.sh' \
            -o -name '*.service' \) -print -quit)" ]; then
            sleep 0.5 # let the editor finish writing
            touch "$stamp"
            build && push || echo "==> build or push failed; waiting for the next save"
        fi
    done
    ;;
"")
    build
    push
    ;;
*)
    sed -n '2,17p' "$0"
    exit 2
    ;;
esac
