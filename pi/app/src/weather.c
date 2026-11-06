/*
 * The weather from SMHI's open data (CC BY 4.0: show "SMHI" with it).
 *
 *   GET https://opendata-download-metfcst.smhi.se/api/category/snow1g/version/1
 *       /geotype/point/lon/<lon>/lat/<lat>/data.json?timeseries=36&parameters=...
 *   → {"createdTime", "referenceTime", "timeSeries": [{"time", "intervalParametersStartTime",
 *      "data": {"air_temperature", "symbol_code", "precipitation_amount_mean", ...}}, ...]}
 *
 * SNOW (snow1g) replaced the old pmp3g forecast, which SMHI switched off on
 * 2026-03-31. Its first 48 steps are an hour apart; each step's time is
 * when the temperature and symbol hold, and its precipitation is the sum
 * over the hour before (intervalParametersStartTime → time). SMHI makes a new
 * forecast every hour and its servers cache one for an hour, so polling
 * every 15 minutes is plenty. SMHI has no precipitation nowcast for a point,
 * only radar pictures, so "rain soon" is the first hour SMHI's own symbol
 * calls wet: hourly, not to the minute.
 *
 * One worker thread fetches, keeps the last good forecast through wifi
 * drops and errors (backing off), and every minute works out from it what
 * "now" means: the nearest hour, today's high/low, the minutes to rain.
 *
 * SPDX-License-Identifier: MIT
 */

#include "weather.h"

#include <cJSON.h>
#include <curl/curl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <lvgl.h>

#include "deck.h"

#define API "https://opendata-download-metfcst.smhi.se/api/category/snow1g/version/1/geotype/point"
#define PARAMS "air_temperature,symbol_code,precipitation_amount_mean,probability_of_precipitation"
#define STEPS 36           // hours: the rest of today and some of tomorrow
#define POLL_S (15 * 60)
#define RETRY_S 30         // after a failure, doubling up to POLL_S
#define TICK_S 60          // "now" moves on between fetches
#define NEAR_S (90 * 60)   // the nearest step must be this close to count as now
#define STALE_S (12 * 3600) // older than this, the forecast isn't shown
#define AHEAD_S (12 * 3600) // how far ahead to look for rain

static void copy(char *dst, size_t size, const char *src) { snprintf(dst, size, "%s", src ? src : ""); }

static time_t mono_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

// The Pi has no RTC: until timesyncd has the network time, "today" is wrong.
// Elsewhere (tests on OMV) there's no timesyncd and the clock is right.
static bool clock_ok(void) {
    return access("/run/systemd/timesync/synchronized", F_OK) == 0 || access("/run/systemd/timesync", F_OK) != 0;
}

// ── SMHI's weather symbols (opendata.smhi.se/metfcst/snow1gv1/parameters) ────

static const struct {
    const char *name;
    uint8_t kind, level;
} symbols[28] = {
    [1] = {"Clear sky", WEATHER_DRY, 0},
    [2] = {"Nearly clear sky", WEATHER_DRY, 0},
    [3] = {"Variable cloudiness", WEATHER_DRY, 0},
    [4] = {"Halfclear sky", WEATHER_DRY, 0},
    [5] = {"Cloudy sky", WEATHER_DRY, 0},
    [6] = {"Overcast", WEATHER_DRY, 0},
    [7] = {"Fog", WEATHER_DRY, 0},
    [8] = {"Light rain showers", WEATHER_RAIN, 1},
    [9] = {"Moderate rain showers", WEATHER_RAIN, 2},
    [10] = {"Heavy rain showers", WEATHER_RAIN, 3},
    [11] = {"Thunderstorm", WEATHER_THUNDER, 3},
    [12] = {"Light sleet showers", WEATHER_SLEET, 1},
    [13] = {"Moderate sleet showers", WEATHER_SLEET, 2},
    [14] = {"Heavy sleet showers", WEATHER_SLEET, 3},
    [15] = {"Light snow showers", WEATHER_SNOW, 1},
    [16] = {"Moderate snow showers", WEATHER_SNOW, 2},
    [17] = {"Heavy snow showers", WEATHER_SNOW, 3},
    [18] = {"Light rain", WEATHER_RAIN, 1},
    [19] = {"Moderate rain", WEATHER_RAIN, 2},
    [20] = {"Heavy rain", WEATHER_RAIN, 3},
    [21] = {"Thunder", WEATHER_THUNDER, 2},
    [22] = {"Light sleet", WEATHER_SLEET, 1},
    [23] = {"Moderate sleet", WEATHER_SLEET, 2},
    [24] = {"Heavy sleet", WEATHER_SLEET, 3},
    [25] = {"Light snowfall", WEATHER_SNOW, 1},
    [26] = {"Moderate snowfall", WEATHER_SNOW, 2},
    [27] = {"Heavy snowfall", WEATHER_SNOW, 3},
};

