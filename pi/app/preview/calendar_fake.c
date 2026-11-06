/*
 * The preview's calendar (instead of src/calendar.c): an all-day event and
 * three more today, the next a Teams meeting in 12 minutes, plus a
 * preview-only switch for "in a meeting now". Times are from the real
 * clock, as the Pi's would be.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "calendar.h"

#define JOIN "https://teams.microsoft.com/l/meetup-join/19%3ameeting_UFJFVklFVw%40thread.v2/0"

static struct cal_state st;
static bool filled;

static struct cal_event ev(time_t start, int minutes, bool all_day, enum cal_busy busy, const char *title,
                           const char *location, const char *join) {
    struct cal_event e = {.start = start, .end = start + minutes * 60, .all_day = all_day, .busy = busy};
    snprintf(e.title, sizeof(e.title), "%s", title);
    snprintf(e.location, sizeof(e.location), "%s", location);
    snprintf(e.join_url, sizeof(e.join_url), "%s", join);
    return e;
}

static void fill(void) {
    if (filled) {
        return;
    }
    filled = true;
    time_t now = time(NULL) / 60 * 60;
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    tm.tm_isdst = -1;
    time_t midnight = mktime(&tm);
    st.status = CAL_OK;
    st.updated = now - 180;
    st.today[0] = ev(midnight, 24 * 60, true, CAL_FREE, "Lucia", "", "");
    st.today[1] = ev(now + 12 * 60, 45, false, CAL_BUSY, "Design review", "Microsoft Teams Meeting", JOIN);
    st.today[2] = ev(now + 90 * 60, 30, false, CAL_TENTATIVE, "1:1 med Åsa", "Rum 4", "");
    st.today[3] = ev(now + 150 * 60, 60, false, CAL_FREE, "Focus time", "", "");
    st.count = 4;
    st.has_next = true;
    st.next = st.today[1];
    st.version = 1;
}

void calendar_start(const char *url_path) { fill(); }

const struct cal_state *calendar_state(void) {
    fill();
    return &st;
}

bool calendar_digest(const char *ics, size_t len, time_t now, struct cal_state *out) { return false; }

// Preview only: a meeting going on now (that started 5 minutes ago), or not.
void calendar_fake_in_meeting(bool on) {
    fill();
    time_t now = time(NULL) / 60 * 60;
    st.has_now = on;
    st.now = ev(now - 5 * 60, 30, false, CAL_BUSY, "Standup", "Microsoft Teams Meeting", on ? JOIN : "");
    st.version++;
}
