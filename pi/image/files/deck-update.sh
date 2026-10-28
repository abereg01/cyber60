#!/bin/sh
# A new version dropped on the SD card's boot partition as deck-update.tar.gz
# (pi/image/build.sh makes it) replaces /opt/deck before the screen starts, and
# is then deleted. Wifi and everything else saved on the Pi stay.
U=/boot/firmware/deck-update.tar.gz
[ -f "$U" ] || exit 0
rm -rf /opt/deck.new && mkdir -p /opt/deck.new
if tar -xzf "$U" -C /opt/deck.new && [ -x /opt/deck.new/deck ]; then
    rm -rf /opt/deck.old
    mv /opt/deck /opt/deck.old && mv /opt/deck.new /opt/deck && rm -rf /opt/deck.old
    echo "$(date -Is) update applied" >>/boot/firmware/deck-update.log
else
    rm -rf /opt/deck.new
    echo "$(date -Is) update unreadable, ignored" >>/boot/firmware/deck-update.log
fi
rm -f "$U"
sync
