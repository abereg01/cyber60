# Local change vs 4pplet/zmk-config-4pplet: cyber60_led.c reacts to Bluetooth
# profile changes, which only exist when the board is the central. As the
# dongle's peripheral it has no profiles (and the event doesn't link), so skip it.
if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    target_sources(app PRIVATE cyber60_led.c)
endif()
target_sources(app PRIVATE cyber60_buzzer.c)
