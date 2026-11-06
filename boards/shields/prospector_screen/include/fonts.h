#pragma once

#include <lvgl.h>

// Fonts in src/fonts/ (the Shoko layout has its own, in layouts/shoko/assets/).

#if defined(CONFIG_PROSPECTOR_STATUS_SCREEN_OPERATOR)

LV_FONT_DECLARE(FG_Medium_20);
LV_FONT_DECLARE(DINishExpanded_Light_36);
LV_FONT_DECLARE(FR_Medium_32);
LV_FONT_DECLARE(Symbols_Bold_26);
LV_FONT_DECLARE(Klor_Mixed_32);

#endif
