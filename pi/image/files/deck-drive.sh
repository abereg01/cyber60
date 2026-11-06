#!/bin/sh
# The DECK drive: the Pi shows up as a small USB drive on the computer its
# USB-C port is plugged into. Copy a new version (GitHub Actions' deck-update
# download as it is, the folder a Mac unpacks it into, or deck-update.tar.gz;
# see deck-update.sh) or an edited deck.conf onto it and
# eject it: the Pi installs it, restarts the screen (no reboot) and puts the
# drive back with DECK-STATUS.txt and the screen's log on it. Pulling the cable
# instead of ejecting works too. drive=0 in deck.conf turns the drive off.
#
# The same port is the Pi's keyboard to the computer (the keyboard link, the
# screen's kblink.c): one composite gadget with a boot keyboard (/dev/hidg0)
# and the media keys and mouse (/dev/hidg1) next to the drive. drive=0
# takes only the drive away.
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
CONF=$GADGET/configs/c.1
LAST=$DATA/drive-last.txt # what happened last, for DECK-STATUS.txt

say() { echo "deck-drive: $*"; }
last() { echo "$(date '+%Y-%m-%d %H:%M')  $*" >"$LAST"; say "$*"; }
version() { head -n1 "$OPT/VERSION" 2>/dev/null || echo unknown; }

drive_on() { ! grep -Eq '^[[:space:]]*drive[[:space:]]*=[[:space:]]*0' "$BOOT/deck.conf" 2>/dev/null; }

# Two settings don't stay in deck.conf, where any computer the Pi is plugged
# into can read it: the Shelly cloud key (it controls every device on its
# Shelly account) and the published calendar's link (anyone with it can read
# the calendar). A shelly_key=... line is moved into $DATA/shelly-key
# (root-only, where the screen reads it) and blanked; calendar_url=... the
# same into $DATA/calendar-url. True if there was one.
take_secret() { # deck.conf setting file
    k=$(sed -n "s/^[[:space:]]*$2[[:space:]]*=[[:space:]]*\([^[:space:]#]\{1,\}\).*/\1/p" "$1" 2>/dev/null |
        tail -n1)
    [ -n "$k" ] || return 1
    (umask 077 && printf '%s\n' "$k" >"$DATA/$3.new") && mv "$DATA/$3.new" "$DATA/$3" &&
        sed -i "s/^[[:space:]]*$2[[:space:]]*=.*/$2=  # saved on the Pi/" "$1" && sync
}

# Both of them. $saved says what was saved, for the notice and the log.
take_key() { # deck.conf
    saved=
    take_secret "$1" shelly_key shelly-key && saved="Shelly key"
    take_secret "$1" calendar_url calendar-url && saved="${saved:+$saved + }Calendar link"
    [ -n "$saved" ]
}

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
    echo "Drop a new version here (GitHub's deck-update download as it is, zip or"
    echo "unpacked folder) or an edited deck.conf, then eject. README.txt has more."
}

