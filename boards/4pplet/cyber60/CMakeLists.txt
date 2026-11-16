# Local change vs 4pplet/zmk-config-4pplet (2026-11-08): this Cyber60 has no
# LEDs fitted (no caps-lock RGB LED, no underglow), so cyber60_led.c isn't
# built. cyber60_buzzer.c isn't either: src/buzzer.c at the repo root replaces
# it (4pplet's beeps on Bluetooth profile changes, which happen on the dongle,
# and doesn't link on a split peripheral).
