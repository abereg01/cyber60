/*
 * The preview's weather (instead of src/weather.c): a grey afternoon with
 * light rain 25 minutes away, plus a preview-only switch for the other
 * states. Times are from the real clock, as the Pi's would be.
 */
#include <time.h>

#include "weather.h"

static struct weather_state st = {
    .configured = true,
    .valid = true,
    .temp = 7.4f,
    .symbol = 6,
    .day_known = true,
    .high = 9.1f,
    .low = 3.2f,
    .rain_kind = WEATHER_RAIN,
    .rain_level = 1,
    .rain_mm_h = 0.4f,
    .rain_chance = 70,
};

void weather_fake_rain(int minutes);

static void fill(void) {
    if (!st.updated) {
        st.updated = time(NULL) - 120;
        st.created = st.updated - 900;
        weather_fake_rain(25);
    }
}

void weather_start(const char *lat_lon) { fill(); }

const struct weather_state *weather_state(void) {
    fill();
    return &st;
}

const char *weather_symbol_name(int s) {
    return s == 6 ? "Overcast" : s == 18 ? "Light rain" : s == 1 ? "Clear sky" : "";
}

bool weather_digest(const char *json, time_t now, struct weather_state *out) { return false; }

// Preview only: rain in that many minutes (0 = raining now, -1 = dry).
void weather_fake_rain(int minutes) {
    fill();
    st.rain_in_min = minutes;
    st.rain_soon = minutes >= 0 && minutes <= WEATHER_SOON_MIN;
    st.rain_start = minutes >= 0 ? time(NULL) + minutes * 60 : 0;
    st.symbol = minutes == 0 ? 18 : 6;
    st.version++;
}
