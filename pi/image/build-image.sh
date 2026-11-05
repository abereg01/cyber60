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
# Desk screen (pi/image/build-image.sh): no sound or camera. The rainbow at
# power-on stays: it is the only sign that the Pi found the card and started.
# The Touch Display is found by display_auto_detect above.
dtparam=audio=off
camera_auto_detect=0
# The USB-C port as a USB device (the DECK drive, deck-drive.sh).
dtoverlay=dwc2,dr_mode=peripheral
CONF
# A quiet boot, but errors still reach the screen (console=tty1 stays), so a
# boot that stops before the app shows why instead of staying black. Wifi
# country Sweden.
mtype -i "$W/boot.vfat" ::/cmdline.txt | tr -d '\n' >"$W/cmdline.txt"
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
put "$D/deck-report.sh" /opt/deck 0100755
put "$D/deck-net.sh" /opt/deck 0100755
put "$D/VERSION" /opt/deck 0100644
put "$D/DRIVE-README.txt" /opt/deck 0100644
for f in "$D"/share/*; do put "$f" /opt/deck/share 0100644; done
for f in "$D"/units/*; do put "$f" /opt/deck/units 0100644; done
put "$F/deck.service" /etc/systemd/system 0100644
put "$F/deck-drive.service" /etc/systemd/system 0100644
put "$F/deck-report.service" /etc/systemd/system 0100644
put "$F/deck-net.service" /etc/systemd/system 0100644
# BUILD PHASE ONLY: SSH with OMV's key (files/deck-ssh.sh). Remove before work.
printf 'mkdir /opt/deck/ssh\n' >>"$R"
own /opt/deck/ssh 040755
put "$D/deck-ssh.sh" /opt/deck 0100755
put "$D/ssh/authorized_keys" /opt/deck/ssh 0100644
put "$F/deck-ssh.service" /etc/systemd/system 0100644
for w in sysinit.target.wants/deck.service multi-user.target.wants/deck-drive.service \
    multi-user.target.wants/deck-report.service multi-user.target.wants/deck-net.service \
    multi-user.target.wants/deck-ssh.service; do
    printf 'symlink /etc/systemd/system/%s /etc/systemd/system/%s\n' "$w" "${w#*/}" >>"$R"
done

# Off: the first-boot user wizard (nobody logs in; it would also take the
# screen), cloud-init (nothing to set up, slow), avahi (announces the Pi on
# the network), the ssh switch, the login prompt on the screen's console.
for u in multi-user.target.wants/userconfig.service multi-user.target.wants/avahi-daemon.service \
    sockets.target.wants/avahi-daemon.socket multi-user.target.wants/sshswitch.service; do
    printf 'rm /etc/systemd/system/%s\n' "$u" >>"$R"
done
printf 'symlink /etc/systemd/system/getty@tty1.service /dev/null\n' >>"$R"
# Ctrl+Alt+Del would restart the Pi: the keyboard can sit on its USB port.
printf 'symlink /etc/systemd/system/ctrl-alt-del.target /dev/null\n' >>"$R"
: >"$W/cloud-init.disabled"
put "$W/cloud-init.disabled" /etc/cloud 0100644

# Hostname: what a wifi router lists the Pi as.
echo deck >"$W/hostname"
sed 's/raspberrypi/deck/' <(debugfs -R 'cat /etc/hosts' "$W/root.ext4" 2>/dev/null) >"$W/hosts"
for f in hostname hosts; do
    printf 'cd /etc\nrm %s\nwrite %s %s\n' "$f" "$W/$f" "$f" >>"$R"
    own "/etc/$f" 0100644
done

# Wifi on: Pi OS ships NetworkManager with it switched off until Imager or
# raspi-config sets it up (the screen's wifi.c switches it on too, for cards
# made before this).
printf '[main]\nWirelessEnabled=true\n' >"$W/NetworkManager.state"
printf 'cd /var/lib/NetworkManager\nrm NetworkManager.state\nwrite %s NetworkManager.state\n' \
    "$W/NetworkManager.state" >>"$R"
own /var/lib/NetworkManager/NetworkManager.state 0100644
# No wifi power saving: on the Pi's chip it stalls connections (seen
# 2026-11-02: a copy over SSH hung, ping went quiet).
printf '%s\n' '# Desk screen: wifi power saving stalls connections on the Pi' '[connection]' 'wifi.powersave=2' \
    >"$W/deck-wifi.conf"
put "$W/deck-wifi.conf" /etc/NetworkManager/conf.d 0100644
# The log kept across restarts (capped), so a restart or crash can be read
# afterwards (deck-boot.txt, or over SSH). Pi OS has /var/log/journal but
# keeps the log in RAM by its own setting; this one in /etc wins.
printf '%s\n' '# Desk screen: keep the log across restarts' '[Journal]' 'Storage=persistent' 'SystemMaxUse=32M' \
    >"$W/journald-deck.conf"
printf 'mkdir /etc/systemd/journald.conf.d\n' >>"$R"
own /etc/systemd/journald.conf.d 040755
printf 'cd /etc/systemd/journald.conf.d\nwrite %s deck.conf\n' "$W/journald-deck.conf" >>"$R"
own /etc/systemd/journald.conf.d/deck.conf 0100644

debugfs -w -f "$R" "$W/root.ext4" 2>&1 | grep -iE 'error|not found|exists|could not' && exit 1
e2fsck -fy "$W/root.ext4" >/dev/null || [ $? -le 1 ]
debugfs -R 'stat /opt/deck/deck' "$W/root.ext4" 2>/dev/null | head -3

insert "$W/boot.vfat" "$BOOT_START"
insert "$W/root.ext4" "$ROOT_START"

echo "==> compressing"
rm -f "$OUT"/deck-*.img.xz
xz -T0 -6 -c "$IMG" >"$FINAL"
ls -la "$FINAL"
