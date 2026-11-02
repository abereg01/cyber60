/*
 * Shelly lights through the Shelly Cloud (Cloud Control API v2). The bulbs
 * stay on whatever wifi they like and in the Shelly app; the Pi only needs
 * the internet, never the network the bulbs are on.
 *
 *   POST https://<server>/v2/devices/api/set/light?auth_key=<key>
 *        {"id":"<device id>","channel":0,"on":true,"brightness":40}
 *   POST https://<server>/v2/devices/api/get?auth_key=<key>
 *        {"ids":[...],"select":["status"]}   → [{"id","online","status"}...]
 *
 * Shelly allows one request a second for the whole account, so one worker
 * thread sends everything, spaced. The LVGL side doesn't queue commands: it
 * leaves each lamp's latest wish in `sh.want` and the worker sends whatever
 * the wish is by the time its turn comes, so dragging the fader sends a
 * value a second and the last one on release, never a backlog. Every wish
 * bumps `want_seq`; a poll that started before a wish was sent can't undo its
 * optimistic change on screen (the same idea as spotify.c's done_seq).
 *
 * The cloud key controls every device on its Shelly account, so it never
 * sits in deck.conf: deck-drive.sh moves it from there into <data dir>/shelly-key
 * (root-only) and blanks the line.
 *
 * SPDX-License-Identifier: MIT
 */

#include "lights.h"

#include <cJSON.h>
#include <ctype.h>
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <lvgl.h>

#include "deck.h"

#define GAP_MS 1100 // Shelly: 1 request/s per account; a little margin
#define POLL_SHOWN_MS 5000
#define POLL_HIDDEN_MS 60000
#define SETTLE_MS 3000 // the cloud's status lags a set a little: no poll that soon

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void copy(char *dst, size_t size, const char *src) { snprintf(dst, size, "%s", src ? src : ""); }

// ── Shared between the threads (under mu) ────────────────────────────────────

struct want {
    bool on;
    bool with_brightness;
    uint8_t brightness;
};

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

static struct {
    struct lights_state st; // as the worker last saw it
    bool dirty;
    struct want want[LIGHTS_MAX];
    uint32_t want_seq[LIGHTS_MAX]; // bumped by every wish
    uint32_t sent_seq[LIGHTS_MAX]; // the wish the cloud has been sent
    bool visible, kick;
} sh;

// ── Worker-only ──────────────────────────────────────────────────────────────

static char base[160], key[256]; // base: https://<server>
static CURL *curl;

#define PUBLISH(code)                                                                              \
    do {                                                                                           \
        pthread_mutex_lock(&mu);                                                                   \
        struct lights_state *st = &sh.st;                                                          \
        code;                                                                                      \
        st->version++;                                                                             \
        sh.dirty = true;                                                                           \
        pthread_mutex_unlock(&mu);                                                                 \
    } while (0)

static void message(const char *m) {
    pthread_mutex_lock(&mu);
    if (strcmp(sh.st.message, m) != 0) {
        copy(sh.st.message, sizeof(sh.st.message), m);
        sh.st.version++;
        sh.dirty = true;
    }
    pthread_mutex_unlock(&mu);
}

struct buf {
    char *data;
    size_t len;
};

static size_t on_data(char *p, size_t size, size_t n, void *arg) {
    struct buf *b = arg;
    size_t add = size * n;
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

// POST path with a JSON body. Returns the HTTP status (0 = no connection).
static long post(const char *path, const char *body, struct buf *out) {
    char url[1024], k[800];
    char *esc = curl_easy_escape(curl, key, 0);
    snprintf(k, sizeof(k), "%s", esc ? esc : "");
    curl_free(esc);
    snprintf(url, sizeof(url), "%s/v2/devices/api/%s?auth_key=%s", base, path, k);
    struct curl_slist *h = curl_slist_append(NULL, "Content-Type: application/json");
    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 6L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "deck/1 (desk screen)");
    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    if (rc == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }
    curl_slist_free_all(h);
    return status;
}

// What a failed request means, for the screen.
static void complain(long status, const struct buf *b) {
    cJSON *o = b->data ? cJSON_Parse(b->data) : NULL;
    const cJSON *e = cJSON_GetObjectItemCaseSensitive(o, "error");
    const char *err = cJSON_IsString(e) ? e->valuestring : "";
    char m[96];
    if (status == 0) {
        copy(m, sizeof(m), "NO CONNECTION TO THE SHELLY CLOUD");
    } else if (status == 401 || status == 403) {
        copy(m, sizeof(m), "THE SHELLY CLOUD DIDN'T ACCEPT THE KEY");
    } else if (!strcmp(err, "DEVICE_OFFLINE")) {
        copy(m, sizeof(m), "THAT LAMP IS OFFLINE");
    } else if (status == 429) {
        copy(m, sizeof(m), "TOO MANY REQUESTS: TRYING AGAIN");
    } else {
        snprintf(m, sizeof(m), "SHELLY SAID %ld %s", status, err);
    }
    message(m);
    deck_log("lights: HTTP %ld %s", status, b->data ? b->data : "");
    cJSON_Delete(o);
}

