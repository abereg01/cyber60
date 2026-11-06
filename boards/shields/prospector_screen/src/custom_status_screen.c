#include <lvgl.h>

#if defined(CONFIG_PROSPECTOR_STATUS_SCREEN_OPERATOR)
#include "layouts/operator/status_screen.c"
#elif defined(CONFIG_PROSPECTOR_STATUS_SCREEN_SHOKO)
#include "layouts/shoko/status_screen.c"
#else
#error "No status screen layout selected"
#endif
