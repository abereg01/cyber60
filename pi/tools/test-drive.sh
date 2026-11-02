#!/usr/bin/env bash
# Try the DECK drive (image/files/deck-drive.sh + deck-update.sh) here on OMV,
# without a Pi. The real scripts run in the deck-tools container against a
# stand-in for the Pi: a fake USB port (configfs files you can write to), a
# real FAT drive image (mount/umount done with mtools, since a container has no
# loop devices), a fake systemd and a fake screen that comes up, or doesn't.
# "The computer" copies files onto the image, ejects it and pulls the cable.
# Also runs shellcheck on the scripts.
#
# Usage: pi/tools/test-drive.sh        (exit code 0 = all passed)
set -euo pipefail

if [ "${1:-}" != --inside ]; then
    PI=$(cd "$(dirname "$0")/.." && pwd)
    [ -w "${HOME:-/}" ] || export DOCKER_CONFIG=${DOCKER_CONFIG:-/srv/mergerfs/pool/docker/appdata/.docker-config}
    docker build -q --network=host -t deck-tools:local -f "$PI/image/Dockerfile.tools" "$PI/image" >/dev/null
    exec docker run --rm --network none --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PI":/pi:ro \
        deck-tools:local bash /pi/tools/test-drive.sh --inside
fi

F=/pi/image/files
echo "==> shellcheck"
shellcheck -s sh "$F/deck-drive.sh" "$F/deck-update.sh"
shellcheck /pi/tools/test-drive.sh

T=$(mktemp -d)
export MTOOLS_SKIP_CHECK=1
export DECK_TEST=$T DECK_BOOT=$T/boot DECK_OPT=$T/opt/deck DECK_DATA=$T/data DECK_RUN=$T/run \
    DECK_GADGET=$T/gadget DECK_UDC=$T/udc DECK_UNITS=$T/units DECK_TICK=0.1 DECK_READY_WAIT=3 DECK_SETTLE=1
LUN=$DECK_GADGET/functions/mass_storage.0/lun.0
IMG=$DECK_DATA/drive.img
STATE=$DECK_UDC/fe980000.usb/state
DRIVE_LOG=$T/drive.log
DRIVE_PID=

cleanup() {
    [ -n "$DRIVE_PID" ] && kill "$DRIVE_PID" 2>/dev/null
    [ -f "$T/sysd/pid" ] && kill -- -"$(cat "$T/sysd/pid")" 2>/dev/null
    rm -rf "$T"
}
trap cleanup EXIT

# ── The stand-in Pi ──────────────────────────────────────────────────────────

mkdir -p "$DECK_BOOT" "$DECK_OPT/units" "$DECK_DATA" "$DECK_RUN" "$LUN" "$DECK_UNITS" \
    "$(dirname "$STATE")" "$T/bin" "$T/sysd"
cp "$F/deck.conf" "$DECK_BOOT/"
sed -i 's/^shelly_key=.*/shelly_key=bootKEY123/' "$DECK_BOOT/deck.conf" # put there on the SD card itself
echo "the first start" >"$DECK_BOOT/deck-log.txt"
: >"$LUN/file"
: >"$LUN/forced_eject"
: >"$DECK_GADGET/UDC"
echo configured >"$STATE"

