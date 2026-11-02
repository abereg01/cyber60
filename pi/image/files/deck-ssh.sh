#!/bin/sh
# BUILD PHASE ONLY: SSH into the Pi while it sits at home, so changes can be
# tried live instead of through the SD card. Root, with the keys in
# /opt/deck/ssh/authorized_keys only (OMV's), never a password. On unless
# deck.conf says ssh=0; read at every power-on (deck-ssh.service).
# Before the Pi goes to work: delete this file, deck-ssh.service and ssh/
# from pi/image/files (and their lines in build-app.sh / build-image.sh), and
# put ssh=0 on the Pi (that also takes the key off it).
BOOT=${DECK_BOOT:-/boot/firmware}
OPT=${DECK_OPT:-/opt/deck}
KEYS=$OPT/ssh/authorized_keys

on=1
v=$(sed -n 's/^[[:space:]]*ssh[[:space:]]*=[[:space:]]*\([01]\).*/\1/p' "$BOOT/deck.conf" 2>/dev/null | tail -n1)
[ -n "$v" ] && on=$v

if [ "$on" = 1 ] && [ -s "$KEYS" ]; then
    install -d -m 700 /root/.ssh
    install -m 600 "$KEYS" /root/.ssh/authorized_keys
    printf '%s\n' '# deck-ssh.sh: keys only' 'PermitRootLogin prohibit-password' 'PasswordAuthentication no' \
        'KbdInteractiveAuthentication no' >/etc/ssh/sshd_config.d/deck.conf
    ssh-keygen -A >/dev/null # Pi OS makes the host keys at first boot through cloud-init, which is off
    systemctl enable ssh.service >/dev/null 2>&1
    systemctl start --no-block ssh.service
    echo "deck-ssh: on (root, key only)"
else
    systemctl disable ssh.service ssh.socket >/dev/null 2>&1
    systemctl stop --no-block ssh.service ssh.socket
    rm -f /root/.ssh/authorized_keys /etc/ssh/sshd_config.d/deck.conf
    echo "deck-ssh: off"
fi