# The Pi's side of the drive, refreshed before the computer gets it back.
fill() {
    image_open || return 1
    status >"$RUN/status.txt"
    put "$RUN/status.txt" DECK-STATUS.txt
    put "$BOOT/deck.conf" deck.conf
    put "$BOOT/deck-log.txt" deck-log.txt
    put "$BOOT/deck-boot.txt" deck-boot.txt
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

# One composite gadget: the keyboard, the media keys and mouse, the drive.
# Windows, macOS and Linux need no driver for any of them. The keyboard
# comes first, so its interface number stays the same with drive=0.

# The boot keyboard (HID 1.11, appendix B): modifiers, a reserved byte, six
# keys 0-255 (F13-F24 too), and the five LEDs back from the computer. In the
# boot layout, so a PC's firmware (UEFI setup, the BitLocker PIN) reads it.
KBD_DESC='05 01 09 06 a1 01 05 07 19 e0 29 e7 15 00 25 01 75 01 95 08 81 02 95 01 75 08 81 03 95 05 75 01
05 08 19 01 29 05 91 02 95 01 75 03 91 03 95 06 75 08 15 00 26 ff 00 05 07 19 00 29 ff 81 00 c0'
# Consumer (report ID 2: six 16-bit usages) and mouse (ID 3: 5 buttons, X, Y,
# wheel, pan, 16 bits each): the Cyber60's own (pi/kbd/), so its reports
# pass through byte for byte.
EXTRA_DESC='05 0c 09 01 a1 01 85 02 05 0c 15 00 26 ff 0f 19 00 2a ff 0f 75 10 95 06 81 00 c0
05 01 09 02 a1 01 85 03 09 01 a1 00 05 09 19 01 29 05 15 00 25 01 75 01 95 05 81 02 75 03 95 01 81 03
05 01 09 30 09 31 16 00 80 26 ff 7f 75 10 95 02 81 06 a1 02 09 38 16 00 80 26 ff 7f 35 00 45 00 75 10
95 01 81 06 c0 a1 02 05 0c 0a 38 02 16 00 80 26 ff 7f 35 00 45 00 75 10 95 01 81 06 c0 c0 c0'

bytes() { # hex -> binary (dash's printf has no \x)
    for b in $1; do
        # shellcheck disable=SC2059 # the octal escape is the format
        printf "\\$(printf %03o "0x$b")"
    done
}

hid_fn() { # function, subclass, protocol, longest report, descriptor
    f=$GADGET/functions/$1
    mkdir -p "$f" && echo "$2" >"$f/subclass" && echo "$3" >"$f/protocol" && echo "$4" >"$f/report_length" &&
        bytes "$5" >"$f/report_desc"
}

# The functions the computer gets, in this order; and those it has now.
wanted() { echo "hid.usb0 hid.usb1$(drive_on && echo ' mass_storage.0')"; }
linked() {
    for l in "$CONF"/hid.* "$CONF"/mass_storage.*; do
        [ -L "$l" ] && echo "${l##*/}"
    done | sort | tr '\n' ' '
}

gadget_up() {
    if [ "$(cat "$GADGET/idVendor" 2>/dev/null)" != 0x1d6b ]; then
        modprobe libcomposite && mkdir -p "$GADGET" && cd "$GADGET" || return 1
        echo 0x1d6b >idVendor  # Linux Foundation
        echo 0x0104 >idProduct # Multifunction Composite Gadget
        echo 0x0200 >bcdUSB
        mkdir -p strings/0x409 "$CONF/strings/0x409" functions/mass_storage.0
        echo Cyber60 >strings/0x409/manufacturer
        echo "Desk screen" >strings/0x409/product
        echo DECK >"$CONF/strings/0x409/configuration"
        echo 500 >"$CONF/MaxPower"
        echo 1 >"$LUN/removable"
        echo 0 >"$LUN/ro"
        echo "Cyber60 DECK" >"$LUN/inquiry_string"
        cd / || return 1
    fi
    if [ "$(wanted | tr ' ' '\n' | sort | tr '\n' ' ')" != "$(linked)" ]; then
        # Functions come and go only while no computer sees the gadget: the
        # port lets go for a moment (the drive comes back as a new disk).
        echo >"$GADGET/UDC" 2>/dev/null
        for l in "$CONF"/hid.* "$CONF"/mass_storage.*; do
            [ ! -L "$l" ] || rm "$l"
        done
        # Version 2 (2026-11): the keyboard joined the drive. A serial number
        # of its own, so a computer that knew the drive alone sets the device
        # up afresh (Windows ties a device's drivers to VID, PID and serial).
        echo 0x0200 >"$GADGET/bcdDevice"
        echo "$(tr -d '\000' 2>/dev/null </proc/device-tree/serial-number)-2" >"$GADGET/strings/0x409/serialnumber"
        hid_fn hid.usb0 1 1 8 "$KBD_DESC" && hid_fn hid.usb1 0 0 16 "$EXTRA_DESC" || return 1
        for f in $(wanted); do
            ln -s "$GADGET/functions/$f" "$CONF/$f" || return 1
        done
    fi
    [ -n "$(cat "$GADGET/UDC" 2>/dev/null)" ] && return 0 # bound before deck-drive.sh (re)started
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
    # The newest new version: a tarball, GitHub's zip, or the folder a Mac
    # unpacked it into (deck-update.sh --find). Copied off the drive before
    # the drive goes back to the computer, then off the drive.
    u=$("$OPT/deck-update.sh" --find "$MNT")
    if [ -n "$u" ]; then
        rm -rf "$DATA/incoming" && mkdir -p "$DATA/incoming"
        cp -R "$u" "$DATA/incoming/" && update=$DATA/incoming/$(basename "$u")
        "$OPT/deck-update.sh" --clear "$MNT"
    fi
    key=
    [ -f "$MNT/deck.conf" ] && take_key "$MNT/deck.conf" && key=$saved
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
        if [ -n "$key" ]; then
            echo "$key saved" >"$RUN/notice"
        else
            echo "Settings applied" >"$RUN/notice"
        fi
        screen_restart || say "the screen didn't come up after the settings change"
        last "settings changed (deck.conf${key:+, $key saved on the Pi})"
        ;;
    esac
}

# Read the drive, give it back, and carry on with a newer deck-drive.sh if
# the update brought one (only if it's at least valid shell).
work() {
    process
    fill
    if ! drive_on; then
        say "drive=0: switching the drive off (the keyboard stays)"
        gadget_up
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

mkdir -p "$DATA" "$RUN" || exit 1
# A key put in deck.conf on the SD card itself (not through DECK).
if take_key "$BOOT/deck.conf"; then
    say "$saved moved out of deck.conf"
    systemctl try-restart deck.service
fi
if drive_on; then
    [ -s "$IMG" ] || image_make || exit 1
fi
gadget_up || {
    say "no USB gadget (is dtoverlay=dwc2 in config.txt?)"
    exit 1
}
drive_on || {
    say "drive=0: the keyboard only, no DECK drive"
    exit 0
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