// Output and brightness of channel 0, from either generation's status:
// Gen2+ has a component object ("light:0", "cct:0", "rgbw:0", ...) with
// output/brightness; Gen1 has lights[0] with ison/brightness.
static bool read_status(const cJSON *status, bool *on, uint8_t *brightness) {
    const cJSON *c;
    cJSON_ArrayForEach(c, status) {
        const char *k = c->string ? c->string : "";
        size_t n = strlen(k);
        if (n > 2 && !strcmp(k + n - 2, ":0") && cJSON_IsBool(cJSON_GetObjectItem(c, "output")) &&
            cJSON_IsNumber(cJSON_GetObjectItem(c, "brightness"))) {
            *on = cJSON_IsTrue(cJSON_GetObjectItem(c, "output"));
            *brightness = (uint8_t)LV_CLAMP(0, cJSON_GetObjectItem(c, "brightness")->valueint, 100);
            return true;
        }
    }
    const cJSON *l = cJSON_GetArrayItem(cJSON_GetObjectItem(status, "lights"), 0);
    if (l && cJSON_IsNumber(cJSON_GetObjectItem(l, "brightness"))) {
        *on = cJSON_IsTrue(cJSON_GetObjectItem(l, "ison"));
        *brightness = (uint8_t)LV_CLAMP(0, cJSON_GetObjectItem(l, "brightness")->valueint, 100);
        return true;
    }
    return false;
}

static void poll(void) {
    int count;
    char ids[LIGHTS_MAX][40];
    pthread_mutex_lock(&mu);
    count = sh.st.count;
    for (int i = 0; i < count; i++) {
        copy(ids[i], sizeof(ids[i]), sh.st.lamp[i].id);
    }
    pthread_mutex_unlock(&mu);

    cJSON *req = cJSON_CreateObject();
    cJSON *a = cJSON_AddArrayToObject(req, "ids");
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(a, cJSON_CreateString(ids[i]));
    }
    cJSON_AddItemToArray(cJSON_AddArrayToObject(req, "select"), cJSON_CreateString("status"));
    char *body = cJSON_PrintUnformatted(req);
    struct buf b = {0};
    long status = post("get", body, &b);
    cJSON *o = status == 200 && b.data ? cJSON_Parse(b.data) : NULL;
    if (!cJSON_IsArray(o)) {
        complain(status, &b);
    } else {
        pthread_mutex_lock(&mu);
        const cJSON *d;
        cJSON_ArrayForEach(d, o) {
            const cJSON *id = cJSON_GetObjectItem(d, "id");
            for (int i = 0; i < count; i++) {
                struct lamp *l = &sh.st.lamp[i];
                if (!cJSON_IsString(id) || strcasecmp(id->valuestring, l->id) != 0) {
                    continue;
                }
                const cJSON *on = cJSON_GetObjectItem(d, "online");
                l->online = cJSON_IsTrue(on) || (cJSON_IsNumber(on) && on->valueint);
                l->known |= read_status(cJSON_GetObjectItem(d, "status"), &l->on, &l->brightness);
            }
        }
        sh.st.message[0] = '\0';
        sh.st.version++;
        sh.dirty = true;
        pthread_mutex_unlock(&mu);
    }
    cJSON_Delete(o);
    cJSON_Delete(req);
    free(body);
    free(b.data);
}

// The oldest lamp with a wish not sent yet, or -1.
static int next_wish(struct want *w, uint32_t *seq, char *id, size_t id_size) {
    for (int i = 0; i < sh.st.count; i++) {
        if (sh.want_seq[i] != sh.sent_seq[i]) {
            *w = sh.want[i];
            *seq = sh.want_seq[i];
            copy(id, id_size, sh.st.lamp[i].id);
            return i;
        }
    }
    return -1;
}

static void send_wish(int i, const struct want *w, const char *id) {
    char body[160];
    if (w->with_brightness) {
        snprintf(body, sizeof(body), "{\"id\":\"%s\",\"channel\":0,\"on\":%s,\"brightness\":%u}", id,
                 w->on ? "true" : "false", w->brightness);
    } else {
        snprintf(body, sizeof(body), "{\"id\":\"%s\",\"channel\":0,\"on\":%s}", id, w->on ? "true" : "false");
    }
    struct buf b = {0};
    long status = post("set/light", body, &b);
    if (status != 200) {
        complain(status, &b);
    } else {
        PUBLISH(st->lamp[i].on = w->on; if (w->with_brightness) st->lamp[i].brightness = w->brightness;
                st->message[0] = '\0');
    }
    free(b.data);
}