# mount -o loop IMG DIR / umount DIR, with mtools.
cat >"$T/bin/mount" <<'EOF'
#!/bin/sh
for a; do img=$dir; dir=$a; done
minfo -i "$img" >/dev/null 2>&1 || exit 32
mkdir -p "$dir" && find "$dir" -mindepth 1 -delete
mcopy -s -m -n -i "$img" '::*' "$dir/" 2>/dev/null
echo "$img" >"$dir.img"
EOF
cat >"$T/bin/umount" <<'EOF'
#!/bin/sh
dir=$1
img=$(cat "$dir.img")
rm -f "$img" && truncate -s 64M "$img" && mkfs.fat -n DECK "$img" >/dev/null
for f in "$dir"/* "$dir"/.[!.]*; do
    [ -e "$f" ] && mcopy -s -m -o -i "$img" "$f" ::/
done
find "$dir" -mindepth 1 -delete
rm -f "$dir.img"
echo x >>"$DECK_TEST/umounts"
EOF
# systemd, for deck.service: a supervisor that restarts the screen when it
# dies (like Restart=always) and counts it (NRestarts).
cat >"$T/bin/systemctl" <<'EOF'
#!/bin/sh
S=$DECK_TEST/sysd
case $1 in
restart)
    [ -f "$S/pid" ] && kill -- -"$(cat "$S/pid")" 2>/dev/null
    echo 0 >"$S/nrestarts"
    setsid sh -c 'n=0; while :; do "$DECK_OPT/deck"; n=$((n + 1)); echo $n >"$DECK_TEST/sysd/nrestarts"; sleep 0.3; done' \
        >/dev/null 2>&1 </dev/null &
    echo $! >"$S/pid"
    ;;
show) cat "$S/nrestarts" ;;
is-active) kill -0 "$(cat "$S/pid")" 2>/dev/null ;;
*) echo "$*" >>"$S/calls" ;;
esac
EOF
printf '#!/bin/sh\necho "(the journal)"\n' >"$T/bin/journalctl"
printf '#!/bin/sh\necho throttled=0x0\n' >"$T/bin/vcgencmd"
printf '#!/bin/sh\necho no\n' >"$T/bin/timedatectl"
chmod +x "$T"/bin/*
export PATH=$T/bin:$PATH

# A version of the screen, as deck-update.tar.gz in $T/out/<version>/:
#   make_update <version> good|crash|crash-late [drive script]
# good comes up and stays; crash never comes up; crash-late comes up and dies.
make_update() {
    local d=$T/out/$1/deck
    mkdir -p "$d/units"
    cp "$F/deck-update.sh" "$F/DRIVE-README.txt" "$d/"
    cp "${3:-$F/deck-drive.sh}" "$d/deck-drive.sh"
    cp "$F/deck.service" "$F/deck-drive.service" "$d/units/"
    echo "$1" >"$d/VERSION"
    cat >"$d/deck" <<EOF
#!/bin/sh
echo "fake deck \$(cat "\$(dirname "\$0")/VERSION") starting" >"\$DECK_BOOT/deck-log.txt"
[ $2 = crash ] && exit 1
mkdir -p "\$DECK_RUN" && : >"\$DECK_RUN/ready"
if [ -f "\$DECK_RUN/notice" ]; then cat "\$DECK_RUN/notice" >>"\$DECK_TEST/notices"; rm -f "\$DECK_RUN/notice"; fi
[ $2 = crash-late ] && { sleep 0.3; exit 1; }
exec sleep 1000
EOF
    chmod +x "$d/deck" "$d/deck-update.sh" "$d/deck-drive.sh"
    tar -czf "$T/out/$1/deck-update.tar.gz" -C "$d" .
}

# The installed version 1, as the SD image has it.
make_update v1 good
tar -xzf "$T/out/v1/deck-update.tar.gz" -C "$DECK_OPT"

# ── The computer ─────────────────────────────────────────────────────────────

host_put() { mcopy -m -o -i "$IMG" "$1" "::${2:-$(basename "$1")}"; }
host_put_dir() { mcopy -s -m -o -i "$IMG" "$1" ::/; }
host_cat() { mtype -i "$IMG" "::$1"; }
host_has() { mdir -b -i "$IMG" "::$1" >/dev/null 2>&1; }
eject() { : >"$LUN/file"; }
unplug() { echo "not attached" >"$STATE"; }
plug() { echo configured >"$STATE"; }

# Wait until the Pi has read the drive and given it back.
done_wait() {
    local before=$1
    for _ in $(seq 300); do
        if [ "$(wc -l <"$T/umounts" 2>/dev/null || echo 0)" -gt "$before" ] && [ -n "$(cat "$LUN/file")" ] &&
            [ ! -e "$DECK_RUN/drive-busy" ]; then
            return 0
        fi
        sleep 0.1
    done
    echo "timed out; deck-drive.sh said:" && cat "$DRIVE_LOG"
    return 1
}
umounts() { wc -l <"$T/umounts" 2>/dev/null || echo 0; }

FAILS=0
check() { # description, command...
    local what=$1
    shift
    if "$@" >/dev/null 2>&1; then
        echo "   ok    $what"
    else
        echo "   FAIL  $what"
        FAILS=$((FAILS + 1))
    fi
}
status_has() { host_cat DECK-STATUS.txt | grep -qF "$1"; }
version_is() { [ "$(cat "$DECK_OPT/VERSION")" = "$1" ]; }
notice_was() { tail -n1 "$T/notices" | grep -qF "$1"; }
drive_said() { grep -qF "$1" "$DRIVE_LOG"; }

start_drive() {
    "$DECK_OPT/deck-drive.sh" >>"$DRIVE_LOG" 2>&1 &
    DRIVE_PID=$!
    for _ in $(seq 100); do
        [ -n "$(cat "$LUN/file")" ] && return 0
        sleep 0.1
    done
    cat "$DRIVE_LOG"
    return 1
}

# ── Scenarios ────────────────────────────────────────────────────────────────

echo "==> power on"
start_drive
check "a FAT drive called DECK" sh -c "[ \"\$(fatlabel '$IMG')\" = DECK ]"
check "README.txt, DECK-STATUS.txt and deck.conf on it" sh -c \
    "mtype -i '$IMG' ::README.txt && mtype -i '$IMG' ::DECK-STATUS.txt && mtype -i '$IMG' ::deck.conf"
check "the status names version v1" status_has "Running:      v1"
check "the screen's log is on it" sh -c "mtype -i '$IMG' ::deck-log.txt | grep -q 'the first start'"
check "a Shelly key on the SD card moved onto the Pi" grep -qx bootKEY123 "$DECK_DATA/shelly-key"
check "and blanked on the SD card and the drive" sh -c \
    "! grep -q bootKEY123 '$DECK_BOOT/deck.conf' && ! mtype -i '$IMG' ::deck.conf | grep -q bootKEY123"

echo "==> v2 as deck-update.tar.gz, then eject"
make_update v2 good
host_put "$T/out/v2/deck-update.tar.gz"
n=$(umounts)
eject
done_wait "$n"
check "v2 is installed" version_is v2
check "v1 is kept in deck.prev" sh -c "[ \"\$(cat '$DECK_OPT.prev/VERSION')\" = v1 ]"
check "the screen showed 'Updated · v2'" notice_was "Updated · v2"
check "the update file is gone from the drive" sh -c "! mdir -b -i '$IMG' ::deck-update.tar.gz"
check "the status says v1 -> v2" status_has "updated v1 -> v2"
check "the new screen's log is on the drive" sh -c "mtype -i '$IMG' ::deck-log.txt | grep -q 'fake deck v2'"
check "the units went into systemd" sh -c "[ -f '$DECK_UNITS/deck-drive.service' ] && grep -q daemon-reload '$T/sysd/calls'"

echo "==> v3 as GitHub's deck-update.zip (two downloads, the newest wins), cable pulled without eject"
make_update v3 good
make_update v3old good
(cd "$T/out/v3old" && python3 -m zipfile -c "$T/old.zip" deck-update.tar.gz)
(cd "$T/out/v3" && python3 -m zipfile -c "$T/deck-update.zip" deck-update.tar.gz)
touch -d '1 hour ago' "$T/old.zip"
host_put "$T/old.zip" "deck-update (1).zip"
host_put "$T/deck-update.zip"
n=$(umounts)
unplug
done_wait "$n"
check "v3 is installed" version_is v3
check "both zips are gone from the drive" sh -c "! mdir -b -i '$IMG' '::deck-update*'"
plug
sleep 0.3

echo "==> deck.conf edited on the drive (Notepad: CRLF), then eject"
host_cat deck.conf | sed 's/^demo=0/demo=1/' | sed 's/$/\r/' >"$T/deck.conf"
host_put "$T/deck.conf"
n=$(umounts)
eject
done_wait "$n"
check "the SD card's deck.conf has demo=1" grep -q '^demo=1' "$DECK_BOOT/deck.conf"
check "the screen showed 'Settings applied'" notice_was "Settings applied"
check "no new version for a settings change" version_is v3

echo "==> a Shelly key in deck.conf (CRLF), then eject"
host_cat deck.conf | tr -d '\r' | sed 's/^shelly_key=.*/shelly_key=dGVzdC1rZXk0NTY=/' | sed 's/$/\r/' >"$T/deck.conf"
host_put "$T/deck.conf"
n=$(umounts)
eject
done_wait "$n"
check "the key is on the Pi" grep -qx 'dGVzdC1rZXk0NTY=' "$DECK_DATA/shelly-key"
check "only root can read it (600)" sh -c "[ \"\$(stat -c %a '$DECK_DATA/shelly-key')\" = 600 ]"
check "not on the SD card's deck.conf" sh -c "! grep -q dGVzdC1rZXk0NTY '$DECK_BOOT/deck.conf'"
check "not on the drive" sh -c "! mtype -i '$IMG' ::deck.conf | grep -q dGVzdC1rZXk0NTY"
check "the rest of deck.conf survived" grep -q '^demo=1' "$DECK_BOOT/deck.conf"
check "the screen showed 'Shelly key saved'" notice_was "Shelly key saved"