const char *weather_symbol_name(int s) { return s >= 1 && s <= 27 ? symbols[s].name : ""; }

// ── The forecast → what the screen shows ─────────────────────────────────────

static time_t utc_time(const char *iso) { // "2026-11-05T17:00:00Z"
    struct tm tm = {0};
    if (!iso || sscanf(iso, "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min,
                       &tm.tm_sec) != 6) {
        return 0;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return timegm(&tm);
}

static double num(const cJSON *data, const char *key, double fallback) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(data, key);
    return cJSON_IsNumber(v) && v->valuedouble != 9999 ? v->valuedouble : fallback; // 9999 = missing
}

static int local_day(time_t t) {
    struct tm tm;
    localtime_r(&t, &tm);
    return tm.tm_year * 1000 + tm.tm_yday;
}

bool weather_digest(const char *json, time_t now, struct weather_state *out) {
    cJSON *o = json ? cJSON_Parse(json) : NULL;
    const cJSON *series = cJSON_GetObjectItemCaseSensitive(o, "timeSeries");
    if (!cJSON_IsArray(series)) {
        cJSON_Delete(o);
        return false;
    }
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(o, "createdTime");
    out->created = utc_time(cJSON_IsString(c) ? c->valuestring : NULL);
    out->valid = out->day_known = out->rain_soon = false;
    out->symbol = 0;
    out->rain_start = 0;
    out->rain_in_min = -1;
    out->rain_kind = WEATHER_DRY;
    out->rain_level = out->rain_chance = 0;
    out->rain_mm_h = 0;

    time_t nearest = NEAR_S + 1;
    int today = local_day(now);
    const cJSON *step;
    cJSON_ArrayForEach(step, series) {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(step, "time");
        const cJSON *s = cJSON_GetObjectItemCaseSensitive(step, "intervalParametersStartTime");
        const cJSON *data = cJSON_GetObjectItemCaseSensitive(step, "data");
        time_t at = utc_time(cJSON_IsString(t) ? t->valuestring : NULL);
        time_t from = utc_time(cJSON_IsString(s) ? s->valuestring : NULL);
        double temp = num(data, "air_temperature", NAN);
        int sym = (int)num(data, "symbol_code", 0);
        if (!at) {
            continue;
        }
        if (!from || from >= at) {
            from = at - 3600;
        }
        if (!isnan(temp) && llabs((long long)(at - now)) <= nearest) { // a tie: the coming hour
            nearest = llabs((long long)(at - now));
            out->valid = true;
            out->temp = (float)temp;
            out->symbol = (uint8_t)(sym >= 1 && sym <= 27 ? sym : 0);
        }
        // Today's high/low from here on (the hour just gone counts too).
        if (!isnan(temp) && at >= now - 3600 && local_day(at) == today) {
            if (!out->day_known || temp > out->high) {
                out->high = (float)temp;
            }
            if (!out->day_known || temp < out->low) {
                out->low = (float)temp;
            }
            out->day_known = true;
        }
        // The first wet hour that isn't over yet.
        if (!out->rain_start && at > now && from < now + AHEAD_S && sym >= 1 && sym <= 27 &&
            symbols[sym].kind != WEATHER_DRY) {
            out->rain_start = from;
            out->rain_in_min = from <= now ? 0 : (int)((from - now) / 60);
            out->rain_soon = out->rain_in_min <= WEATHER_SOON_MIN;
            out->rain_kind = symbols[sym].kind;
            out->rain_level = symbols[sym].level;
            out->rain_mm_h = (float)(num(data, "precipitation_amount_mean", 0) * 3600.0 / (double)(at - from));
            out->rain_chance = (uint8_t)LV_CLAMP(0, (int)num(data, "probability_of_precipitation", 0), 100);
        }
    }
    cJSON_Delete(o);
    return true;
}

// ── Shared between the threads (under mu) ────────────────────────────────────

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static struct {
    struct weather_state st;
    bool dirty;
} sh;

// ── Worker-only ──────────────────────────────────────────────────────────────

static double lat, lon;
static CURL *curl;

struct buf {
    char *data;
    size_t len;
};

static size_t on_data(char *p, size_t size, size_t n, void *arg) {
    struct buf *b = arg;
    size_t add = size * n;
    if (b->len + add > 4 * 1024 * 1024) {
        return 0; // a forecast is ~10 KB: something is wrong
    }
    char *d = realloc(b->data, b->len + add + 1);
    if (!d) {
        return 0;
    }
    memcpy(d + b->len, p, add);
    b->data = d;
    b->len += add;
    d[b->len] = '\0';
    return add;
}

