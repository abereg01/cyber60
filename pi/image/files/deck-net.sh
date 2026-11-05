#!/bin/sh
# deck-net.sh: keeps the desk screen on the wifi.
#
# NetworkManager gives up on a wifi it failed to rejoin a few times in a row:
# it decides the password must be wrong and waits for someone to type a new
# one, which on a screen with no one logged in never happens. Seen 2026-11-04:
# the Pi tried another access point, was refused seven times in a minute and
# then sat off the network for 9 hours with the screen still on.
#
# This looks every 30 s. Wifi disconnected for a minute: ask NM to join again
# (that also lifts its "wrong password" block). Still down after 10 minutes:
# switch the wifi radio off and on as well, every 10 minutes. It leaves NM
# alone while it is busy connecting (the screen's wifi page may be joining a
# network) and when the radio is switched off.
down=0
while sleep 30; do
    if [ "$(nmcli -g GENERAL.STATE device show wlan0 2>/dev/null | cut -d' ' -f2)" != "(disconnected)" ] ||
        ! nmcli -g TYPE connection show | grep -qx 802-11-wireless; then
        [ "$down" -ge 2 ] && echo "wifi back"
        down=0
        continue
    fi
    down=$((down + 1))
    [ "$down" -ge 2 ] || continue
    if [ $((down % 20)) = 0 ]; then
        echo "wifi still down after $((down / 2)) min: radio off and on"
        nmcli radio wifi off
        sleep 3
        nmcli radio wifi on
        sleep 10
    fi
    echo "wifi down for $((down / 2)) min: asking NetworkManager to join again"
    nmcli --wait 45 device connect wlan0 || true
done
