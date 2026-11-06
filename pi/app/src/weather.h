#pragma once

// The weather from SMHI's open data: the point forecast (SNOW, snow1g v1),
// for the spot in deck.conf (weather=lat,lon). A worker thread does the
// network work; the state is read here on the LVGL thread (like spotify.h).
// weather.c is the real thing (Pi); the preview uses preview/weather_fake.c.
//
// SMHI has no precipitation nowcast for a point (only radar pictures), so
// "rain soon" comes from the forecast's hourly steps: SMHI's own weather
// symbol for each hour. Minutes are to the start of the first wet hour.

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define WEATHER_SOON_MIN 60 // rain_soon: wet within this many minutes

enum weather_rain {
    WEATHER_DRY,
    WEATHER_RAIN,
    WEATHER_SLEET,
    WEATHER_SNOW,
    WEATHER_THUNDER,
};

struct weather_state {
    bool configured; // weather= in deck.conf
    bool valid;      // a forecast covers now (false: not yet, or too old)
    float temp;      // °C, the forecast hour nearest now
    uint8_t symbol;  // SMHI symbol_code 1-27 (weather_symbol_name), 0 = unknown
    bool day_known;  // high/low: today (local), what's left of it plus what was seen
    float high, low;

    bool rain_soon;              // wet within WEATHER_SOON_MIN
    time_t rain_start;           // the first wet hour in the next 12 h (0 = none); <= now: wet now
    int rain_in_min;             // minutes to rain_start, 0 = now, -1 = none in 12 h
    enum weather_rain rain_kind; // of that hour
    uint8_t rain_level;          // 1 light, 2 moderate, 3 heavy (SMHI's symbol)
    float rain_mm_h;             // the ensemble mean for that hour
    uint8_t rain_chance;         // % probability of precipitation

    time_t updated; // when the forecast was last fetched, 0 = never
    time_t created; // when SMHI made it
    char message[96]; // the last problem worth showing, "" if none
    uint32_t version; // changes whenever anything above does
};

// lat_lon: deck.conf's "59.3293,18.0686" (decimal degrees); NULL/"" = off.
void weather_start(const char *lat_lon);
const struct weather_state *weather_state(void);
// SMHI's meaning of a symbol_code ("Light rain showers"), "" if unknown.
const char *weather_symbol_name(int symbol);

// The core without the network, for app/test/weather_cli.c: fills `out`'s
// forecast fields from an SMHI point forecast (JSON) as seen at `now`.
// False if the JSON isn't one.
bool weather_digest(const char *json, time_t now, struct weather_state *out);