static long fetch(struct buf *out) {
    char url[512];
    snprintf(url, sizeof(url), API "/lon/%.4f/lat/%.4f/data.json?timeseries=%d&parameters=" PARAMS, lon, lat,
             STEPS);
    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "deck/1 (desk screen)");
    long status = 0;
    if (curl_easy_perform(curl) == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }
    return status;
}

static void *worker(void *arg) {
    (void)arg;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    char *good = NULL; // the last good forecast (JSON)
    time_t fetched = 0, next_fetch = 0;
    int fails = 0;
    char message[96] = "";
    float seen_hi = 0, seen_lo = 0; // the "now" temperatures seen today
    int seen_day = -1;

    for (;;) {
        if (mono_s() >= next_fetch) {
            struct buf b = {0};
            long status = fetch(&b);
            struct weather_state probe = {0};
            if (status == 200 && weather_digest(b.data, time(NULL), &probe)) {
                free(good);
                good = b.data;
                b.data = NULL;
                fetched = time(NULL);
                if (fails) {
                    deck_log("weather: forecast again after %d failed tries", fails);
                } else if (!next_fetch) {
                    deck_log("weather: first forecast in");
                }
                fails = 0;
                message[0] = '\0';
                next_fetch = mono_s() + POLL_S;
            } else {
                copy(message, sizeof(message),
                     status == 0     ? "NO CONNECTION TO SMHI"
                     : status == 404 ? "SMHI HAS NO FORECAST FOR THAT SPOT"
                     : status == 200 ? "SMHI SENT SOMETHING ODD"
                                     : "SMHI ISN'T ANSWERING");
                if (!fails) { // once per streak
                    deck_log("weather: %s (HTTP %ld), trying again", message, status);
                }
                fails++;
                next_fetch = mono_s() + LV_MIN(RETRY_S << LV_MIN(fails - 1, 6), POLL_S);
            }
            free(b.data);
        }

        struct weather_state s;
        memset(&s, 0, sizeof(s)); // compared with memcmp below: no stray padding
        s.configured = true;
        time_t now = time(NULL);
        if (good && clock_ok() && now - fetched < STALE_S) {
            weather_digest(good, now, &s);
        }
        if (s.valid) {
            if (local_day(now) != seen_day) {
                seen_day = local_day(now);
                seen_hi = seen_lo = s.temp;
            }
            seen_hi = LV_MAX(seen_hi, s.temp);
            seen_lo = LV_MIN(seen_lo, s.temp);
            s.high = s.day_known ? LV_MAX(s.high, seen_hi) : seen_hi;
            s.low = s.day_known ? LV_MIN(s.low, seen_lo) : seen_lo;
            s.day_known = true;
        } else {
            s.temp = 0;
            s.symbol = 0;
            s.created = 0;
            s.high = s.low = 0;
            s.day_known = s.rain_soon = false;
            s.rain_start = 0;
            s.rain_in_min = -1;
        }
        s.updated = fetched;
        copy(s.message, sizeof(s.message), message);

        pthread_mutex_lock(&mu);
        s.version = sh.st.version;
        if (memcmp(&s, &sh.st, sizeof(s)) != 0) {
            s.version++;
            memcpy(&sh.st, &s, sizeof(s));
            sh.dirty = true;
        }
        pthread_mutex_unlock(&mu);

        time_t wait = next_fetch - mono_s();
        sleep((unsigned)LV_CLAMP(1, wait, TICK_S));
    }
    return NULL;
}

// ── LVGL side ────────────────────────────────────────────────────────────────

static struct weather_state pub;

static void collect(lv_timer_t *t) {
    (void)t;
    pthread_mutex_lock(&mu);
    if (sh.dirty) {
        sh.dirty = false;
        uint32_t v = pub.version + 1;
        pub = sh.st;
        pub.version = v;
    }
    pthread_mutex_unlock(&mu);
}

void weather_start(const char *lat_lon) {
    if (!lat_lon || !lat_lon[0]) {
        return; // not configured: the screen leaves the weather out
    }
    pub.configured = true;
    pub.rain_in_min = -1;
    if (sscanf(lat_lon, "%lf , %lf", &lat, &lon) != 2 || fabs(lat) > 90 || fabs(lon) > 180) {
        copy(pub.message, sizeof(pub.message), "WEATHER= NEEDS LAT,LON");
        deck_log("weather: weather= isn't lat,lon (like 59.3293,18.0686)");
        return;
    }
    sh.st.configured = true;
    sh.st.rain_in_min = -1;
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) != 0) {
        return;
    }
    pthread_detach(t);
    deck_log("weather: SMHI forecast for the spot in deck.conf"); // not the spot itself: logs get shared
    lv_timer_create(collect, 250, NULL);
}

const struct weather_state *weather_state(void) { return &pub; }
