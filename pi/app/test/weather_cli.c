/*
 * Runs the real src/weather.c on a PC, without a screen. pi/tools/weather-test.sh
 * builds and runs it.
 *
 *   weather_cli test <rain.json>   checks what weather_digest() makes of a made-up
 *                                  forecast at several moments (exit 0 = all passed)
 *   weather_cli live <lat,lon>     the real thing: SMHI's forecast for that spot,
 *                                  through the worker thread, as the screen gets it
 *
 * Run with TZ=Europe/Stockholm (or its POSIX rule): "today" is local.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lvgl.h"
#include "weather.h"

static uint32_t tick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void deck_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("log: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static const char *kinds[] = {"dry", "rain", "sleet", "snow", "thunder"};

static void print_state(const struct weather_state *s, time_t now) {
    char t[32];
    strftime(t, sizeof(t), "%Y-%m-%d %H:%M %Z", localtime(&now));
    printf("at %s: ", t);
    if (!s->valid) {
        printf("no forecast%s%s\n", s->message[0] ? ": " : "", s->message);
        return;
    }
    printf("%.1f°C, %s (symbol %d)", s->temp, weather_symbol_name(s->symbol), s->symbol);
    if (s->day_known) {
        printf(", today %.1f / %.1f", s->high, s->low);
    }
    char when[24] = "now";
    if (s->rain_in_min > 0) {
        snprintf(when, sizeof(when), "in %d min", s->rain_in_min);
    }
    if (s->rain_in_min < 0) {
        printf(", dry for the next 12 h\n");
    } else {
        printf(", %s %s (level %d, %.1f mm/h, %d%%)%s\n", kinds[s->rain_kind], when, s->rain_level, s->rain_mm_h,
               s->rain_chance, s->rain_soon ? ", soon" : "");
    }
}

static int failed, passed;

static void check(const char *what, int ok) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    ok ? passed++ : failed++;
}

static time_t utc(const char *iso) {
    struct tm tm = {0};
    sscanf(iso, "%d-%d-%dT%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return timegm(&tm);
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *d = malloc((size_t)n + 1);
    d[fread(d, 1, (size_t)n, f)] = '\0';
    fclose(f);
    return d;
}

static int run_tests(const char *path) {
    char *json = slurp(path);
    struct weather_state s;

    memset(&s, 0, sizeof(s));
    time_t now = utc("2026-11-05T14:34");
    check("parses SMHI's shape", weather_digest(json, now, &s));
    print_state(&s, now);
    check("now = the step nearest now (15:00 UTC)", s.valid && s.temp > 6.05f && s.temp < 6.15f && s.symbol == 3);
    check("today's high/low: what's left of the local day (6.8 / 2.2, not the 1.5 after midnight)",
          s.day_known && s.high > 6.75f && s.high < 6.85f && s.low > 2.15f && s.low < 2.25f);
    check("light rain in 26 min, soon", s.rain_in_min == 26 && s.rain_soon && s.rain_kind == WEATHER_RAIN &&
                                            s.rain_level == 1 && s.rain_chance == 70);
    check("mm/h of that hour", s.rain_mm_h > 0.29f && s.rain_mm_h < 0.31f);
    check("created time", s.created == utc("2026-11-05T11:28") + 20);

    now = utc("2026-11-05T15:10");
    weather_digest(json, now, &s);
    print_state(&s, now);
    check("inside the wet hour: rain now (0 min)", s.rain_in_min == 0 && s.rain_soon && s.rain_start <= now);

    now = utc("2026-11-05T12:00");
    weather_digest(json, now, &s);
    print_state(&s, now);
    check("three hours ahead: rain later, not soon", s.rain_in_min == 180 && !s.rain_soon);

    now = utc("2026-11-05T17:30");
    weather_digest(json, now, &s);
    print_state(&s, now);
    check("after the rain: snow showers in 30 min", s.rain_in_min == 30 && s.rain_kind == WEATHER_SNOW &&
                                                         s.rain_soon && s.rain_level == 1);
    check("half way between two hours: the coming one is now", s.symbol == 6 && s.temp > 5.05f && s.temp < 5.15f);

    now = utc("2026-11-05T19:30");
    weather_digest(json, now, &s);
    print_state(&s, now);
    check("dry for the rest: none", s.rain_in_min < 0 && !s.rain_soon && s.rain_start == 0);

    now = utc("2026-11-07T00:00");
    weather_digest(json, now, &s);
    print_state(&s, now);
    check("a forecast that has run out isn't shown", !s.valid);

    check("an error page isn't a forecast", !weather_digest("<html>503</html>", now, &s));
    check("other JSON isn't either", !weather_digest("{\"approvedTime\":1}", now, &s));
    free(json);
    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}

static int run_live(const char *where) {
    lv_init();
    lv_tick_set_cb(tick);
    weather_start(where);
    const struct weather_state *s = weather_state();
    uint32_t seen = s->version;
    for (int i = 0; i < 300; i++) { // 30 s
        lv_timer_handler();
        usleep(100000);
        if (s->version != seen) {
            seen = s->version;
            if (s->valid || s->message[0]) {
                break;
            }
        }
    }
    print_state(s, time(NULL));
    if (s->valid) {
        char c[32], u[32];
        strftime(c, sizeof(c), "%H:%M", localtime(&s->created));
        strftime(u, sizeof(u), "%H:%M:%S", localtime(&s->updated));
        printf("    SMHI made it at %s, fetched at %s\n", c, u);
    }
    return s->valid ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "test")) {
        return run_tests(argv[2]);
    }
    if (argc == 3 && !strcmp(argv[1], "live")) {
        return run_live(argv[2]);
    }
    fprintf(stderr, "usage: %s test <rain.json> | live <lat,lon>\n", argv[0]);
    return 2;
}