static void *worker(void *arg) {
    (void)arg;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    uint32_t next_req = 0, next_poll = 0;

    for (;;) {
        pthread_mutex_lock(&mu);
        struct want w;
        uint32_t seq = 0;
        char id[40];
        int lamp;
        for (;;) {
            uint32_t t = now_ms();
            if (sh.kick) {
                sh.kick = false;
                next_poll = t;
            }
            lamp = next_wish(&w, &seq, id, sizeof(id));
            bool due = lamp >= 0 || (int32_t)(t - next_poll) >= 0;
            if (due && (int32_t)(t - next_req) >= 0) {
                break;
            }
            uint32_t until = due ? next_req : next_poll;
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            uint32_t wait = LV_MIN(until - t, 1000u);
            ts.tv_nsec += (long)wait * 1000000;
            ts.tv_sec += ts.tv_nsec / 1000000000;
            ts.tv_nsec %= 1000000000;
            pthread_cond_timedwait(&cv, &mu, &ts);
        }
        if (lamp >= 0) {
            sh.sent_seq[lamp] = seq;
        }
        bool visible = sh.visible;
        pthread_mutex_unlock(&mu);

        if (lamp >= 0) {
            send_wish(lamp, &w, id);
            if ((int32_t)(next_poll - now_ms()) < SETTLE_MS) {
                next_poll = now_ms() + SETTLE_MS;
            }
        } else {
            poll();
            next_poll = now_ms() + (visible ? POLL_SHOWN_MS : POLL_HIDDEN_MS);
        }
        next_req = now_ms() + GAP_MS;
    }
    return NULL;
}

// ── LVGL side ────────────────────────────────────────────────────────────────

static struct lights_state pub;
static bool started;

static void collect(lv_timer_t *t) {
    (void)t;
    pthread_mutex_lock(&mu);
    if (sh.dirty) {
        sh.dirty = false;
        struct lights_state s = sh.st;
        for (int i = 0; i < s.count; i++) {
            // A wish not yet confirmed by a poll that came after it: keep it.
            if (sh.want_seq[i] != sh.sent_seq[i] || !sh.st.lamp[i].known) {
                s.lamp[i].on = pub.lamp[i].on;
                s.lamp[i].brightness = pub.lamp[i].brightness;
            }
        }
        s.version = pub.version + 1;
        pub = s;
    }
    pthread_mutex_unlock(&mu);
}

bool lights_add(const char *spec) {
    const char *colon = strchr(spec, ':');
    if (pub.count >= LIGHTS_MAX || !colon || colon == spec || !colon[1]) {
        deck_log("lights: ignoring light=%s (NAME:deviceid, at most %d)", spec, LIGHTS_MAX);
        return false;
    }
    struct lamp *l = &pub.lamp[pub.count++];
    snprintf(l->name, sizeof(l->name), "%.*s", (int)(colon - spec), spec);
    for (char *c = l->name; *c; c++) {
        *c = (char)toupper((unsigned char)*c);
    }
    copy(l->id, sizeof(l->id), colon + 1);
    l->brightness = 100;
    pub.auth = LIGHTS_NO_KEY;
    return true;
}

static bool key_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        return false;
    }
    if (!fgets(key, sizeof(key), f)) {
        key[0] = '\0';
    }
    fclose(f);
    key[strcspn(key, "\r\n \t")] = '\0';
    return key[0] != '\0';
}

void lights_start(const char *srv, const char *key_path) {
    if (!pub.count) {
        pub.auth = LIGHTS_NONE;
        return;
    }
    // The host as the Shelly app shows it, with or without https://. Plain
    // http only to this machine: pi/tools/shelly-test.sh mock.
    const char *s = srv ? srv : "";
    if (!strncmp(s, "http://127.0.0.1:", 17)) {
        copy(base, sizeof(base), s);
    } else {
        s += !strncmp(s, "https://", 8) ? 8 : !strncmp(s, "http://", 7) ? 7 : 0;
        snprintf(base, sizeof(base), "https://%.*s", (int)strcspn(s, "/"), s);
    }
    if (!s[0] || !key_load(key_path)) {
        pub.auth = LIGHTS_NO_KEY;
        deck_log("lights: %d lamp(s), but no %s yet", pub.count, s[0] ? "key" : "shelly_server");
        return;
    }
    pub.auth = LIGHTS_OK;
    sh.st = pub;
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) != 0) {
        return;
    }
    pthread_detach(t);
    started = true;
    deck_log("lights: %d lamp(s) through %s", pub.count, base);
    lv_timer_create(collect, 100, NULL);
}

const struct lights_state *lights_state(void) { return &pub; }

void lights_visible(bool visible) {
    pthread_mutex_lock(&mu);
    sh.kick |= visible && !sh.visible;
    sh.visible = visible;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
}

static void wish(int i, bool on, int brightness) {
    if (!started || i < 0 || i >= pub.count) {
        return;
    }
    pub.lamp[i].on = on;
    if (brightness >= 0) {
        pub.lamp[i].brightness = (uint8_t)brightness;
    }
    pub.version++;
    pthread_mutex_lock(&mu);
    struct want *w = &sh.want[i];
    w->on = on;
    if (brightness >= 0) {
        w->with_brightness = true;
        w->brightness = (uint8_t)brightness;
    }
    if (sh.want_seq[i] == sh.sent_seq[i] && brightness < 0) {
        w->with_brightness = false; // a new wish: only what changed
    }
    sh.want_seq[i]++;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
}

void lights_set_on(int i, bool on) { wish(i, on, -1); }

void lights_set_brightness(int i, uint8_t pct) { wish(i, true, LV_CLAMP(1, pct, 100)); }
