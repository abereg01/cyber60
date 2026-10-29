#!/bin/sh
# The DECK drive: the Pi shows up as a small USB drive on the computer its
# USB-C port is plugged into. Copy a new version (deck-update.tar.gz, or the
# deck-update.zip GitHub Actions hands out) or an edited deck.conf onto it and
# eject it: the Pi installs it, restarts the screen (no reboot) and puts the
# drive back with DECK-STATUS.txt and the screen's log on it. Pulling the cable
# instead of ejecting works too. drive=0 in deck.conf turns all this off.
#
# The computer and the Pi can't both use the drive at once, so the Pi only
# opens it while the computer has let go: after an eject (the backing file of
# a removable LUN goes empty, kernel Documentation/usb/mass-storage.rst) or
# when the cable is pulled. A new version that doesn't come up is replaced by
# the one before (deck-update.sh --rollback).
#
# Run by deck-drive.service. The DECK_* variables move everything somewhere
# else for pi/tools/test-drive.sh.

BOOT=${DECK_BOOT:-/boot/firmware}
OPT=${DECK_OPT:-/opt/deck}
DATA=${DECK_DATA:-/var/lib/deck}
RUN=${DECK_RUN:-/run/deck}
GADGET=${DECK_GADGET:-/sys/kernel/config/usb_gadget/deck}
UDC=${DECK_UDC:-/sys/class/udc}
TICK=${DECK_TICK:-0.5}        # seconds between looks at the USB port
READY_WAIT=${DECK_READY_WAIT:-15} # for a restarted screen to come up
SETTLE=${DECK_SETTLE:-3}      # and then to stay up

IMG=$DATA/drive.img
MNT=$RUN/drive
LUN=$GADGET/functions/mass_storage.0/lun.0
LAST=$DATA/drive-last.txt # what happened last, for DECK-STATUS.txt

say() { echo "deck-drive: $*"; }
last() { echo "$(date '+%Y-%m-%d %H:%M')  $*" >"$LAST"; say "$*"; }
version() { head -n1 "$OPT/VERSION" 2>/dev/null || echo unknown; }

drive_on() { ! grep -Eq '^[[:space:]]*drive[[:space:]]*=[[:space:]]*0' "$BOOT/deck.conf" 2>/dev/null; }

# ── The drive image ──────────────────────────────────────────────────────────

image_make() {
    rm -f "$IMG"
    truncate -s 64M "$IMG.new" && mkfs.fat -n DECK "$IMG.new" >/dev/null && mv "$IMG.new" "$IMG"
}

image_open() {
    fsck.fat -a -w "$IMG" >/dev/null 2>&1 # repairs a drive pulled out mid-copy
    mkdir -p "$MNT" && mount -o loop "$IMG" "$MNT" && return 0
    say "the drive image won't mount, starting a new one"
    image_make && mount -o loop "$IMG" "$MNT"
}

image_close() {
    sync
    umount "$MNT"
}

put() { # file on the Pi, name on the drive: copied when it differs
    [ -f "$1" ] && ! cmp -s "$1" "$MNT/$2" && cp "$1" "$MNT/$2"
}

status() {
    echo "Desk screen - DECK drive"
    echo
    echo "Running:      $(version)"
    echo "Last action:  $(cat "$LAST" 2>/dev/null || echo none yet)"
    t=$(vcgencmd get_throttled 2>/dev/null | cut -d= -f2)
    case $t in 0x[0-9a-fA-F]*) ;; *) t= ;; esac
    if [ -z "$t" ]; then
        echo "Power:        unknown"
    elif [ $((t & 0x10000)) -ne 0 ]; then
        echo "Power:        TOO LOW since power-on: use the USB-C splitter or a stronger port"
    else
        echo "Power:        OK since power-on"
    fi
    if [ "$(timedatectl show -p NTPSynchronized --value 2>/dev/null)" = yes ]; then
        echo "Clock:        set over wifi"
    else
        echo "Clock:        a guess (no wifi), so the times above may be off"
    fi
    echo
    echo "Drop a deck-update.tar.gz (or the deck-update.zip from GitHub) or an"
    echo "edited deck.conf here, then eject this drive. README.txt has more."
}

