/*
 * Tests the real src/calendar.c on a PC: what the screen would show from
 * hand-written Outlook-style calendars (app/test/ics/) at chosen moments.
 * pi/tools/calendar-test.sh builds and runs it, with TZ = Stockholm's rule.
 *
 *   calendar_test <ics dir>                       the checks (exit 0 = all passed)
 *   calendar_test show <file.ics> [YYYY-MM-DD HH:MM]  what a calendar file gives (now by default)
 *   calendar_test live <file with the link>       fetch it the way the Pi does, and show it
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "calendar.h"
#include "lvgl.h"

void deck_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("log: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static uint32_t tick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static time_t L(const char *s) { // local time
    struct tm tm = {0};
    sscanf(s, "%d-%d-%d %d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

static time_t Z(const char *s) { // UTC
    struct tm tm = {0};
    sscanf(s, "%d-%d-%d %d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return timegm(&tm);
}

static const char *hm(time_t t) {
    static char b[4][32];
    static int i;
    i = (i + 1) % 4;
    strftime(b[i], sizeof(b[i]), "%a %d %b %H:%M", localtime(&t));
    return b[i];
}

static const char *busy_word[] = {"busy", "tentative", "free", "away", "elsewhere"};

static void print_event(const char *label, const struct cal_event *e) {
    if (e->all_day) {
        printf("  %-5s all day %s - %s  %s", label, hm(e->start), hm(e->end), e->title);
    } else {
        printf("  %-5s %s - %s  %s", label, hm(e->start), hm(e->end), e->title);
    }
    printf("%s%s%s  [%s]%s\n", e->location[0] ? "  (" : "", e->location, e->location[0] ? ")" : "",
           busy_word[e->busy], e->join_url[0] ? "  JOIN" : "");
}

static void print_state(const struct cal_state *s, time_t now) {
    printf("at %s:\n", hm(now));
    for (int i = 0; i < s->count; i++) {
        print_event(i ? "" : "today", &s->today[i]);
    }
    if (s->has_now) {
        print_event("now", &s->now);
    }
    if (s->has_next) {
        print_event("next", &s->next);
        printf("        in %ld min%s%s\n", (long)(s->next.start - now) / 60, s->next.join_url[0] ? ", join: " : "",
               s->next.join_url);
    }
}

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *d = malloc((size_t)n + 1);
    *len = fread(d, 1, (size_t)n, f);
    d[*len] = '\0';
    fclose(f);
    return d;
}

static int passed, failed;

static void check(const char *what, int ok) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    ok ? passed++ : failed++;
}

static const struct cal_event *find(const struct cal_state *s, const char *title) {
    for (int i = 0; i < s->count; i++) {
        if (!strcmp(s->today[i].title, title)) {
            return &s->today[i];
        }
    }
    return NULL;
}

static bool starts_at(const struct cal_event *e, time_t t) { return e && e->start == t; }

static bool utf8_ok(const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p;) {
        int n = *p < 0x80 ? 1 : *p >= 0xF0 ? 4 : *p >= 0xE0 ? 3 : *p >= 0xC0 ? 2 : 0;
        if (!n) {
            return false;
        }
        for (int i = 1; i < n; i++) {
            if ((p[i] & 0xC0) != 0x80) {
                return false;
            }
        }
        p += n;
    }
    return true;
}

static struct cal_state st; // ~9 KB

static const struct cal_state *at(const char *ics, size_t len, const char *when) {
    time_t now = L(when);
    if (!calendar_digest(ics, len, now, &st)) {
        printf("  (not a calendar)\n");
    }
    print_state(&st, now);
    return &st;
}

static void outlook(const char *dir) {
    char path[512];
    size_t len;
    snprintf(path, sizeof(path), "%s/outlook.ics", dir);
    char *ics = slurp(path, &len);
    printf("==> outlook.ics: Exchange's shape (CRLF, folded, Windows zones), all details\n");

    const struct cal_state *s = at(ics, len, "2026-11-05 09:00");
    check("ten events left today (the screen's most), all-day first", s->count == CAL_TODAY_MAX &&
                                                                          s->today[0].all_day && s->today[1].all_day &&
                                                                          !s->today[2].all_day);
    static const char *order[] = {"Conference", "Lucia", NULL, "Lunch talk", "Maybe sync", "Design review",
                                  "Focus time", "Vendor call", "Paris sync", "SF call"};
    bool in_order = true;
    for (int i = 0; i < 10; i++) {
        in_order &= !order[i] || !strcmp(s->today[i].title, order[i]);
    }
    check("in order of start", in_order);
    const struct cal_event *e = find(s, "Conference");
    check("a 3-day all-day event from Exchange (midnight in a zone + ALLDAYEVENT), started yesterday",
          e && e->all_day && e->start == L("2026-11-04 00:00") && e->end == L("2026-11-07 00:00") &&
              e->busy == CAL_AWAY && !strcmp(e->location, "Göteborg"));
    e = find(s, "Lucia");
    check("a DATE all-day event: local midnight to midnight, free",
          e && e->start == L("2026-11-05 00:00") && e->end == L("2026-11-06 00:00") && e->busy == CAL_FREE);
    const char *title = s->today[2].title;
    check("a long title folded inside a UTF-8 character: joined, cut at a whole character",
          !strcmp(title, "Möte med Åsa om budgeten för nästa år och det övriga vi pratade om förra veckan och om") &&
              utf8_ok(title) && starts_at(&s->today[2], L("2026-11-05 10:00")));
    check("a UTC (Z) time", starts_at(find(s, "Vendor call"), Z("2026-11-05 15:00")));
    check("a Windows zone without a VTIMEZONE (Romance Standard Time, built in)",
          starts_at(find(s, "Paris sync"), Z("2026-11-05 16:00")));
    check("the file's VTIMEZONE for Pacific (already winter time on 5 Nov)",
          starts_at(find(s, "SF call"), Z("2026-11-05 17:00")));
    check("STATUS:CANCELLED and \"Canceled:\" left out", !find(s, "Old meeting") && !find(s, "Canceled: Retro"));
    check("nothing now; next is 10:00", !s->has_now && s->has_next && s->next.start == L("2026-11-05 10:00"));

    s = at(ics, len, "2026-11-05 11:40");
    check("now: Lunch talk, its Teams link from X-MICROSOFT-SKYPETEAMSMEETINGURL (not the Zoom one)",
          s->has_now && !strcmp(s->now.title, "Lunch talk") &&
              !strncmp(s->now.join_url, "https://teams.microsoft.com/l/meetup-join/19%3ameeting_TFVOQ0g", 60));
    check("next: a tentative one counts", s->has_next && !strcmp(s->next.title, "Maybe sync") &&
                                               s->next.busy == CAL_TENTATIVE);

    s = at(ics, len, "2026-11-05 13:10");
    static const char *join =
        "https://teams.microsoft.com/l/meetup-join/19%3ameeting_ZjQ4NGE3MDQtYjM2Mi00ZjE0LWI2NDMtNTk3YWM0ZDQ5ZTgx"
        "%40thread.v2/0?context=%7b%22Tid%22%3a%2211111111-2222-3333-4444-555555555555%22%2c%22Oid%22%3a"
        "%2266666666-7777-8888-9999-000000000000%22%7d";
    check("now: Design review, the join link out of the folded DESCRIPTION (not Meeting options)",
          s->has_now && !strcmp(s->now.title, "Design review") && !strcmp(s->now.join_url, join) &&
              !strcmp(s->now.location, "Microsoft Teams Meeting"));
    check("next skips a free block (Focus time)", s->has_next && !strcmp(s->next.title, "Vendor call"));

    s = at(ics, len, "2026-11-05 14:30");
    check("a free block isn't \"now\", but is on today's list", !s->has_now && find(s, "Focus time"));

    s = at(ics, len, "2026-11-06 05:00");
    check("next day: the conference is still on", s->count && !strcmp(s->today[0].title, "Conference"));
    check("a custom VTIMEZONE (+05:30, Exchange's equal DAYLIGHT)",
          s->has_next && !strcmp(s->next.title, "Bangalore standup") && s->next.start == Z("2026-11-06 05:00"));
    e = find(s, "Dentist");
    check("a VALARM's SUMMARY/DESCRIPTION don't leak into the event", e && !e->join_url[0]);
    check("a floating time is local", starts_at(find(s, "Gym"), L("2026-11-06 18:00")));
    free(ics);
}

static void recurring(const char *dir) {
    char path[512];
    size_t len;
    snprintf(path, sizeof(path), "%s/recurring.ics", dir);
    char *ics = slurp(path, &len);
    printf("==> recurring.ics: series (LF line ends, no VTIMEZONE: the built-in zones)\n");

    const struct cal_state *s = at(ics, len, "2026-10-19 08:00");
    check("weekly, before the DST change: 10:00 = 08:00 UTC", s->has_next && !strcmp(s->next.title, "Weekly sync") &&
                                                                  s->next.start == Z("2026-10-19 08:00") &&
                                                                  s->next.join_url[0]);
    s = at(ics, len, "2026-10-25 00:00");
    check("02:30 on the night it happens twice: the first one (00:30 UTC)",
          s->has_next && !strcmp(s->next.title, "Night job") && s->next.start == Z("2026-10-25 00:30"));
    check("WKST=SU: no Rota on Sunday 25 Oct", !find(at(ics, len, "2026-10-25 07:00"), "Rota"));
    s = at(ics, len, "2026-10-26 08:00");
    check("the week after the change: still 10:00, now 09:00 UTC",
          s->has_next && !strcmp(s->next.title, "Weekly sync") && s->next.start == Z("2026-10-26 09:00"));
    check("EXDATE: 2 Nov deleted", !find(at(ics, len, "2026-11-02 08:00"), "Weekly sync"));
    check("RECURRENCE-ID + STATUS:CANCELLED: 9 Nov gone", !find(at(ics, len, "2026-11-09 08:00"), "Weekly sync"));
    s = at(ics, len, "2026-11-16 08:00");
    const struct cal_event *e = find(s, "Weekly sync (moved)");
    check("RECURRENCE-ID: 16 Nov moved to 14:00, the 10:00 one gone, the link kept from the series",
          e && e->start == L("2026-11-16 14:00") && e->join_url[0] && !find(s, "Weekly sync"));
    check("UNTIL counts its own instance (30 Nov)",
          starts_at(find(at(ics, len, "2026-11-30 08:00"), "Weekly sync"), L("2026-11-30 10:00")));
    check("and nothing after it", !find(at(ics, len, "2026-12-07 08:00"), "Weekly sync"));

    s = at(ics, len, "2026-11-05 08:00");
    check("DAILY;INTERVAL=2;COUNT=3: the third on 5 Nov", starts_at(find(s, "Onboarding"), L("2026-11-05 09:00")));
    check("MONTHLY;BYDAY=1TH: 5 Nov", starts_at(find(s, "Monthly all-hands"), L("2026-11-05 15:00")));
    check("COUNT ends it: none on 7 Nov", !find(at(ics, len, "2026-11-07 08:00"), "Onboarding"));
    s = at(ics, len, "2026-11-26 08:00");
    check("BYDAY=TH;BYSETPOS=-1: the last Thursday",
          starts_at(find(s, "Last-Thursday beer"), L("2026-11-26 16:00")) && !find(s, "Monthly all-hands"));
    s = at(ics, len, "2026-11-15 08:00");
    e = find(s, "Payday");
    check("MONTHLY;BYMONTHDAY=15, all day", e && e->all_day && e->start == L("2026-11-15 00:00"));
    check("Rota: Sunday 15 Nov (an on week)", find(s, "Rota") != NULL);
    s = at(ics, len, "2026-11-01 07:00");
    check("Rota on Sunday 1 Nov, and Onboarding's first day",
          starts_at(find(s, "Rota"), L("2026-11-01 08:00")) && starts_at(find(s, "Onboarding"), L("2026-11-01 09:00")));
    check("Rota: not Tuesday 10 Nov (an off week)", !find(at(ics, len, "2026-11-10 07:00"), "Rota"));
    check("Rota: Tuesday 17 Nov", find(at(ics, len, "2026-11-17 07:00"), "Rota") != NULL);
    e = find(at(ics, len, "2027-06-06 08:00"), "Mum's birthday");
    check("YEARLY from 1960, all day, TRANSP = free", e && e->all_day && e->busy == CAL_FREE);
    s = at(ics, len, "2026-11-27 08:00");
    check("next looks past the weekend: Rota on Sunday 29 Nov",
          s->has_next && !strcmp(s->next.title, "Rota") && s->next.start == L("2026-11-29 08:00"));
    free(ics);
}

static void others(const char *dir) {
    char path[512];
    size_t len;
    snprintf(path, sizeof(path), "%s/availability.ics", dir);
    char *ics = slurp(path, &len);
    printf("==> availability.ics: published as \"Can view when I'm busy\"\n");
    const struct cal_state *s = at(ics, len, "2026-11-05 12:00");
    check("titles are the free/busy words, no place, no link",
          s->has_next && !strcmp(s->next.title, "Busy") && !s->next.location[0] && !s->next.join_url[0] &&
              s->count == 2 && s->today[1].busy == CAL_TENTATIVE);
    free(ics);
    printf("==> not calendars\n");
    static const char html[] = "<!DOCTYPE html><html><body>Outlook</body></html>";
    check("an HTML page", !calendar_digest(html, sizeof(html) - 1, L("2026-11-05 12:00"), &st));
    check("nothing", !calendar_digest("", 0, L("2026-11-05 12:00"), &st));
}

static int live(const char *url_file) {
    lv_init();
    lv_tick_set_cb(tick);
    calendar_start(url_file);
    const struct cal_state *s = calendar_state();
    for (int i = 0; i < 600 && s->status != CAL_NONE; i++) { // 60 s
        lv_timer_handler();
        usleep(100000);
        if (s->status == CAL_OK || s->message[0]) {
            break;
        }
    }
    if (s->message[0]) {
        printf("message: %s\n", s->message);
    }
    print_state(s, time(NULL));
    return s->status == CAL_OK ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "show")) {
        size_t len;
        char *ics = slurp(argv[2], &len);
        time_t now = argc > 3 ? L(argv[3]) : time(NULL);
        if (!calendar_digest(ics, len, now, &st)) {
            fprintf(stderr, "%s isn't a calendar\n", argv[2]);
            return 1;
        }
        print_state(&st, now);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "live")) {
        return live(argv[2]);
    }
    if (argc != 2) {
        fprintf(stderr, "usage: %s <ics dir> | show <file.ics> [YYYY-MM-DD HH:MM] | live <link file>\n", argv[0]);
        return 2;
    }
    outlook(argv[1]);
    recurring(argv[1]);
    others(argv[1]);
    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
