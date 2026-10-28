Desk screen (Cyber60) - files on this SD card's boot partition
==============================================================

deck.conf            Settings: screen rotation, Mac/Windows key icon, demo.
                     Edit it here, put the card back, power on.
deck-log.txt         What the screen did at the last start (written by the Pi).
deck-update.tar.gz   Drop a new version here (from pi/image/build.sh or the
                     GitHub Actions "deck-update" artifact) instead of
                     re-flashing: the Pi installs it at the next start and
                     deletes the file. Saved wifi networks stay.
deck-update.log      When an update was installed.

Nobody can log in to this Pi; there is no user and no SSH. Wifi is set up on
the touch screen (swipe left, SET UP WIFI). Source:
https://github.com/abereg01/cyber60 (folder pi/).
