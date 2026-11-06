/*
 * The work calendar, from Outlook's published ICS link (Outlook on the web →
 * Settings → Calendar → Shared calendars → Publish a calendar → the ICS
 * link). What's in it depends on the level picked there:
 *   "Can view when I'm busy"         times only; SUMMARY is the free/busy word
 *   "Can view titles and locations"  plus the real SUMMARY and LOCATION
 *   "Can view all details"           plus DESCRIPTION, the invitation's text, where
 *                                    Teams puts "Join the meeting now<https://teams...>"
 * so the JOIN key needs "all details". X-MICROSOFT-SKYPETEAMSMEETINGURL (in
 * invitations) is used first when it's there, X-ALT-DESC (the HTML body some
 * feeds carry) last.
 *
 * Exchange writes times as DTSTART;TZID=<Windows zone name>, with a
 * VTIMEZONE for most (not always all) of the names it uses, so known names
 * are also built in. A recurring meeting is one VEVENT with an RRULE, which
 * runs on that zone's wall clock (10:00 stays 10:00 across a DST change);
 * deleted instances are EXDATEs, and each changed (or cancelled) instance is
 * a VEVENT with the same UID and a RECURRENCE-ID. Instances are matched by
 * their day in the series' zone, which is all a rule here can tell apart.
 *
 * Supported: line folding, VTIMEZONE (yearly nth-weekday rules), UTC, floating
 * and all-day times, DURATION, RRULE DAILY/WEEKLY/MONTHLY/YEARLY with
 * INTERVAL, COUNT, UNTIL, BYDAY (with nth for months), BYMONTHDAY, BYMONTH,
 * one BYSETPOS (per month) and WKST; EXDATE; RECURRENCE-ID; STATUS:CANCELLED
 * and "Canceled:" titles; free/busy. Not: RDATE, BYHOUR and the like
 * (Outlook doesn't write them), RANGE=THISANDFUTURE.
 *
 * The link lets anyone read the calendar, so it's never logged, and it lives
 * in a root-only file (deck-drive.sh moves it there from deck.conf).
 *
 * One worker thread fetches every 10 minutes (backing off on errors, keeping
 * the last good calendar through wifi drops) and works out what "now" means
 * whenever that changes: a meeting starting or ending, midnight, or a new
 * calendar.
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE // strcasestr
#include "calendar.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <lvgl.h>

#include "deck.h"

#define POLL_S (10 * 60)
#define RETRY_S 30  // after a failure, doubling up to POLL_S
#define TICK_S 60
#define AHEAD_DAYS 14  // how far "next" looks
#define BEHIND_DAYS 32 // an instance that began this long ago can still be going on (a long all-day one)
#define MAX_BYTES (16 * 1024 * 1024)
#define MAX_PERIODS 200000 // a rule stops after this many days/weeks/months/years

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

// ── Dates (days since 1970-01-01; Howard Hinnant's algorithms) ───────────────

static int64_t floor_div(int64_t a, int64_t b) { return a / b - (a % b != 0 && (a < 0) != (b < 0)); }

static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = floor_div(y, 400);
    int yoe = (int)(y - era * 400);
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int *y, int *m, int *d) {
    z += 719468;
    int64_t era = floor_div(z, 146097);
    int doe = (int)(z - era * 146097);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)(yoe + era * 400) + (*m <= 2);
}

static int weekday(int64_t days) { return (int)((days % 7 + 11) % 7); } // 0 = Sunday; 1970-01-01 was a Thursday

static int month_days(int y, int m) {
    static const int n[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return n[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}

// ── Time zones ───────────────────────────────────────────────────────────────
// A zone is a standard and a summer offset and the two yearly changes
// (POSIX TZ's model), which is what Exchange's VTIMEZONEs describe. The
// zone pointer NULL means floating: the Pi's own zone (TZ), through libc.

struct rule {
    int8_t month, wday, week; // week 1-4, -1 = the last
    int32_t at;               // seconds into that day, on the clock before the change
};

struct zone {
    char name[64];
    int32_t std, dst; // UTC offsets, seconds east
    bool has_dst;
    struct rule on, off; // standard → summer, summer → standard
};

#define EU(n, h)                                                                                   \
    {n, (h) * 3600, ((h) + 1) * 3600, true, {3, 0, -1, 3600 + (h) * 3600}, {10, 0, -1, 3600 + ((h) + 1) * 3600}}
#define US(n, h) {n, (h) * 3600, ((h) + 1) * 3600, true, {3, 0, 2, 7200}, {11, 0, 1, 7200}}
#define FIXED(n, s) {n, s, s, false, {0, 0, 0, 0}, {0, 0, 0, 0}}

static const struct zone utc = FIXED("UTC", 0);

// Names Outlook uses without always describing them (Windows names, and the
// IANA ones other calendars use). Today's rules.
static const struct zone known[] = {
    FIXED("UTC", 0), FIXED("Etc/UTC", 0), FIXED("GMT", 0), FIXED("Greenwich Standard Time", 0),
    FIXED("Coordinated Universal Time", 0), FIXED("UTC Standard Time", 0),
    EU("GMT Standard Time", 0), EU("Europe/London", 0), EU("Europe/Dublin", 0), EU("Europe/Lisbon", 0),
    EU("W. Europe Standard Time", 1), EU("Romance Standard Time", 1), EU("Central Europe Standard Time", 1),
    EU("Central European Standard Time", 1), EU("Europe/Stockholm", 1), EU("Europe/Oslo", 1),
    EU("Europe/Copenhagen", 1), EU("Europe/Berlin", 1), EU("Europe/Amsterdam", 1), EU("Europe/Brussels", 1),
    EU("Europe/Paris", 1), EU("Europe/Madrid", 1), EU("Europe/Rome", 1), EU("Europe/Vienna", 1),
    EU("Europe/Zurich", 1), EU("Europe/Warsaw", 1), EU("Europe/Prague", 1), EU("Europe/Budapest", 1),
    EU("FLE Standard Time", 2), EU("GTB Standard Time", 2), EU("E. Europe Standard Time", 2),
    EU("Europe/Helsinki", 2), EU("Europe/Tallinn", 2), EU("Europe/Riga", 2), EU("Europe/Vilnius", 2),
    EU("Europe/Athens", 2), EU("Europe/Bucharest", 2), EU("Europe/Kyiv", 2), EU("Europe/Kiev", 2),
    EU("Europe/Sofia", 2),
    US("Eastern Standard Time", -5), US("US Eastern Standard Time", -5), US("America/New_York", -5),
    US("Central Standard Time", -6), US("America/Chicago", -6), US("Mountain Standard Time", -7),
    US("America/Denver", -7), US("Pacific Standard Time", -8), US("America/Los_Angeles", -8),
    FIXED("US Mountain Standard Time", -7 * 3600), FIXED("America/Phoenix", -7 * 3600),
    FIXED("Russian Standard Time", 3 * 3600), FIXED("Europe/Moscow", 3 * 3600),
    FIXED("Turkey Standard Time", 3 * 3600), FIXED("Europe/Istanbul", 3 * 3600),
    FIXED("India Standard Time", 19800), FIXED("Asia/Kolkata", 19800), FIXED("China Standard Time", 8 * 3600),
    FIXED("Asia/Shanghai", 8 * 3600), FIXED("Singapore Standard Time", 8 * 3600),
    FIXED("Asia/Singapore", 8 * 3600), FIXED("Tokyo Standard Time", 9 * 3600), FIXED("Asia/Tokyo", 9 * 3600),
};

static int64_t rule_day(int y, const struct rule *r) {
    if (r->week >= 1 && r->week <= 4) {
        int64_t first = days_from_civil(y, r->month, 1);
        return first + (r->wday - weekday(first) + 7) % 7 + 7 * (r->week - 1);
    }
    int64_t last = days_from_civil(y, r->month, month_days(y, r->month));
    return last - (weekday(last) - r->wday + 7) % 7;
}

static int32_t offset_at(const struct zone *z, int64_t u) {
    if (!z->has_dst) {
        return z->std;
    }
    int y, m, d;
    civil_from_days(floor_div(u + z->std, 86400), &y, &m, &d);
    int64_t on = rule_day(y, &z->on) * 86400 + z->on.at - z->std;
    int64_t off = rule_day(y, &z->off) * 86400 + z->off.at - z->dst;
    bool summer = on < off ? u >= on && u < off : u >= on || u < off; // the south has summer at new year
    return summer ? z->dst : z->std;
}

// "Wall" = seconds since 1970 on that zone's clock, as if it were UTC.
static int64_t wall_to_utc(const struct zone *z, int64_t wall) {
    if (!z) {
        int64_t days = floor_div(wall, 86400);
        int secs = (int)(wall - days * 86400), y, m, d;
        civil_from_days(days, &y, &m, &d);
        struct tm tm = {.tm_year = y - 1900, .tm_mon = m - 1, .tm_mday = d, .tm_hour = secs / 3600,
                        .tm_min = secs / 60 % 60, .tm_sec = secs % 60, .tm_isdst = -1};
        return mktime(&tm);
    }
    if (!z->has_dst) {
        return wall - z->std;
    }
    int64_t early = wall - z->dst; // the hour that comes twice in autumn: the first one
    if (offset_at(z, early) == z->dst) {
        return early;
    }
    return wall - z->std; // standard time, or a time the spring change skips (read on the clock before it)
}

static int64_t utc_to_wall(const struct zone *z, int64_t u) {
    if (!z) {
        time_t t = (time_t)u;
        struct tm tm;
        localtime_r(&t, &tm);
        return days_from_civil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * 86400 + tm.tm_hour * 3600 +
               tm.tm_min * 60 + tm.tm_sec;
    }
    return u + offset_at(z, u);
}

static int64_t local_day(int64_t u) { return floor_div(utc_to_wall(NULL, u), 86400); }
static int64_t local_midnight(int64_t day) { return wall_to_utc(NULL, day * 86400); }

// ── Lines ────────────────────────────────────────────────────────────────────

struct line {
    const char *name; // "NAME;PARAM=...", up to where the value starts
    const char *value;
};

// Unfolds the text (a line starting with a space or tab continues the one
// before) into a new buffer and splits each line at its first ':' outside
// quotes. Returns the lines (free both).
static struct line *split(const char *text, size_t len, char **bufp, int *count) {
    size_t max = 1;
    for (size_t i = 0; i < len; i++) {
        max += text[i] == '\n';
    }
    char *buf = malloc(len + 2);
    struct line *lines = malloc(max * sizeof(*lines));
    *bufp = buf;
    *count = 0;
    if (!buf || !lines) {
        free(lines);
        return NULL;
    }
    if (len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3)) { // a BOM
        text += 3;
        len -= 3;
    }
    size_t o = 0;
    int n = 0;
    for (size_t i = 0; i < len;) {
        size_t j = i;
        while (j < len && text[j] != '\n') {
            j++;
        }
        size_t e = j > i && text[j - 1] == '\r' ? j - 1 : j;
        if (n > 0 && e > i && (text[i] == ' ' || text[i] == '\t')) {
            memcpy(buf + o, text + i + 1, e - i - 1);
            o += e - i - 1;
        } else {
            if (n > 0) {
                buf[o++] = '\0';
            }
            lines[n++].name = buf + o;
            memcpy(buf + o, text + i, e - i);
            o += e - i;
        }
        i = j + 1;
    }
    buf[o] = '\0';
    int kept = 0;
    for (int i = 0; i < n; i++) {
        char *p = (char *)lines[i].name;
        bool quoted = false;
        for (; *p && (*p != ':' || quoted); p++) {
            quoted ^= *p == '"';
        }
        if (*p) {
            *p = '\0';
            lines[kept].name = lines[i].name;
            lines[kept++].value = p + 1;
        }
    }
    *count = kept;
    return lines;
}

static bool is(const struct line *l, const char *name) {
    size_t n = strlen(name);
    return !strncasecmp(l->name, name, n) && (l->name[n] == ';' || l->name[n] == '\0');
}

static bool is_value(const struct line *l, const char *name, const char *value) {
    return is(l, name) && !strcasecmp(l->value, value);
}

// A parameter's value (TZID, VALUE), "" if the line has none.
static void param(const struct line *l, const char *key, char *out, size_t size) {
    out[0] = '\0';
    size_t k = strlen(key);
    for (const char *p = strchr(l->name, ';'); p; p = strchr(p + 1, ';')) {
        if (!strncasecmp(p + 1, key, k) && p[1 + k] == '=') {
            const char *v = p + 2 + k;
            size_t n = *v == '"' ? strcspn(++v, "\"") : strcspn(v, ";");
            snprintf(out, size, "%.*s", (int)n, v);
            return;
        }
    }
}

// A TEXT value without its escapes (\n \, \; \\), cut at a whole UTF-8
// character; newlines become spaces when one_line.
static void text(char *dst, size_t size, const char *src, bool one_line) {
    size_t n = 0;
    for (const char *s = src ? src : ""; *s && n + 1 < size; s++) {
        char c = *s;
        if (c == '\\' && s[1]) {
            c = *++s;
            if (c == 'n' || c == 'N') {
                c = '\n';
            }
        }
        dst[n++] = one_line && (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    dst[n] = '\0';
    if (n + 1 == size) { // full: drop a character cut in half
        size_t lead = n;
        while (lead > 0 && (dst[lead - 1] & 0xC0) == 0x80) {
            lead--;
        }
        if (lead > 0) {
            unsigned char b = (unsigned char)dst[lead - 1];
            size_t want = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
            if (lead - 1 + want > n) {
                dst[lead - 1] = '\0';
            }
        }
    }
    while (one_line && n > 0 && dst[strlen(dst) - 1] == ' ') { // trailing spaces
        dst[strlen(dst) - 1] = '\0';
        n--;
    }
}

// ── Parsing ──────────────────────────────────────────────────────────────────

struct when {
    int64_t wall; // on the zone's clock; a DATE: its midnight
    bool date;
    const struct zone *z; // NULL = floating (the Pi's zone)
};

enum freq { R_NONE, R_DAILY, R_WEEKLY, R_MONTHLY, R_YEARLY };

struct rrule {
    enum freq freq;
    int interval, count, setpos, wkst;
    bool has_until;
    struct when until;
    uint8_t mask; // every BYDAY weekday (bit 0 = Sunday)
    struct {
        int8_t n, wd; // n: 2 = the second, -1 = the last, 0 = every
    } byday[14];
    int nbyday;
    int8_t monthday[31];
    int nmonthday;
    uint16_t months; // BYMONTH (bit m)
};

struct ev {
    uint64_t uid; // FNV-1a of the UID
    struct when start;
    int64_t dur; // seconds; all-day: days * 86400
    bool has_rid;
    struct when rid;
    struct rrule rr;
    struct when *ex; // EXDATEs
    int nex;
    bool cancelled;
    enum cal_busy busy;
    char title[96], location[96], join[512];
};

struct ctx {
    struct zone zones[32]; // the file's VTIMEZONEs
    int nzones;
    struct ev *ev;
    int nev, cap;
    int64_t from_u, to_u; // what matters: instances overlapping this
};

static const struct zone *find_zone(const struct ctx *c, const char *tzid) {
    char name[64];
    copy(name, sizeof(name), tzid);
    if (!strncasecmp(name, "tzone://Microsoft/", 18)) {
        memmove(name, name + 18, strlen(name + 18) + 1);
    }
    char *s = name + strspn(name, " /");
    for (size_t n = strlen(s); n > 0 && s[n - 1] == ' '; n--) {
        s[n - 1] = '\0';
    }
    for (int i = 0; i < c->nzones; i++) {
        if (!strcasecmp(c->zones[i].name, s)) {
            return &c->zones[i];
        }
    }
    for (unsigned i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        if (!strcasecmp(known[i].name, s)) {
            return &known[i];
        }
    }
    static char logged[64]; // once per name, not at every look
    if (strcmp(logged, s) != 0) {
        copy(logged, sizeof(logged), s);
        deck_log("calendar: unknown time zone \"%s\", reading it as local time", s);
    }
    return NULL;
}

static bool parse_when(const struct ctx *c, const struct line *l, struct when *w) {
    int y, mo, d, h = 0, mi = 0, s = 0;
    char z = 0, tzid[64], value[16];
    int n = sscanf(l->value, "%4d%2d%2dT%2d%2d%2d%c", &y, &mo, &d, &h, &mi, &s, &z);
    if (n < 3 || mo < 1 || mo > 12 || d < 1 || d > 31) {
        return false;
    }
    param(l, "VALUE", value, sizeof(value));
    param(l, "TZID", tzid, sizeof(tzid));
    w->date = n < 5 || !strcasecmp(value, "DATE");
    w->wall = days_from_civil(y, mo, d) * 86400 + (w->date ? 0 : h * 3600 + mi * 60 + s);
    w->z = z == 'Z' ? &utc : tzid[0] ? find_zone(c, tzid) : NULL;
    return true;
}

static int64_t instant(const struct when *w) { return wall_to_utc(w->date ? NULL : w->z, w->wall); }

static int64_t when_day(const struct when *w) { return floor_div(w->wall, 86400); }

// +0100, -0500, +053000 → seconds
static int32_t utc_offset(const char *v) {
    int sign = *v == '-' ? -1 : 1;
    v += *v == '-' || *v == '+';
    int hh = 0, mm = 0, ss = 0;
    sscanf(v, "%2d%2d%2d", &hh, &mm, &ss);
    return sign * (hh * 3600 + mm * 60 + ss);
}

static int weekday_code(const char *s) {
    static const char *codes[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
    for (int i = 0; i < 7; i++) {
        if (!strncasecmp(s, codes[i], 2)) {
            return i;
        }
    }
    return -1;
}

static void parse_rrule(const struct ctx *c, const char *value, struct rrule *r) {
    memset(r, 0, sizeof(*r));
    r->interval = 1;
    r->wkst = 1; // Monday
    char buf[512];
    copy(buf, sizeof(buf), value);
    char *save, *tok;
    for (tok = strtok_r(buf, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
        char *v = strchr(tok, '=');
        if (!v) {
            continue;
        }
        *v++ = '\0';
        if (!strcasecmp(tok, "FREQ")) {
            r->freq = !strcasecmp(v, "DAILY")     ? R_DAILY
                      : !strcasecmp(v, "WEEKLY")  ? R_WEEKLY
                      : !strcasecmp(v, "MONTHLY") ? R_MONTHLY
                      : !strcasecmp(v, "YEARLY")  ? R_YEARLY
                                                  : R_NONE; // HOURLY etc.: Outlook doesn't; only DTSTART then
        } else if (!strcasecmp(tok, "INTERVAL")) {
            r->interval = LV_MAX(1, atoi(v));
        } else if (!strcasecmp(tok, "COUNT")) {
            r->count = LV_MAX(0, atoi(v));
        } else if (!strcasecmp(tok, "UNTIL")) {
            struct line l = {"UNTIL", v};
            r->has_until = parse_when(c, &l, &r->until);
        } else if (!strcasecmp(tok, "WKST")) {
            r->wkst = LV_MAX(0, weekday_code(v));
        } else if (!strcasecmp(tok, "BYSETPOS")) {
            r->setpos = atoi(v);
        } else {
            char *s2, *item;
            for (item = strtok_r(v, ",", &s2); item; item = strtok_r(NULL, ",", &s2)) {
                int n = atoi(item);
                if (!strcasecmp(tok, "BYDAY") && r->nbyday < 14) {
                    int wd = weekday_code(item + strspn(item, "+-0123456789"));
                    if (wd >= 0) {
                        r->byday[r->nbyday].n = (int8_t)LV_CLAMP(-5, n, 5);
                        r->byday[r->nbyday++].wd = (int8_t)wd;
                        r->mask |= (uint8_t)(1 << wd);
                    }
                } else if (!strcasecmp(tok, "BYMONTHDAY") && r->nmonthday < 31 && n && n >= -31 && n <= 31) {
                    r->monthday[r->nmonthday++] = (int8_t)n;
                } else if (!strcasecmp(tok, "BYMONTH") && n >= 1 && n <= 12) {
                    r->months |= (uint16_t)(1 << n);
                }
            }
        }
    }
}

// One VTIMEZONE: its STANDARD and DAYLIGHT parts (the newest of each) → a zone.
static int parse_vtimezone(struct ctx *c, const struct line *l, int n, int i) {
    struct zone z = {0};
    struct part {
        bool seen;
        int64_t start;
        int32_t from, to;
        struct rule r;
        bool has_rule;
    } std = {0}, dst = {0}, cur = {0};
    bool in = false, is_dst = false;
    for (; i < n && !is_value(&l[i], "END", "VTIMEZONE"); i++) {
        if (is(&l[i], "TZID") && !in) {
            copy(z.name, sizeof(z.name), l[i].value);
        } else if (is_value(&l[i], "BEGIN", "STANDARD") || is_value(&l[i], "BEGIN", "DAYLIGHT")) {
            in = true;
            is_dst = !strcasecmp(l[i].value, "DAYLIGHT");
            memset(&cur, 0, sizeof(cur));
            cur.seen = true;
        } else if (in && (is_value(&l[i], "END", "STANDARD") || is_value(&l[i], "END", "DAYLIGHT"))) {
            in = false;
            struct part *p = is_dst ? &dst : &std;
            if (!p->seen || cur.start >= p->start) {
                *p = cur;
            }
        } else if (in && is(&l[i], "DTSTART")) {
            struct when w;
            if (parse_when(c, &l[i], &w)) {
                cur.start = w.wall;
                cur.r.at = (int32_t)(w.wall - when_day(&w) * 86400);
            }
        } else if (in && is(&l[i], "TZOFFSETFROM")) {
            cur.from = utc_offset(l[i].value);
        } else if (in && is(&l[i], "TZOFFSETTO")) {
            cur.to = utc_offset(l[i].value);
        } else if (in && is(&l[i], "RRULE")) {
            struct rrule r;
            parse_rrule(c, l[i].value, &r);
            for (int m = 1; m <= 12; m++) {
                if (r.months & (1 << m)) {
                    cur.r.month = (int8_t)m;
                }
            }
            if (r.freq == R_YEARLY && cur.r.month && r.nbyday) {
                cur.r.wday = r.byday[0].wd;
                cur.r.week = (int8_t)(r.byday[0].n >= 5 || r.byday[0].n < 0 ? -1 : LV_MAX(1, r.byday[0].n));
                cur.has_rule = true;
            }
        }
    }
    if (z.name[0] && (std.seen || dst.seen) && c->nzones < (int)(sizeof(c->zones) / sizeof(c->zones[0]))) {
        z.std = std.seen ? std.to : dst.to;
        z.dst = dst.seen ? dst.to : z.std;
        z.has_dst = std.seen && dst.seen && std.has_rule && dst.has_rule && z.std != z.dst;
        z.on = dst.r;
        z.off = std.r;
        c->zones[c->nzones++] = z;
    }
    return i;
}

static uint64_t fnv(const char *s) {
    uint64_t h = 1469598103934665603ull;
    for (; *s; s++) {
        h = (h ^ (unsigned char)*s) * 1099511628211ull;
    }
    return h;
}

// The meeting link: Teams first, wherever it is, then the others.
static void find_join(char *out, size_t size, const char *const *texts, int ntexts) {
    static const char *kinds[] = {"teams.microsoft.com/l/meetup-join/", "teams.microsoft.com/meet/",
                                  "teams.live.com/meet/", "teams.live.com/l/meetup-join/", "zoom.us/j/",
                                  "zoom.us/my/", "meet.google.com/", ".webex.com/"};
    out[0] = '\0';
    for (unsigned k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
        for (int t = 0; t < ntexts; t++) {
            const char *hit = texts[t] ? strcasestr(texts[t], kinds[k]) : NULL;
            if (!hit) {
                continue;
            }
            const char *start = hit;
            while (start > texts[t] && hit - start < 64 && strncasecmp(start, "https://", 8) != 0) {
                start--;
            }
            if (strncasecmp(start, "https://", 8) != 0) {
                continue;
            }
            size_t n = strcspn(start, " \t\r\n<>\"'");
            while (n > 0 && strchr(".,;)]", start[n - 1])) {
                n--;
            }
            if (n < size) {
                snprintf(out, size, "%.*s", (int)n, start);
                return;
            }
        }
    }
}

static int parse_vevent(struct ctx *c, const struct line *l, int n, int i) {
    struct ev e;
    memset(&e, 0, sizeof(e));
    const struct line *end = NULL, *summary = NULL, *location = NULL, *desc = NULL, *html = NULL, *skype = NULL,
                      *external = NULL, *busy = NULL;
    bool has_start = false, all_day = false, transparent = false, tentative = false;
    int64_t duration = -1;
    int depth = 0;
    for (; i < n; i++) {
        if (is(&l[i], "BEGIN")) {
            depth++; // a VALARM: its DESCRIPTION isn't the event's
            continue;
        }
        if (is(&l[i], "END")) {
            if (depth-- == 0) {
                break;
            }
            continue;
        }
        if (depth) {
            continue;
        }
        if (is(&l[i], "UID")) {
            e.uid = fnv(l[i].value);
        } else if (is(&l[i], "DTSTART")) {
            has_start = parse_when(c, &l[i], &e.start);
        } else if (is(&l[i], "DTEND")) {
            end = &l[i];
        } else if (is(&l[i], "DURATION")) {
            int64_t v = 0, num = 0;
            for (const char *p = l[i].value; *p; p++) {
                if (*p >= '0' && *p <= '9') {
                    num = num * 10 + (*p - '0');
                    continue;
                }
                v += *p == 'W' ? num * 604800 : *p == 'D' ? num * 86400 : *p == 'H' ? num * 3600
                     : *p == 'M' ? num * 60 : *p == 'S' ? num : 0;
                num = 0;
            }
            duration = l[i].value[0] == '-' ? 0 : v;
        } else if (is(&l[i], "RECURRENCE-ID")) {
            e.has_rid = parse_when(c, &l[i], &e.rid);
        } else if (is(&l[i], "RRULE")) {
            parse_rrule(c, l[i].value, &e.rr);
        } else if (is(&l[i], "EXDATE")) {
            char *vals = strdup(l[i].value), *save, *v;
            for (v = strtok_r(vals, ",", &save); v; v = strtok_r(NULL, ",", &save)) {
                struct line one = {l[i].name, v};
                struct when w;
                struct when *grown = parse_when(c, &one, &w) ? realloc(e.ex, (size_t)(e.nex + 1) * sizeof(w)) : NULL;
                if (grown) {
                    e.ex = grown;
                    e.ex[e.nex++] = w;
                }
            }
            free(vals);
        } else if (is(&l[i], "SUMMARY")) {
            summary = &l[i];
        } else if (is(&l[i], "LOCATION")) {
            location = &l[i];
        } else if (is(&l[i], "DESCRIPTION")) {
            desc = &l[i];
        } else if (is(&l[i], "X-ALT-DESC")) {
            html = &l[i]; // the invitation as HTML, in some feeds
        } else if (is(&l[i], "X-MICROSOFT-SKYPETEAMSMEETINGURL")) {
            skype = &l[i];
        } else if (is(&l[i], "X-MICROSOFT-ONLINEMEETINGEXTERNALLINK")) {
            external = &l[i];
        } else if (is(&l[i], "X-MICROSOFT-CDO-BUSYSTATUS")) {
            busy = &l[i];
        } else if (is_value(&l[i], "X-MICROSOFT-CDO-ALLDAYEVENT", "TRUE")) {
            all_day = true;
        } else if (is_value(&l[i], "TRANSP", "TRANSPARENT")) {
            transparent = true;
        } else if (is_value(&l[i], "STATUS", "CANCELLED")) {
            e.cancelled = true;
        } else if (is_value(&l[i], "STATUS", "TENTATIVE")) {
            tentative = true;
        }
    }
    if (!has_start) {
        free(e.ex);
        return i;
    }
    // Exchange's all-day events can come as midnight-to-midnight in a zone.
    if (all_day && !e.start.date && e.start.wall % 86400 == 0) {
        e.start.date = true;
    }
    struct when w;
    if (end && parse_when(c, end, &w)) {
        e.dur = e.start.date ? (when_day(&w) - when_day(&e.start)) * 86400 : instant(&w) - instant(&e.start);
    } else {
        e.dur = duration >= 0 ? duration : e.start.date ? 86400 : 0;
    }
    if (e.start.date) {
        e.dur = LV_MAX(86400, e.dur / 86400 * 86400);
    }
    e.dur = LV_MAX(0, e.dur);

    text(e.title, sizeof(e.title), summary ? summary->value : "", true);
    text(e.location, sizeof(e.location), location ? location->value : "", true);
    if (!strncasecmp(e.title, "Canceled:", 9) || !strncasecmp(e.title, "Cancelled:", 10)) {
        e.cancelled = true; // cancelled by the organiser, not yet removed
    }
    const char *v = busy ? busy->value : "";
    e.busy = !strcasecmp(v, "FREE")               ? CAL_FREE
             : !strcasecmp(v, "TENTATIVE")        ? CAL_TENTATIVE
             : !strcasecmp(v, "OOF")              ? CAL_AWAY
             : !strcasecmp(v, "WORKINGELSEWHERE") ? CAL_ELSEWHERE
             : busy                               ? CAL_BUSY
             : transparent                        ? CAL_FREE
             : tentative                          ? CAL_TENTATIVE
                                                  : CAL_BUSY;
    char *body = NULL;
    if (desc) {
        size_t len = strlen(desc->value) + 1;
        body = malloc(len);
        if (body) {
            text(body, len, desc->value, false);
        }
    }
    const char *texts[] = {skype ? skype->value : NULL, external ? external->value : NULL, body, e.location,
                           html ? html->value : NULL};
    find_join(e.join, sizeof(e.join), texts, 5);
    free(body);

    // Keep only what can matter: series, changed instances, and events near now.
    int64_t s = instant(&e.start);
    bool keep = e.rr.freq != R_NONE || e.has_rid || (s < c->to_u + 2 * 86400 && s + e.dur > c->from_u - 2 * 86400);
    struct ev *grown = keep && c->nev == c->cap ? realloc(c->ev, (size_t)(c->cap * 2 + 16) * sizeof(*grown)) : NULL;
    if (grown) {
        c->ev = grown;
        c->cap = c->cap * 2 + 16;
    }
    if (keep && c->nev < c->cap) {
        c->ev[c->nev++] = e;
    } else {
        free(e.ex);
    }
    return i;
}

// ── Recurrence ───────────────────────────────────────────────────────────────

struct occ {
    int64_t start, end;
    int ev;
    bool all_day;
};

struct occs {
    struct occ *o;
    int n, cap;
};

static bool byday_match(const struct rrule *r, int day, int dim, int wd) {
    for (int i = 0; i < r->nbyday; i++) {
        int n = r->byday[i].n;
        if (r->byday[i].wd == wd &&
            (n == 0 || (n > 0 && (day - 1) / 7 + 1 == n) || (n < 0 && (dim - day) / 7 + 1 == -n))) {
            return true;
        }
    }
    return false;
}

// The days a monthly or yearly rule picks in one month, in order.
static int month_picks(const struct rrule *r, int y, int m, int start_mday, int64_t *out) {
    int dim = month_days(y, m), n = 0;
    int64_t first = days_from_civil(y, m, 1);
    for (int day = 1; day <= dim; day++) {
        bool ok;
        if (r->nmonthday) {
            ok = false;
            for (int i = 0; i < r->nmonthday; i++) {
                ok |= r->monthday[i] > 0 ? r->monthday[i] == day : dim + r->monthday[i] + 1 == day;
            }
            ok &= !r->nbyday || byday_match(r, day, dim, weekday(first + day - 1));
        } else if (r->nbyday) {
            ok = byday_match(r, day, dim, weekday(first + day - 1));
        } else {
            ok = day == start_mday; // a month without that day is skipped (RFC 5545)
        }
        if (ok) {
            out[n++] = first + day - 1;
        }
    }
    if (r->setpos && n) {
        int i = r->setpos > 0 ? r->setpos - 1 : n + r->setpos;
        out[0] = i >= 0 && i < n ? out[i] : 0;
        n = i >= 0 && i < n;
    }
    return n;
}

// Period k of the rule (day, week, month or year number k * INTERVAL after
// the first): the days it picks, in order, and where the period begins.
static int period(const struct rrule *r, int64_t first, int64_t k, int64_t *out, int64_t *begins) {
    int n = 0, y, m, d;
    civil_from_days(first, &y, &m, &d);
    switch (r->freq) {
    case R_DAILY:
        *begins = first + k * r->interval;
        if (!r->mask || (r->mask >> weekday(*begins) & 1)) {
            out[n++] = *begins;
        }
        break;
    case R_WEEKLY: {
        uint8_t mask = r->mask ? r->mask : (uint8_t)(1 << weekday(first));
        *begins = first - (weekday(first) - r->wkst + 7) % 7 + 7 * r->interval * k;
        for (int i = 0; i < 7; i++) {
            if (mask >> weekday(*begins + i) & 1) {
                out[n++] = *begins + i;
            }
        }
        break;
    }
    case R_MONTHLY: {
        int64_t mm = (int64_t)y * 12 + (m - 1) + k * r->interval;
        int yy = (int)floor_div(mm, 12), mo = (int)(mm - floor_div(mm, 12) * 12) + 1;
        *begins = days_from_civil(yy, mo, 1);
        n = month_picks(r, yy, mo, d, out);
        break;
    }
    case R_YEARLY: {
        int yy = y + (int)(k * r->interval);
        uint16_t months = r->months ? r->months : (uint16_t)(1 << m);
        *begins = days_from_civil(yy, 1, 1);
        for (int mo = 1; mo <= 12; mo++) {
            if (months >> mo & 1) {
                n += month_picks(r, yy, mo, d, out + n);
            }
        }
        break;
    }
    default:
        *begins = INT64_MAX;
    }
    return n;
}

static void add(struct occs *os, int ev, int64_t start, int64_t end) {
    if (os->n == os->cap) {
        struct occ *g = realloc(os->o, (size_t)(os->cap * 2 + 32) * sizeof(*g));
        if (!g) {
            return;
        }
        os->o = g;
        os->cap = os->cap * 2 + 32;
    }
    os->o[os->n++] = (struct occ){start, end, ev, false};
}

// The instance of event i on day (in its own zone), if it overlaps the window.
static void instance(const struct ctx *c, struct occs *os, int i, int64_t day) {
    const struct ev *e = &c->ev[i];
    int64_t s, t;
    if (e->start.date) {
        s = local_midnight(day);
        t = local_midnight(day + e->dur / 86400);
    } else {
        s = wall_to_utc(e->start.z, day * 86400 + (e->start.wall - when_day(&e->start) * 86400));
        t = s + e->dur;
    }
    if (s < c->to_u && (t > c->from_u || (t == s && s >= c->from_u))) {
        add(os, i, s, t);
        os->o[os->n - 1].all_day = e->start.date;
    }
}

// The day of an EXDATE or RECURRENCE-ID in series m's own zone.
static int64_t day_in(const struct ev *m, const struct when *w) {
    if (w->date) {
        return when_day(w);
    }
    return floor_div(utc_to_wall(m->start.date ? NULL : m->start.z, instant(w)), 86400);
}

static bool skipped(int64_t d, const int64_t *skip, int nskip) {
    for (int i = 0; i < nskip; i++) {
        if (skip[i] == d) {
            return true;
        }
    }
    return false;
}

// Every instance of event i in the window, minus the days in skip.
static void expand(const struct ctx *c, struct occs *os, int i, const int64_t *skip, int nskip) {
    const struct ev *e = &c->ev[i];
    const struct rrule *r = &e->rr;
    int64_t first = when_day(&e->start), tod = e->start.wall - first * 86400;
    if (!skipped(first, skip, nskip)) {
        instance(c, os, i, first); // DTSTART is always the first instance (RFC 5545)
    }
    if (r->freq == R_NONE) {
        return;
    }
    int64_t to_day = local_day(c->to_u) + 1, until_u = INT64_MAX, until_day = INT64_MAX;
    if (r->has_until) {
        if (e->start.date) {
            until_day = r->until.date ? when_day(&r->until) : local_day(instant(&r->until));
        } else {
            until_u = r->until.date ? wall_to_utc(e->start.z, (when_day(&r->until) + 1) * 86400) - 1
                                    : instant(&r->until);
            until_day = floor_div(utc_to_wall(e->start.z, until_u), 86400) + 1;
        }
        if (until_day < local_day(c->from_u) - BEHIND_DAYS) {
            return; // a series that ended long ago
        }
    }
    int count = 1;
    for (int64_t k = 0; k < MAX_PERIODS; k++) {
        int64_t days[12 * 31], begins;
        int nd = period(r, first, k, days, &begins);
        if (begins > to_day || begins > until_day) {
            return;
        }
        for (int j = 0; j < nd; j++) {
            int64_t d = days[j];
            if (d <= first) {
                continue;
            }
            if ((r->count && ++count > r->count) || d > until_day || d > to_day ||
                (!e->start.date && wall_to_utc(e->start.z, d * 86400 + tod) > until_u)) {
                return;
            }
            if (!skipped(d, skip, nskip)) {
                instance(c, os, i, d);
            }
        }
    }
}

// ── What the screen shows ────────────────────────────────────────────────────

static void event_out(const struct ctx *c, const struct occ *o, struct cal_event *out) {
    const struct ev *e = &c->ev[o->ev];
    memset(out, 0, sizeof(*out));
    out->start = (time_t)o->start;
    out->end = (time_t)o->end;
    out->all_day = o->all_day;
    out->busy = e->busy;
    copy(out->title, sizeof(out->title), e->title);
    copy(out->location, sizeof(out->location), e->location);
    copy(out->join_url, sizeof(out->join_url), e->join);
}

static int by_time(const void *pa, const void *pb) { // all-day first, then by start, then by end
    const struct occ *a = pa, *b = pb;
    if (a->all_day != b->all_day) {
        return a->all_day ? -1 : 1;
    }
    if (a->start != b->start) {
        return a->start < b->start ? -1 : 1;
    }
    return a->end < b->end ? -1 : a->end > b->end;
}

// Parse, expand, pick. *change: when the picks change next (a start, an end,
// midnight), so the worker knows when to look again.
static bool digest(const char *ics, size_t len, time_t now, struct cal_state *out, time_t *change) {
    char *buf;
    int n;
    struct line *l = split(ics, len, &buf, &n);
    bool cal = false;
    for (int i = 0; i < n && !cal; i++) {
        cal = is_value(&l[i], "BEGIN", "VCALENDAR");
    }
    struct ctx *c = cal ? calloc(1, sizeof(*c)) : NULL;
    if (!c) {
        free(buf);
        free(l);
        return false;
    }
    memset(out, 0, sizeof(*out));
    int64_t today = local_day(now), tomorrow = local_midnight(today + 1);
    c->from_u = now;
    c->to_u = now + AHEAD_DAYS * 86400;
    for (int i = 0; i < n; i++) { // zones first: they can come after the events that use them
        if (is_value(&l[i], "BEGIN", "VTIMEZONE")) {
            i = parse_vtimezone(c, l, n, i + 1);
        }
    }
    for (int i = 0; i < n; i++) {
        if (is_value(&l[i], "BEGIN", "VEVENT")) {
            i = parse_vevent(c, l, n, i + 1);
        }
    }

    // Changed instances: what they leave out comes from their series.
    int *changed = malloc((size_t)(c->nev + 1) * sizeof(int)), nchanged = 0;
    for (int i = 0; changed && i < c->nev; i++) {
        struct ev *e = &c->ev[i];
        if (!e->has_rid) {
            continue;
        }
        changed[nchanged++] = i;
        for (int j = 0; j < c->nev; j++) {
            const struct ev *m = &c->ev[j];
            if (!m->has_rid && m->uid == e->uid) {
                if (!e->title[0]) {
                    copy(e->title, sizeof(e->title), m->title);
                }
                if (!e->location[0]) {
                    copy(e->location, sizeof(e->location), m->location);
                }
                if (!e->join[0]) {
                    copy(e->join, sizeof(e->join), m->join);
                }
                break;
            }
        }
    }
    struct occs os = {0};
    for (int i = 0; changed && i < c->nev; i++) {
        const struct ev *e = &c->ev[i];
        if (e->cancelled) {
            continue; // a cancelled changed instance still hides its series' one (below)
        }
        int64_t *skip = malloc((size_t)(e->nex + nchanged + 1) * sizeof(int64_t));
        int nskip = 0;
        for (int k = 0; skip && !e->has_rid && k < e->nex; k++) {
            skip[nskip++] = day_in(e, &e->ex[k]);
        }
        for (int k = 0; skip && !e->has_rid && k < nchanged; k++) {
            if (c->ev[changed[k]].uid == e->uid) {
                skip[nskip++] = day_in(e, &c->ev[changed[k]].rid);
            }
        }
        if (skip) {
            expand(c, &os, i, skip, nskip);
        }
        free(skip);
    }
    qsort(os.o, (size_t)os.n, sizeof(*os.o), by_time);

    *change = (time_t)tomorrow;
    int now_i = -1, next_i = -1;
    for (int i = 0; i < os.n; i++) {
        const struct occ *o = &os.o[i];
        const struct ev *e = &c->ev[o->ev];
        if (o->start > now) {
            *change = LV_MIN(*change, (time_t)o->start);
        } else if (o->end > now) {
            *change = LV_MIN(*change, (time_t)o->end);
        }
        if (o->start < tomorrow && (o->end > now || (o->end == o->start && o->start >= now)) &&
            out->count < CAL_TODAY_MAX) {
            event_out(c, o, &out->today[out->count++]);
        }
        if (o->all_day || e->busy == CAL_FREE) {
            continue;
        }
        if (o->start <= now && o->end > now) {
            now_i = i; // sorted by start: the last one is the latest started
        } else if (o->start > now && next_i < 0) {
            next_i = i;
        }
    }
    if (now_i >= 0) {
        out->has_now = true;
        event_out(c, &os.o[now_i], &out->now);
    }
    if (next_i >= 0) {
        out->has_next = true;
        event_out(c, &os.o[next_i], &out->next);
    }

    for (int i = 0; i < c->nev; i++) {
        free(c->ev[i].ex);
    }
    free(changed);
    free(os.o);
    free(c->ev);
    free(c);
    free(buf);
    free(l);
    return true;
}

bool calendar_digest(const char *ics, size_t len, time_t now, struct cal_state *out) {
    time_t change;
    return digest(ics, len, now, out, &change);
}

// ── Shared between the threads (under mu) ────────────────────────────────────

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static struct {
    struct cal_state st;
    bool dirty;
} sh;

// ── Worker-only ──────────────────────────────────────────────────────────────

static char url[2048]; // never logged: anyone with it can read the calendar
static CURL *curl;

struct buf {
    char *data;
    size_t len;
};

static size_t on_data(char *p, size_t size, size_t n, void *arg) {
    struct buf *b = arg;
    size_t add = size * n;
    if (b->len + add > MAX_BYTES) {
        return 0;
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
    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
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
    char *good = NULL; // the last good calendar
    size_t good_len = 0;
    time_t fetched = 0, next_fetch = 0;
    int fails = 0;
    char message[96] = "";
    static struct cal_state s; // ~9 KB: not on the stack

    for (;;) {
        if (mono_s() >= next_fetch) {
            struct buf b = {0};
            long status = fetch(&b);
            if (status == 200 && b.data && strstr(b.data, "BEGIN:VCALENDAR")) {
                free(good);
                good = b.data;
                good_len = b.len;
                b.data = NULL;
                fetched = time(NULL);
                if (fails || !next_fetch) {
                    deck_log("calendar: read (%zu KB)%s", good_len / 1024, fails ? ", after failed tries" : "");
                }
                fails = 0;
                message[0] = '\0';
                next_fetch = mono_s() + POLL_S;
            } else {
                if (status == 0) {
                    copy(message, sizeof(message), "NO CONNECTION TO OUTLOOK");
                } else if (status == 401 || status == 403 || status == 404 || status == 410) {
                    copy(message, sizeof(message), "THE CALENDAR LINK DOESN'T WORK ANY MORE");
                } else if (status == 200) {
                    copy(message, sizeof(message), "THAT LINK ISN'T AN .ICS CALENDAR");
                } else {
                    snprintf(message, sizeof(message), "OUTLOOK SAID %ld", status);
                }
                if (!fails) { // once per streak
                    deck_log("calendar: %s (HTTP %ld), trying again", message, status);
                }
                fails++;
                next_fetch = mono_s() + LV_MIN(RETRY_S << LV_MIN(fails - 1, 6), POLL_S);
            }
            free(b.data);
        }

        time_t now = time(NULL), change = now + TICK_S;
        memset(&s, 0, sizeof(s));
        s.status = good && clock_ok() && digest(good, good_len, now, &s, &change) ? CAL_OK : CAL_LOADING;
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

        time_t wait = LV_MIN(next_fetch - mono_s(), change - time(NULL));
        sleep((unsigned)LV_CLAMP(1, wait, TICK_S));
    }
    return NULL;
}

// ── LVGL side ────────────────────────────────────────────────────────────────

static struct cal_state pub;

static void collect(lv_timer_t *t) {
    (void)t;
    pthread_mutex_lock(&mu);
    if (sh.dirty) {
        sh.dirty = false;
        uint32_t v = pub.version + 1;
        memcpy(&pub, &sh.st, sizeof(pub));
        pub.version = v;
    }
    pthread_mutex_unlock(&mu);
}

void calendar_start(const char *url_path) {
    FILE *f = fopen(url_path, "r");
    if (f) {
        if (!fgets(url, sizeof(url), f)) {
            url[0] = '\0';
        }
        fclose(f);
    }
    url[strcspn(url, "\r\n \t")] = '\0';
    if (!url[0]) {
        pub.status = CAL_NONE;
        deck_log("calendar: no calendar link yet (calendar_url= in deck.conf)");
        return;
    }
    if (!strncasecmp(url, "webcal://", 9)) { // what some "subscribe" buttons hand out
        memmove(url + 8, url + 9, strlen(url + 9) + 1);
        memcpy(url, "https://", 8);
    }
    size_t n = strlen(url);
    if (n > 5 && !strcasecmp(url + n - 5, ".html") && n + 1 < sizeof(url)) {
        strcpy(url + n - 5, ".ics"); // Outlook's other link (the web page) sits next to the ICS one
        deck_log("calendar: that's Outlook's HTML link, using the ICS one next to it");
    }
    pub.status = sh.st.status = CAL_LOADING;
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) != 0) {
        return;
    }
    pthread_detach(t);
    deck_log("calendar: reading the published calendar every %d minutes", POLL_S / 60);
    lv_timer_create(collect, 250, NULL);
}

const struct cal_state *calendar_state(void) { return &pub; }