echo "==> v4 that never comes up, then eject"
make_update v4 crash
host_put "$T/out/v4/deck-update.tar.gz"
n=$(umounts)
eject
done_wait "$n"
check "back to v3" version_is v3
check "the screen showed 'Update failed · back to v3'" notice_was "Update failed · back to v3"
check "deck-log-failed.txt holds v4's log" sh -c "mtype -i '$IMG' ::deck-log-failed.txt | grep -q 'fake deck v4'"
check "the status says what happened" status_has "v4 didn't start"

echo "==> v5 that comes up and dies a moment later"
make_update v5 crash-late
host_put "$T/out/v5/deck-update.tar.gz"
n=$(umounts)
eject
done_wait "$n"
check "back to v3" version_is v3
check "the status says v5 didn't start" status_has "v5 didn't start"

echo "==> v6 with a broken deck-drive.sh"
printf 'if then fi\n' >"$T/broken-drive.sh"
make_update v6 good "$T/broken-drive.sh"
host_put "$T/out/v6/deck-update.tar.gz"
n=$(umounts)
eject
done_wait "$n"
check "v6 is installed" version_is v6
check "deck-drive.sh doesn't switch to the broken one" drive_said "has a mistake in it"
check "the failed log from v4/v5 is gone after a good update" sh -c "! mdir -b -i '$IMG' ::deck-log-failed.txt"