# The Pi's side of the drive, refreshed before the computer gets it back.
fill() {
    image_open || return 1
    status >"$RUN/status.txt"
    put "$RUN/status.txt" DECK-STATUS.txt
    put "$BOOT/deck.conf" deck.conf
    put "$BOOT/deck-log.txt" deck-log.txt
    put "$OPT/DRIVE-README.txt" README.txt
    if [ -f "$DATA/failed-log.txt" ]; then
        put "$DATA/failed-log.txt" deck-log-failed.txt
    else
        rm -f "$MNT/deck-log-failed.txt"
    fi
    [ -e "$MNT/.metadata_never_index" ] || : >"$MNT/.metadata_never_index" # no Spotlight on a Mac
    image_close
}

# ── USB ──────────────────────────────────────────────────────────────────────

gadget_up() {
    if [ ! -d "$GADGET" ]; then
        modprobe libcomposite && mkdir "$GADGET" && cd "$GADGET" || return 1
        echo 0x1d6b >idVendor  # Linux Foundation
        echo 0x0104 >idProduct # Multifunction Composite Gadget
        echo 0x0100 >bcdDevice
        echo 0x0200 >bcdUSB
        mkdir -p strings/0x409 configs/c.1/strings/0x409 functions/mass_storage.0
        echo Cyber60 >strings/0x409/manufacturer
        echo "Desk screen" >strings/0x409/product
        tr -d '\000' </proc/device-tree/serial-number >strings/0x409/serialnumber 2>/dev/null
        echo DECK >configs/c.1/strings/0x409/configuration
        echo 500 >configs/c.1/MaxPower
        echo 1 >"$LUN/removable"
        echo 0 >"$LUN/ro"
        echo "Cyber60 DECK" >"$LUN/inquiry_string"
        ln -s functions/mass_storage.0 configs/c.1/
        cd / || return 1
    fi
    [ -n "$(cat "$GADGET/UDC")" ] && return 0 # bound before deck-drive.sh (re)started
    for u in "$UDC"/*; do # the Pi 4 has one: its USB-C port
        [ -e "$u" ] && echo "${u##*/}" >"$GADGET/UDC" && return 0
    done
    return 1
}

