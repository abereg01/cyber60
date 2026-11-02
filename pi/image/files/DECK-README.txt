Desk screen (Cyber60) - files on this SD card's boot partition
==============================================================

deck.conf            Settings: screen rotation, Mac/Windows key icon, demo.
                     Edit it here, put the card back, power on.
deck-log.txt         What the screen did at the last start (written by the Pi).
deck-boot.txt        The Pi's report from the last boot, 30 s after power-on:
                     display, touch, power, the screen's journal, the kernel
                     log. Screen stayed black? This file says why.
deck-update.tar.gz   Drop a new version here (from pi/image/build.sh or the
                     GitHub Actions "deck-update" artifact) instead of
                     re-flashing: the Pi installs it at the next start and
                     deletes the file. Saved wifi networks stay.
deck-update.log      When an update was installed.

Neither log here after a power-on (wait a minute first)? Then Linux never got
as far as the screen: look at the screen itself during the boot (the rainbow,
then any error text) and at the green light on the Pi.

Easier: plug the Pi's USB-C port into a computer. It shows up as a drive
called DECK: drop deck-update.tar.gz or an edited deck.conf on it and eject
it. The screen restarts with it, no reboot (README.txt on DECK).

Nobody can log in to this Pi; there is no user and no SSH. Wifi is set up on
the touch screen (swipe left, SET UP WIFI). Source:
https://github.com/abereg01/cyber60 (folder pi/).
