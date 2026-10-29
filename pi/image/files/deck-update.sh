#!/bin/sh
# Installs a new version of the screen (deck-update.tar.gz, made by
# pi/image/build.sh) into /opt/deck and keeps the one before in /opt/deck.prev.
# Wifi and everything else saved on the Pi stay.
#
#   deck-update.sh              the file dropped on the SD card's boot partition:
#                               installed before the screen starts, then deleted
#   deck-update.sh <file>       a file from the DECK drive (deck-drive.sh)
#   deck-update.sh --rollback   back to the version before
#
# The tarball's units/*.service go into /etc/systemd/system when they differ.
OPT=${DECK_OPT:-/opt/deck}
BOOT=${DECK_BOOT:-/boot/firmware}
UNITS=${DECK_UNITS:-/etc/systemd/system}

note() {
    echo "$(date -Is) $*" >>"$BOOT/deck-update.log"
    echo "deck-update: $*"
}

units() {
    changed=
    for u in "$OPT"/units/*.service; do
        [ -f "$u" ] || continue
        cmp -s "$u" "$UNITS/${u##*/}" && continue
        cp "$u" "$UNITS/" && changed="$changed ${u##*/}"
    done
    [ -n "$changed" ] || return 0
    systemctl daemon-reload
    # shellcheck disable=SC2086 # one word per unit
    systemctl enable $changed
    note "units updated:$changed"
}

install() {
    rm -rf "$OPT.new" && mkdir -p "$OPT.new"
    if tar -xf "$1" -C "$OPT.new" && [ -x "$OPT.new/deck" ]; then
        rm -rf "$OPT.prev"
        mv "$OPT" "$OPT.prev" && mv "$OPT.new" "$OPT"
        units
        note "installed $(head -n1 "$OPT/VERSION" 2>/dev/null)"
        sync
        return 0
    fi
    rm -rf "$OPT.new"
    note "${1##*/} unreadable, ignored"
    return 1
}

rollback() {
    [ -x "$OPT.prev/deck" ] || {
        note "no version before to go back to"
        return 1
    }
    rm -rf "$OPT.bad"
    mv "$OPT" "$OPT.bad" && mv "$OPT.prev" "$OPT" && rm -rf "$OPT.bad"
    units
    note "went back to $(head -n1 "$OPT/VERSION" 2>/dev/null)"
    sync
}

case ${1:-} in
--rollback)
    rollback
    ;;
"")
    # Before the screen starts: never stop it from starting.
    U=$BOOT/deck-update.tar.gz
    [ -f "$U" ] || exit 0
    install "$U"
    rm -f "$U"
    sync
    exit 0
    ;;
*)
    install "$1"
    ;;
esac
