#pragma once

// The work calendar, from Outlook's published ICS link ("Publish a
// calendar" in Outlook on the web), for the next-meeting countdown and the
// JOIN key. The link lets anyone read the calendar, so it never sits in
// deck.conf: deck-drive.sh moves calendar_url= from there into a root-only
// file, which this reads. A worker thread fetches it; the state is read here
// on the LVGL thread (like spotify.h). calendar.c is the real thing (Pi); the
// preview uses preview/calendar_fake.c.

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define CAL_TODAY_MAX 10

enum cal_status {
    CAL_NONE,    // no calendar link on the Pi: the screen leaves the calendar out
    CAL_LOADING, // a link, but no calendar read yet (or the clock isn't set yet)
    CAL_OK,
};

// Outlook's free/busy for the event (X-MICROSOFT-CDO-BUSYSTATUS, TRANSP).
enum cal_busy { CAL_BUSY, CAL_TENTATIVE, CAL_FREE, CAL_AWAY, CAL_ELSEWHERE };

struct cal_event {
    time_t start, end; // all-day: local midnights
    bool all_day;
    enum cal_busy busy;
    char title[96];    // "Busy" etc. when published as availability only
    char location[96]; // "" when published as availability only
    char join_url[512]; // Teams (else Zoom/Meet/Webex) link, "" if none
};

struct cal_state {
    enum cal_status status;
    int count;                                // today's events not over yet:
    struct cal_event today[CAL_TODAY_MAX];    // all-day first, then by start
    bool has_now;                             // a meeting going on now (timed, not
    struct cal_event now;                     // free; the latest started)
    bool has_next;                            // the next one to start (timed, not free,
    struct cal_event next;                    // within 14 days)
    time_t updated;   // when the calendar was last fetched, 0 = never
    char message[96]; // the last problem worth showing, "" if none
    uint32_t version; // changes whenever anything above does
};

// url_path: the file holding the ICS link (deck-drive.sh puts it there).
void calendar_start(const char *url_path);
const struct cal_state *calendar_state(void);

// The core without the network, for app/test/calendar_test.c: what the screen
// would show at `now` from an ICS text. Fills everything but status, updated,
// message and version. False if it isn't a calendar.
bool calendar_digest(const char *ics, size_t len, time_t now, struct cal_state *out);
