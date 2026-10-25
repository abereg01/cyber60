# Top-level module CMakeLists for the Cyber60 + Prospector config.
#
# This repo is both a ZMK user config (config/) and a Zephyr module
# (zephyr/module.yml) that provides:
#   - the Cyber60 board (boards/4pplet/cyber60, from 4pplet/zmk-config-4pplet)
#   - the dongle shields (boards/shields/prospector_dongle*, screen code carried
#     over from the KLOR repo's klor_dongle_screen)
#   - the ST7789V display driver amendment, event types and caps-word override
#     the screen needs
#   - the layer-glow behavior (underglow follows the active layer)

zephyr_include_directories(include)
zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
zephyr_library()

# Layer glow: compiled on BOTH sides. The dongle invokes it on every layer
# change; ZMK forwards it (global locality) to the Cyber60, which owns the LEDs.
if(CONFIG_DT_HAS_ZMK_BEHAVIOR_LAYER_GLOW_ENABLED)
        target_sources(app PRIVATE src/behaviors/behavior_layer_glow.c)
endif()

if(CONFIG_SHIELD_PROSPECTOR_SCREEN)

        # ST7789V driver amendment (drivers/display/CMakeLists.txt uses
        # zephyr_library_amend() to override Zephyr's stock driver).
        add_subdirectory(${ZEPHYR_CURRENT_MODULE_DIR}/drivers/display)

        # Caps-word state change event + behavior override, so the screen can
        # light the modifier row while Caps Word is on.
        if(CONFIG_DT_HAS_ZMK_BEHAVIOR_CAPS_WORD_ENABLED)
                zephyr_library_sources(src/events/caps_word_state_changed.c)
                set_source_files_properties(
                        ${APPLICATION_SOURCE_DIR}/src/behaviors/behavior_caps_word.c
                        TARGET_DIRECTORY app
                        PROPERTIES HEADER_FILE_ONLY ON)
                target_sources(app PRIVATE src/behaviors/behavior_caps_word.c)
        endif()

        # Split-central status change event + observer (the screen and the
        # layer glow both need to know when the Cyber60 connects).
        zephyr_library_sources(src/events/split_central_status_changed.c)
        zephyr_library_sources(src/split/bluetooth/central_status_changed_observer.c)

endif()