echo "==> v7 with a new, working deck-drive.sh"
sed 's/^say "DECK is ready/say "drive script v7"\nsay "DECK is ready/' "$F/deck-drive.sh" >"$T/v7-drive.sh"
make_update v7 good "$T/v7-drive.sh"
host_put "$T/out/v7/deck-update.tar.gz"
n=$(umounts)
eject
done_wait "$n"
check "v7 is installed" version_is v7
check "deck-drive.sh carried on as the new one" drive_said "drive script v7"
n=$(umounts)
eject
done_wait "$n"
check "and the new one still reads the drive" drive_said "ejected"

echo "==> v8 as a Mac leaves GitHub's download: unpacked into a folder (._ files, no x bits), then eject"
make_update v8 good
mkdir -p "$T/mac/deck-update"
cp -R "$T/out/v8/deck/." "$T/mac/deck-update/"
find "$T/mac/deck-update" -type f -exec chmod 644 {} +
: >"$T/mac/deck-update/._deck"
: >"$T/mac/deck-update/.DS_Store"
host_put_dir "$T/mac/deck-update"
n=$(umounts)
eject
done_wait "$n"
check "v8 is installed" version_is v8
check "the screen can start (deck is executable again)" sh -c "[ -x '$DECK_OPT/deck' ]"
check "no Mac files came along" sh -c "! find '$DECK_OPT' \\( -name '._*' -o -name .DS_Store \\) | grep -q ."
check "the folder is gone from the drive" sh -c "! mdir -b -i '$IMG' ::deck-update"