attached() { [ -n "$(cat "$LUN/file" 2>/dev/null)" ]; }
attach() { echo "$IMG" >"$LUN/file"; }
detach() {
    echo 1 >"$LUN/forced_eject" 2>/dev/null
    attached && echo >"$LUN/file"
}
# "configured" while a computer uses the drive. Anything else after that (cable
# pulled, computer asleep) counts as let go: the drive comes back as a new
# disk, so the computer re-reads it instead of trusting what it had cached.
usb_state() { cat "$UDC"/*/state 2>/dev/null | head -n1; }

# ── Reading the drive ────────────────────────────────────────────────────────

# Restart the screen and wait for it to say it's up (it writes $RUN/ready) and
# stay up. False if it doesn't.
screen_restart() {
    rm -f "$RUN/ready"
    systemctl restart deck.service
    n0=$(systemctl show -p NRestarts --value deck.service)
    i=0
    while [ ! -e "$RUN/ready" ]; do
        i=$((i + 1))
        [ "$i" -gt $((READY_WAIT * 2)) ] && return 1
        sleep 0.5
    done
    sleep "$SETTLE"
    systemctl is-active --quiet deck.service &&
        [ "$(systemctl show -p NRestarts --value deck.service)" = "$n0" ]
}

process() {
    : >"$RUN/drive-busy" # the screen shows "Reading DECK..."
    image_open || {
        rm -f "$RUN/drive-busy"
        return 1
    }
    update=
    restart=
    # The newest new version, as a tarball or GitHub's zip; Windows names a
    # second download "deck-update (1).tar.gz".
    # shellcheck disable=SC2012 # ls -t: sorting by date is the point
    u=$(ls -t "$MNT"/deck-update*.tar.gz "$MNT"/deck-update*.tgz "$MNT"/deck-update*.tar \
        "$MNT"/deck-update*.zip 2>/dev/null | head -n1)
    if [ -n "$u" ]; then
        rm -rf "$DATA/incoming" && mkdir -p "$DATA/incoming"
        case $u in
        *.zip)
            unzip -qo "$u" -d "$DATA/incoming" 2>/dev/null
            update=$(find "$DATA/incoming" -name 'deck-update*.tar*' | head -n1)
            [ -n "$update" ] || last "$(basename "$u") has no deck-update.tar.gz in it, ignored"
            ;;
        *)
            cp "$u" "$DATA/incoming/" && update=$DATA/incoming/$(basename "$u")
            ;;
        esac
        rm -f "$MNT"/deck-update*
    fi
    if [ -f "$MNT/deck.conf" ] && ! cmp -s "$MNT/deck.conf" "$BOOT/deck.conf"; then
        cp "$MNT/deck.conf" "$BOOT/deck.conf.new" && mv "$BOOT/deck.conf.new" "$BOOT/deck.conf" && sync
        restart=settings
    fi
    image_close
    rm -f "$RUN/drive-busy"

    if [ -n "$update" ]; then
        old=$(version)
        if "$OPT/deck-update.sh" "$update"; then
            restart=update
        else
            last "$(basename "$u") couldn't be read, nothing installed"
        fi
        rm -rf "$DATA/incoming"
    fi
    case $restart in
    update)
        rm -f "$DATA/failed-log.txt"
        echo "Updated · $(version)" >"$RUN/notice" # the screen shows it as it starts
        if screen_restart; then
            last "updated $old -> $(version)"
        else
            { cat "$BOOT/deck-log.txt"; echo; echo "-- journal --"; journalctl -u deck.service -n 40 --no-pager; } \
                >"$DATA/failed-log.txt" 2>&1
            new=$(version)
            "$OPT/deck-update.sh" --rollback
            echo "Update failed · back to $(version)" >"$RUN/notice"
            screen_restart || say "the old version doesn't come up either"
            last "$new didn't start (see deck-log-failed.txt), went back to $(version)"
        fi
        ;;
    settings)
        echo "Settings applied" >"$RUN/notice"
        screen_restart || say "the screen didn't come up after the settings change"
        last "settings changed (deck.conf)"
        ;;
    esac
}

# Read the drive, give it back, and carry on with a newer deck-drive.sh if
# the update brought one (only if it's at least valid shell).
work() {
    process
    fill
    if ! drive_on; then
        say "drive=0: switching the drive off"
        echo >"$GADGET/UDC"
        exit 0
    fi
    attach
    if [ "$(cksum <"$OPT/deck-drive.sh")" != "$SELF" ]; then
        if sh -n "$OPT/deck-drive.sh"; then
            say "running the new deck-drive.sh"
            exec "$OPT/deck-drive.sh"
        fi
        SELF=$(cksum <"$OPT/deck-drive.sh")
        last "the new deck-drive.sh has a mistake in it, still running the old one"
    fi
}

drive_on || exit 0
mkdir -p "$DATA" "$RUN" || exit 1
[ -s "$IMG" ] || image_make || exit 1
gadget_up || {
    say "no USB gadget (is dtoverlay=dwc2 in config.txt?)"
    exit 1
}
SELF=$(cksum <"$OPT/deck-drive.sh")
if ! attached; then
    fill
    attach
fi
say "DECK is ready ($(version))"

was=
while :; do
    state=$(usb_state)
    if ! attached; then
        say "ejected"
        work
    elif [ "$was" = configured ] && [ "$state" != configured ]; then
        say "unplugged"
        detach
        work
    fi
    was=$state
    sleep "$TICK"
done
