#!/usr/bin/env bash
# Build the SD card image: the official Raspberry Pi OS Lite (base-image.txt)
# with the screen added and everything that would get in its way switched off.
# Works on copies of the partitions with mtools and debugfs, so it needs no
# root, no loop devices and no ARM emulation (nothing is installed: Pi OS Lite
# already has NetworkManager, libdrm and libcurl).
#   pi/build/deck-<date>.img.xz   flash with Raspberry Pi Imager ("Use custom")
# Runs inside the deck-tools container after build-app.sh.
set -euo pipefail

PI=$(cd "$(dirname "$0")/.." && pwd)
CACHE=${DECK_CACHE:?}
OUT=$PI/build
F=$PI/image/files
. "$PI/image/base-image.txt"

# The base image, downloaded once and checked.
mkdir -p "$CACHE/raspios"
BASE=$CACHE/raspios/$(basename "$URL")
if ! echo "$SHA256  $BASE" | sha256sum -c --quiet 2>/dev/null; then
    echo "==> downloading $(basename "$URL")"
    curl -fsSL -o "$BASE.part" "$URL"
    echo "$SHA256  $BASE.part" | sha256sum -c --quiet
    mv "$BASE.part" "$BASE"
fi

# Everything happens in the container's own scratch space (fast local disk);
# only the finished .img.xz goes to pi/build.
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
IMG=$W/deck.img
FINAL=$OUT/deck-$(date +%Y%m%d).img.xz
echo "==> unpacking"
xz -dc "$BASE" >"$IMG"

part() { sfdisk -d "$IMG" | awk -v p="$IMG$1 " 'index($0, p) == 1 { gsub(/,/, ""); print $4, $6 }'; }
read -r BOOT_START BOOT_SIZE < <(part 1)
read -r ROOT_START ROOT_SIZE < <(part 2)
extract() { dd if="$IMG" of="$1" bs=4M iflag=skip_bytes,count_bytes skip=$(($2 * 512)) count=$(($3 * 512)) status=none; }
insert() { dd if="$1" of="$IMG" bs=4M oflag=seek_bytes seek=$(($2 * 512)) conv=notrunc status=none; }
extract "$W/boot.vfat" "$BOOT_START" "$BOOT_SIZE"
extract "$W/root.ext4" "$ROOT_START" "$ROOT_SIZE"

echo "==> boot partition"
export MTOOLS_SKIP_CHECK=1
mtype -i "$W/boot.vfat" ::/config.txt >"$W/config.txt"
cat >>"$W/config.txt" <<'CONF'

[all]
# Desk screen (pi/image/build-image.sh): no rainbow at power-on, no sound or
# camera. The Touch Display is found by display_auto_detect above.
disable_splash=1
dtparam=audio=off
camera_auto_detect=0
# The USB-C port as a USB device (the DECK drive, deck-drive.sh).
dtoverlay=dwc2,dr_mode=peripheral
CONF
# Kernel messages stay off the screen (serial only); wifi country Sweden.
mtype -i "$W/boot.vfat" ::/cmdline.txt | tr -d '\n' | sed 's/ console=tty1//' >"$W/cmdline.txt"
printf ' quiet loglevel=3 logo.nologo vt.global_cursor_default=0 consoleblank=0 cfg80211.ieee80211_regdom=SE\n' \
    >>"$W/cmdline.txt"
mcopy -o -i "$W/boot.vfat" "$W/config.txt" "$W/cmdline.txt" "$F/deck.conf" "$F/DECK-README.txt" ::/

echo "==> root partition"
# The DECK drive (deck-drive.sh) uses these from Pi OS Lite; stop here if a
# newer base image ever drops one.
KMOD=$(debugfs -R 'ls /usr/lib/modules' "$W/root.ext4" 2>/dev/null | grep -oE '[^ ]+-rpi-v8' | head -n1)
G=/usr/lib/modules/$KMOD/kernel/drivers/usb/gadget
for f in /usr/sbin/mkfs.fat /usr/sbin/fsck.fat /usr/bin/unzip /usr/bin/vcgencmd \
    "$G/libcomposite.ko.xz" "$G/function/usb_f_mass_storage.ko.xz"; do
    debugfs -R "stat $f" "$W/root.ext4" 2>/dev/null | grep -q '^Inode:' || {
        echo "the base image has no $f (the DECK drive needs it)" >&2
        exit 1
    }
done
D=$OUT/opt-deck
R=$W/debugfs.cmd
own() { printf 'sif %s uid 0\nsif %s gid 0\nsif %s mode %s\n' "$1" "$1" "$1" "$2" >>"$R"; }
put() { # local file, directory in the image, mode
    printf 'cd %s\nwrite %s %s\n' "$2" "$1" "$(basename "$1")" >>"$R"
    own "$2/$(basename "$1")" "$3"
}
: >"$R"
printf 'mkdir /opt/deck\nmkdir /opt/deck/share\nmkdir /opt/deck/units\n' >>"$R"
own /opt/deck 040755
own /opt/deck/share 040755
own /opt/deck/units 040755
put "$D/deck" /opt/deck 0100755
put "$D/deck-update.sh" /opt/deck 0100755
put "$D/deck-drive.sh" /opt/deck 0100755
put "$D/VERSION" /opt/deck 0100644
put "$D/DRIVE-README.txt" /opt/deck 0100644
for f in "$D"/share/*; do put "$f" /opt/deck/share 0100644; done
for f in "$D"/units/*; do put "$f" /opt/deck/units 0100644; done
put "$F/deck.service" /etc/systemd/system 0100644
put "$F/deck-drive.service" /etc/systemd/system 0100644
printf 'symlink /etc/systemd/system/sysinit.target.wants/deck.service /etc/systemd/system/deck.service\n' >>"$R"
printf 'symlink /etc/systemd/system/multi-user.target.wants/deck-drive.service /etc/systemd/system/deck-drive.service\n' >>"$R"

# Off: the first-boot user wizard (nobody logs in; it would also take the
# screen), cloud-init (nothing to set up, slow), avahi (announces the Pi on
# the network), the ssh switch, the login prompt on the screen's console.
for u in multi-user.target.wants/userconfig.service multi-user.target.wants/avahi-daemon.service \
    sockets.target.wants/avahi-daemon.socket multi-user.target.wants/sshswitch.service; do
    printf 'rm /etc/systemd/system/%s\n' "$u" >>"$R"
done
printf 'symlink /etc/systemd/system/getty@tty1.service /dev/null\n' >>"$R"
: >"$W/cloud-init.disabled"
put "$W/cloud-init.disabled" /etc/cloud 0100644

# Hostname: what a wifi router lists the Pi as.
echo deck >"$W/hostname"
sed 's/raspberrypi/deck/' <(debugfs -R 'cat /etc/hosts' "$W/root.ext4" 2>/dev/null) >"$W/hosts"
for f in hostname hosts; do
    printf 'cd /etc\nrm %s\nwrite %s %s\n' "$f" "$W/$f" "$f" >>"$R"
    own "/etc/$f" 0100644
done

debugfs -w -f "$R" "$W/root.ext4" 2>&1 | grep -iE 'error|not found|exists|could not' && exit 1
e2fsck -fy "$W/root.ext4" >/dev/null || [ $? -le 1 ]
debugfs -R 'stat /opt/deck/deck' "$W/root.ext4" 2>/dev/null | head -3

insert "$W/boot.vfat" "$BOOT_START"
insert "$W/root.ext4" "$ROOT_START"

echo "==> compressing"
rm -f "$OUT"/deck-*.img.xz
xz -T0 -6 -c "$IMG" >"$FINAL"
ls -la "$FINAL"
