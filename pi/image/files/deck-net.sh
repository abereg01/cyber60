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
#
# The wifi can also go quiet while NM still says "connected" (2026-11-05: ten
# minutes with no traffic and not one line in the journal). So while connected
# it pings the gateway, but only one that has answered before: a network whose
# router never answers pings must not make it rejoin every two minutes. Two
# minutes of silence from a gateway that used to answer: leave and join again.
down=0
dead=0
seen= # gateways that have answered a ping
while sleep 30; do
    state=$(nmcli -g GENERAL.STATE device show wlan0 2>/dev/null | cut -d' ' -f2)
    if [ "$state" = "(connected)" ]; then
        [ "$down" -ge 2 ] && echo "wifi back"
        down=0
        gw=$(ip -4 route show default dev wlan0 2>/dev/null | awk '{print $3; exit}')
        [ -n "$gw" ] || continue
        if ping -c 2 -W 3 -I wlan0 "$gw" >/dev/null 2>&1; then
            case " $seen " in *" $gw "*) ;; *) seen="$seen $gw" ;; esac
            [ "$dead" -ge 4 ] && echo "gateway $gw answers again"
            dead=0
            continue
        fi
        case " $seen " in *" $gw "*) ;; *) continue ;; esac
        dead=$((dead + 1))
        [ $((dead % 4)) = 0 ] || continue # from 2 min on, every 2 min
        echo "connected, but the gateway $gw has been silent for $((dead / 2)) min: joining again"
        if [ $((dead % 20)) = 0 ]; then
            nmcli radio wifi off
            sleep 3
            nmcli radio wifi on
            sleep 10
        else
            nmcli device disconnect wlan0 >/dev/null 2>&1
        fi
        nmcli --wait 45 device connect wlan0 >/dev/null 2>&1 && echo "joined" || echo "join failed"
        continue
    fi
    dead=0
    if [ "$state" != "(disconnected)" ] || ! nmcli -g TYPE connection show | grep -qx 802-11-wireless; then
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
    # (its progress spinner would land in the log as "blob data")
    nmcli --wait 45 device connect wlan0 >/dev/null 2>&1 && echo "joined" || echo "join failed"
done