echo "==> v9 as a Mac's zip of that folder, any name (with __MACOSX), then eject"
make_update v9 good
mkdir -p "$T/mac2/deck-update" "$T/mac2/__MACOSX/deck-update"
cp -R "$T/out/v9/deck/." "$T/mac2/deck-update/"
echo junk >"$T/mac2/__MACOSX/deck-update/._VERSION"
(cd "$T/mac2" && python3 -m zipfile -c "$T/Deck Update from Cyber60.zip" deck-update __MACOSX)
host_put "$T/Deck Update from Cyber60.zip"
n=$(umounts)
eject
done_wait "$n"
check "v9 is installed" version_is v9
check "the zip is gone from the drive" sh -c "! mdir -b -i '$IMG' '::*.zip'"

echo "==> drive=0 on the drive, then eject"
host_cat deck.conf | sed 's/^drive=1/drive=0/' >"$T/deck.conf"
host_put "$T/deck.conf"
eject
for _ in $(seq 100); do kill -0 "$DRIVE_PID" 2>/dev/null || break; sleep 0.1; done
check "deck-drive.sh stopped" sh -c "! kill -0 $DRIVE_PID"
check "the USB port was let go" sh -c "[ -z \"\$(tr -d '\n' <'$DECK_GADGET/UDC')\" ]"
DRIVE_PID=
"$DECK_OPT/deck-drive.sh" >>"$DRIVE_LOG" 2>&1 && rc=0 || rc=$?
check "with drive=0 it doesn't start at all" [ "$rc" = 0 ]

# The SD card's bootfs: deck.service runs deck-update.sh before the screen.
mkdir -p "$DECK_BOOT/overlays" && : >"$DECK_BOOT/overlays/vc4-kms-v3d.dtbo"

echo "==> power-on with a Mac-unpacked folder on bootfs"
make_update v10 good
mkdir -p "$DECK_BOOT/Deck Update from Cyber60"
cp -R "$T/out/v10/deck/." "$DECK_BOOT/Deck Update from Cyber60/"
"$DECK_OPT/deck-update.sh" >/dev/null
check "v10 is installed" version_is v10
check "the folder is gone from bootfs" sh -c "[ ! -e '$DECK_BOOT/Deck Update from Cyber60' ]"
check "bootfs kept the rest" sh -c "[ -f '$DECK_BOOT/deck.conf' ] && [ -f '$DECK_BOOT/overlays/vc4-kms-v3d.dtbo' ]"

echo "==> power-on with GitHub's zip and an older folder one level deeper on bootfs"
make_update v11 good
make_update v11old good
mkdir -p "$DECK_BOOT/Downloads/deck-update"
cp -R "$T/out/v11old/deck/." "$DECK_BOOT/Downloads/deck-update/"
touch -d '1 hour ago' "$DECK_BOOT/Downloads/deck-update/VERSION"
(cd "$T/out/v11" && python3 -m zipfile -c "$DECK_BOOT/deck-update.zip" deck-update.tar.gz)
"$DECK_OPT/deck-update.sh" >/dev/null
check "v11, the newest, is installed" version_is v11
check "both are gone from bootfs" sh -c "[ ! -e '$DECK_BOOT/Downloads' ] && [ ! -e '$DECK_BOOT/deck-update.zip' ]"

echo "==> power-on with nothing new"
"$DECK_OPT/deck-update.sh" >/dev/null
check "still v11" version_is v11
check "bootfs untouched" sh -c "[ -f '$DECK_BOOT/deck.conf' ] && [ -d '$DECK_BOOT/overlays' ]"

echo
if [ "$FAILS" = 0 ]; then
    echo "all passed"
else
    echo "$FAILS failed. deck-drive.sh said:"
    cat "$DRIVE_LOG"
    exit 1
fi
