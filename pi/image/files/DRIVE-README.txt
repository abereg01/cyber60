DECK - the desk screen's drive
==============================

Put a new version or new settings on this drive, then EJECT it
(Windows: right-click DECK > Eject. Mac: the eject arrow next to DECK).
The Pi reads the drive, the screen says "Reading DECK..." and restarts
with the change a few seconds later. No reboot. Then DECK comes back.

What you can drop here
  deck-update.tar.gz    a new version (pi/image/build.sh update), or the
  deck-update.zip       "deck-update" download from GitHub Actions as it is
  deck.conf             edit the copy here (Notepad is fine) to change a
                        setting. The Pi saves it to its SD card too.

What the Pi puts here
  DECK-STATUS.txt       the version running and what happened last
  deck-log.txt          what the screen did when it last started
  deck-log-failed.txt   only after a new version that didn't start: its log.
                        The Pi went back to the version before by itself.

Pulling the cable instead of ejecting works too, but eject if you can.
Hide this drive: drive=0 in deck.conf.
