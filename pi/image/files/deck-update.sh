#!/bin/sh
# Installs a new version of the screen into /opt/deck and keeps the one before
# in /opt/deck.prev. Wifi and everything else saved on the Pi stay.
#
# A new version is taken in whatever shape the computer left it:
#   deck-update.tar.gz   from pi/image/build.sh
#   any .zip             GitHub Actions' "deck-update" download as it is, or
#                        a zip of the files themselves
#   a folder             the download unpacked (a Mac does that by itself):
#                        deck, VERSION, share/, units/ ... in it, or one
#                        folder deeper
#
#   deck-update.sh              whatever is on the SD card's boot partition:
#                               installed before the screen starts, then deleted
#   deck-update.sh <path>       one from the DECK drive (deck-drive.sh)
#   deck-update.sh --find DIR   the newest new version in DIR (deck-drive.sh)
#   deck-update.sh --clear DIR  delete every new version in DIR
#   deck-update.sh --rollback   back to the version before
#
# The new version's units/*.service go into /etc/systemd/system when they differ.
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
    # A new unit runs now, not only from the next power-on (one already
    # running, like the screen itself, is left alone).
    # shellcheck disable=SC2086
    systemctl start --no-block $changed
    note "units updated:$changed"
}

# Every new version lying in $1, newest first, one per line. A folder counts
# when it (or a folder in it) holds a VERSION file.
candidates() {
    # shellcheck disable=SC2012 # ls -t: the newest wins ("deck-update (1).tar.gz")
    ls -td "$1"/deck-update*.tar.gz "$1"/deck-update*.tgz "$1"/deck-update*.tar "$1"/*.zip "$1"/*.ZIP \
        "$1"/*/VERSION "$1"/*/*/VERSION 2>/dev/null | sed 's|/VERSION$||'
}

# What to delete once $1 (found in $2) is in: the file, or the folder it came in.
top() {
    rel=${1#"$2"/}
    echo "$2/${rel%%/*}"
}

clear_dir() {
    candidates "$1" | while IFS= read -r c; do
        t=$(top "$c" "$1")
        [ "$t" != "$1/" ] && rm -rf "$t"
    done
}

# $1 (tarball, zip or folder) into the empty folder $2.
unpack() {
    src=$1
    case $src in
    *.zip | *.ZIP)
        z=$2.zip
        rm -rf "$z" && mkdir -p "$z" && unzip -qo "$src" -d "$z" >/dev/null 2>&1 || return 1
        src=$(find "$z" \( -name 'deck-update*.tar*' -o -name 'deck-update*.tgz' \) ! -path '*__MACOSX*' | head -n1)
        [ -n "$src" ] || src=$(find "$z" -name VERSION ! -path '*__MACOSX*' | head -n1 | sed 's|/VERSION$||')
        [ -n "$src" ] || return 1
        ;;
    esac
    if [ -d "$src" ]; then
        cp -R "$src"/. "$2"/
    else
        tar -xf "$src" -C "$2"
    fi
}

# A Mac's extra files out, and the modes set here: a folder that went through
# a FAT drive or a zip doesn't bring them.
tidy() {
    find "$1" -depth \( -name '._*' -o -name .DS_Store -o -name __MACOSX \) -exec rm -rf {} +
    find "$1" -type d -exec chmod 755 {} +
    find "$1" -type f -exec chmod 644 {} +
    for x in "$1"/deck "$1"/*.sh; do
        [ -f "$x" ] && chmod 755 "$x"
    done
    return 0
}

install() {
    rm -rf "$OPT.new" "$OPT.new.zip" && mkdir -p "$OPT.new"
    if unpack "$1" "$OPT.new" && tidy "$OPT.new" && [ -x "$OPT.new/deck" ] && [ -f "$OPT.new/VERSION" ]; then
        rm -rf "$OPT.new.zip" "$OPT.prev"
        mv "$OPT" "$OPT.prev" && mv "$OPT.new" "$OPT"
        units
        note "installed $(head -n1 "$OPT/VERSION" 2>/dev/null)"
        sync
        return 0
    fi
    rm -rf "$OPT.new" "$OPT.new.zip"
    note "${1##*/} unreadable or not a version of the screen, ignored"
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
--find)
    candidates "$2" | head -n1
    ;;
--clear)
    clear_dir "$2"
    ;;
"")
    # Before the screen starts: never stop it from starting.
    U=$(candidates "$BOOT" | head -n1)
    [ -n "$U" ] || exit 0
    install "$U"
    clear_dir "$BOOT"
    sync
    exit 0
    ;;
*)
    install "$1"
    ;;
esac
