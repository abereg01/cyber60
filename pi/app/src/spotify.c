/*
 * Spotify through its Web API. One worker thread does all the network work
 * (libcurl, one connection kept alive); the LVGL thread reads `pub` and queues
 * commands. They meet in `sh` under `mu`.
 *
 * Login, once: the Pi can't show Spotify's login page, so the phone does it.
 * While logged out, a tiny web server listens on PORT and the screen shows a
 * QR code for http://<this Pi>:PORT/. That sends the phone on to Spotify's
 * consent page (Authorization Code with PKCE: no client secret anywhere).
 * Spotify only returns to https:// pages, so it lands on docs/spotify/ on
 * GitHub Pages, which forwards the code to http://<this Pi>:PORT/callback (the
 * Pi put its own address in `state`). The worker swaps the code, with the
 * verifier that never left the Pi, for tokens and keeps the refresh token in
 * <data dir>/spotify.json (root-only). The server then closes for good.
 *
 * Logged in: /me/player is polled every second while the page is on screen
 * (every 10 s otherwise); when the track changes, its cover is fetched and
 * decoded here, off the screen's thread. Commands run in order, each followed
 * by a quick poll. A command bumps cmd_sent on the LVGL side and done_seq here
 * once run, so a poll that started before a command can't undo its
 * optimistic change on screen.
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE // accept4
#include "spotify.h"

#include <arpa/inet.h>
#include <cJSON.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <jpeglib.h>
#include <netinet/in.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/random.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "deck.h"

#define PORT 8899
#define REDIRECT "https://abereg01.github.io/cyber60/spotify/"
#define SCOPES                                                                                     \
    "user-read-playback-state user-modify-playback-state user-read-currently-playing "            \
    "playlist-read-private playlist-read-collaborative user-library-read user-library-modify"
#define API "https://api.spotify.com/v1"
#define POLL_SHOWN_MS 1000
#define POLL_HIDDEN_MS 10000
#define CMDS 16
#define THUMB_QUEUE 64
#define THUMB_CACHE 96

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// ── Shared between the threads (under mu) ────────────────────────────────────

enum cmd_kind { C_PLAY, C_PAUSE, C_NEXT, C_PREV, C_SEEK, C_SHUFFLE, C_REPEAT, C_LIKE, C_PLAY_URI,
                C_TRANSFER, C_LOGOUT };

struct cmd {
    enum cmd_kind kind;
    int64_t arg;
    char str[128];
};

struct thumb_px {
    char url[256];
    uint16_t *px;
};

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

static struct {
    struct sp_state st; // as the worker last saw it
    uint32_t done_seq;  // commands run before st was polled
    bool dirty;

    uint16_t *cover; // a new cover for cover_url, handed to the LVGL side
    char cover_url[256];

    struct cmd cmds[CMDS];
    int cmd_head, cmd_len;

    int list_req; // enum sp_list, -1 none
    bool list_done, list_ok;
    enum sp_list list_kind;
    struct sp_item list[SP_MAX_ITEMS];
    int list_count;

    char thumb_q[THUMB_QUEUE][256]; // to fetch
    int thumb_qn;
    struct thumb_px thumb_done[16]; // fetched, not yet collected
    int thumb_dn;

    bool visible, kick; // kick: the page just came on screen, poll now
} sh = {.list_req = -1};

// ── Worker-only ──────────────────────────────────────────────────────────────

static char client_id[64], token_path[300];
static char access_token[1024], refresh_token[1024], user_id[96];
static uint32_t token_until;
static CURL *curl;
static int srv = -1;
static char verifier[96], pending_state[128];
static char cur_uri[64], cur_cover[256];
static uint32_t done_local;

// ── Small helpers ────────────────────────────────────────────────────────────

static void copy(char *dst, size_t size, const char *src) { snprintf(dst, size, "%s", src ? src : ""); }

static const char *jstr(const cJSON *o, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static double jnum(const cJSON *o, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : 0;
}

static bool jbool(const cJSON *o, const char *key) {
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, key));
}

static void urlenc(char *dst, size_t size, const char *s) {
    size_t n = 0;
    for (; *s && n + 4 < size; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            dst[n++] = (char)c;
        } else {
            n += (size_t)snprintf(dst + n, size - n, "%%%02X", c);
        }
    }
    dst[n] = '\0';
}

// Query parameter `key` of "a=1&b=2", percent-decoded.
static bool query_get(const char *q, const char *key, char *dst, size_t size) {
    size_t kl = strlen(key);
    for (const char *p = q; p && *p; p = strchr(p, '&') ? strchr(p, '&') + 1 : NULL) {
        if (strncmp(p, key, kl) != 0 || p[kl] != '=') {
            continue;
        }
        size_t n = 0;
        for (p += kl + 1; *p && *p != '&' && *p != ' ' && n + 1 < size; p++) {
            if (*p == '%' && p[1] && p[2]) {
                char hex[3] = {p[1], p[2], 0};
                dst[n++] = (char)strtol(hex, NULL, 16);
                p += 2;
            } else {
                dst[n++] = *p == '+' ? ' ' : *p;
            }
        }
        dst[n] = '\0';
        return true;
    }
    return false;
}

// ── SHA-256 and base64url, for PKCE ──────────────────────────────────────────

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256(const uint8_t *msg, size_t len, uint8_t out[32]) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    size_t total = ((len + 9 + 63) / 64) * 64;
    uint8_t *m = calloc(1, total);
    memcpy(m, msg, len);
    m[len] = 0x80;
    for (int i = 0; i < 8; i++) {
        m[total - 1 - i] = (uint8_t)((uint64_t)len * 8 >> (8 * i));
    }
    for (size_t off = 0; off < total; off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            const uint8_t *b = m + off + 4 * i;
            w[i] = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
        }
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
    }
    free(m);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(h[i] >> 24), out[4 * i + 1] = (uint8_t)(h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(h[i] >> 8), out[4 * i + 3] = (uint8_t)h[i];
    }
}

static void b64url(const uint8_t *in, size_t n, char *out) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) |
                     (i + 2 < n ? in[i + 2] : 0);
        out[o++] = t[v >> 18 & 63];
        out[o++] = t[v >> 12 & 63];
        if (i + 1 < n) {
            out[o++] = t[v >> 6 & 63];
        }
        if (i + 2 < n) {
            out[o++] = t[v & 63];
        }
    }
    out[o] = '\0';
}

static void random_text(char *out, size_t n) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    uint8_t r[128];
    if (n > sizeof(r)) {
        n = sizeof(r);
    }
    if (getrandom(r, n, 0) != (ssize_t)n) {
        for (size_t i = 0; i < n; i++) {
            r[i] = (uint8_t)rand();
        }
    }
    for (size_t i = 0; i + 1 < n; i++) {
        out[i] = t[r[i] % (sizeof(t) - 1)];
    }
    out[n - 1] = '\0';
}

// ── Publishing ───────────────────────────────────────────────────────────────

// Change sh.st under the lock; `code` edits it as `st`.
#define PUBLISH(code)                                                                              \
    do {                                                                                           \
        pthread_mutex_lock(&mu);                                                                   \
        struct sp_state *st = &sh.st;                                                              \
        code;                                                                                      \
        st->version++;                                                                             \
        sh.dirty = true;                                                                           \
        pthread_mutex_unlock(&mu);                                                                 \
    } while (0)

static void message(const char *m) { PUBLISH(copy(st->message, sizeof(st->message), m)); }

// ── HTTP ─────────────────────────────────────────────────────────────────────

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

static long retry_after;

static size_t on_header(char *p, size_t size, size_t n, void *arg) {
    (void)arg;
    if (!strncasecmp(p, "Retry-After:", 12)) {
        retry_after = atol(p + 12);
    }
    return size * n;
}

// One request. Returns the HTTP status (0 = no connection); the body in *out.
static long http(const char *method, const char *url, const char *auth, const char *type,
                 const char *body, struct buf *out) {
    struct curl_slist *h = NULL;
    char line[1200];
    if (auth) {
        snprintf(line, sizeof(line), "Authorization: Bearer %s", auth);
        h = curl_slist_append(h, line);
    }
    if (type) {
        snprintf(line, sizeof(line), "Content-Type: %s", type);
        h = curl_slist_append(h, line);
    }
    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 6L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "deck/1 (desk screen)");
    if (strcmp(method, "GET") != 0) {
        // PUT/POST with no body still need Content-Length: 0 for Spotify.
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body : "");
    }
    retry_after = 0;
    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    if (rc == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }
    curl_slist_free_all(h);
    return status;
}

// ── Tokens ───────────────────────────────────────────────────────────────────

static void token_save(void) {
    char tmp[320];
    snprintf(tmp, sizeof(tmp), "%s.new", token_path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        deck_log("spotify: can't save the login: %s", strerror(errno));
        return;
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "refresh_token", refresh_token);
    char *s = cJSON_PrintUnformatted(o);
    if (write(fd, s, strlen(s)) < 0) {
        deck_log("spotify: can't save the login: %s", strerror(errno));
    }
    fsync(fd);
    close(fd);
    rename(tmp, token_path);
    free(s);
    cJSON_Delete(o);
}

static void token_load(void) {
    FILE *f = fopen(token_path, "r");
    if (!f) {
        return;
    }
    char s[2048] = "";
    size_t n = fread(s, 1, sizeof(s) - 1, f);
    s[n] = '\0';
    fclose(f);
    cJSON *o = cJSON_Parse(s);
    copy(refresh_token, sizeof(refresh_token), jstr(o, "refresh_token"));
    cJSON_Delete(o);
}

static void logged_out(const char *why) {
    access_token[0] = refresh_token[0] = user_id[0] = cur_uri[0] = cur_cover[0] = '\0';
    unlink(token_path);
    PUBLISH(st->auth = SP_LOGGED_OUT; st->has_player = false;
            copy(st->message, sizeof(st->message), why));
}

// The token endpoint: form in, tokens out.
static bool token_call(const char *form) {
    struct buf b = {0};
    long status = http("POST", "https://accounts.spotify.com/api/token", NULL,
                       "application/x-www-form-urlencoded", form, &b);
    cJSON *o = b.data ? cJSON_Parse(b.data) : NULL;
    bool ok = status == 200 && *jstr(o, "access_token");
    if (ok) {
        copy(access_token, sizeof(access_token), jstr(o, "access_token"));
        token_until = now_ms() + (uint32_t)(jnum(o, "expires_in") - 60) * 1000;
        if (*jstr(o, "refresh_token")) { // PKCE refresh tokens rotate
            copy(refresh_token, sizeof(refresh_token), jstr(o, "refresh_token"));
            token_save();
        }
    } else if (status == 400 && !strcmp(jstr(o, "error"), "invalid_grant")) {
        deck_log("spotify: the login was revoked or expired");
        logged_out("LOGGED OUT BY SPOTIFY: SCAN AGAIN");
    } else {
        deck_log("spotify: token call failed (%ld): %.200s", status, b.data ? b.data : "");
    }
    cJSON_Delete(o);
    free(b.data);
    return ok;
}

static bool refresh(void) {
    char form[1400], enc[1100];
    urlenc(enc, sizeof(enc), refresh_token);
    snprintf(form, sizeof(form), "grant_type=refresh_token&refresh_token=%s&client_id=%s", enc, client_id);
    return token_call(form);
}

// A Web API call with the access token, refreshed when it runs out or is
// refused once. *json gets the parsed body (may be NULL). Returns the status.
static long api(const char *method, const char *path, const char *body, cJSON **json) {
    char url[1024];
    snprintf(url, sizeof(url), API "%s", path);
    if (json) {
        *json = NULL;
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        if ((!access_token[0] || (int32_t)(now_ms() - token_until) > 0) && !refresh()) {
            return 0;
        }
        struct buf b = {0};
        long status = http(method, url, access_token, body ? "application/json" : NULL, body, &b);
        if (status == 401 && attempt == 0) {
            access_token[0] = '\0';
            free(b.data);
            continue;
        }
        if (status == 429) {
            deck_log("spotify: rate limited, waiting %ld s", retry_after);
            sleep((unsigned)LV_CLAMP(1, retry_after, 30));
        }
        if (json && b.data) {
            *json = cJSON_Parse(b.data);
        }
        free(b.data);
        return status;
    }
    return 401;
}

// ── Images ───────────────────────────────────────────────────────────────────
// Covers come as JPEG. libjpeg shrinks by a power of two while decoding, then
// a box filter takes the centre square to exactly `px`, as RGB565.

struct jerr {
    struct jpeg_error_mgr mgr;
    jmp_buf jb;
};

static void jpeg_fail(j_common_ptr c) { longjmp(((struct jerr *)c->err)->jb, 1); }

static uint16_t *decode(const uint8_t *data, size_t n, int px) {
    struct jpeg_decompress_struct c;
    struct jerr e;
    uint8_t *volatile rgb = NULL;
    c.err = jpeg_std_error(&e.mgr);
    e.mgr.error_exit = jpeg_fail;
    if (setjmp(e.jb)) {
        jpeg_destroy_decompress(&c);
        free(rgb);
        return NULL;
    }
    jpeg_create_decompress(&c);
    jpeg_mem_src(&c, data, (unsigned long)n);
    jpeg_read_header(&c, TRUE);
    c.out_color_space = JCS_RGB;
    c.scale_num = 1;
    c.scale_denom = 1;
    while (c.scale_denom < 8 && c.image_width / (c.scale_denom * 2) >= (unsigned)px &&
           c.image_height / (c.scale_denom * 2) >= (unsigned)px) {
        c.scale_denom *= 2;
    }
    jpeg_start_decompress(&c);
    int w = (int)c.output_width, h = (int)c.output_height;
    rgb = malloc((size_t)w * h * 3);
    while (rgb && c.output_scanline < c.output_height) {
        JSAMPROW row = rgb + (size_t)c.output_scanline * w * 3;
        jpeg_read_scanlines(&c, &row, 1);
    }
    jpeg_finish_decompress(&c);
    jpeg_destroy_decompress(&c);
    if (!rgb) {
        return NULL;
    }

    int side = LV_MIN(w, h), x0 = (w - side) / 2, y0 = (h - side) / 2;
    uint16_t *out = malloc((size_t)px * px * 2);
    for (int oy = 0; out && oy < px; oy++) {
        int sy0 = y0 + oy * side / px, sy1 = LV_MAX(sy0 + 1, y0 + (oy + 1) * side / px);
        for (int ox = 0; ox < px; ox++) {
            int sx0 = x0 + ox * side / px, sx1 = LV_MAX(sx0 + 1, x0 + (ox + 1) * side / px);
            uint32_t r = 0, g = 0, bl = 0, cnt = 0;
            for (int sy = sy0; sy < sy1; sy++) {
                const uint8_t *p = rgb + ((size_t)sy * w + sx0) * 3;
                for (int sx = sx0; sx < sx1; sx++, p += 3, cnt++) {
                    r += p[0], g += p[1], bl += p[2];
                }
            }
            r /= cnt, g /= cnt, bl /= cnt;
            out[oy * px + ox] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (bl >> 3));
        }
    }
    free(rgb);
    return out;
}

static uint16_t *fetch_image(const char *url, int px) {
    struct buf b = {0};
    long status = http("GET", url, NULL, NULL, NULL, &b);
    uint16_t *out = status == 200 && b.len ? decode((const uint8_t *)b.data, b.len, px) : NULL;
    free(b.data);
    return out;
}

// The smallest image at least `px` wide (Spotify lists them largest first).
static const char *pick_image(const cJSON *images, int px) {
    const char *best = "";
    const cJSON *img;
    cJSON_ArrayForEach(img, images) {
        double w = jnum(img, "width");
        if (!*best || w == 0 || w >= px) {
            best = jstr(img, "url");
        }
    }
    return best;
}

// ── Login (worker) ───────────────────────────────────────────────────────────

static bool local_ip(char *ip, size_t size) {
    struct ifaddrs *ifs, *i;
    bool found = false;
    if (getifaddrs(&ifs) != 0) {
        return false;
    }
    for (int pass = 0; pass < 2 && !found; pass++) { // wlan0 first, then anything
        for (i = ifs; i && !found; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !strcmp(i->ifa_name, "lo") ||
                (pass == 0 && strcmp(i->ifa_name, "wlan0") != 0) || strncmp(i->ifa_name, "docker", 6) == 0) {
                continue;
            }
            inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, ip, (socklen_t)size);
            found = true;
        }
    }
    freeifaddrs(ifs);
    return found;
}

static void reply(int fd, int status, const char *what, const char *extra, const char *html) {
    char head[1024];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %d %s\r\nConnection: close\r\nCache-Control: no-store\r\n"
                     "Content-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\n%s\r\n",
                     status, what, strlen(html), extra);
    if (write(fd, head, (size_t)n) < 0 || write(fd, html, strlen(html)) < 0) {
        deck_log("spotify: login page reply failed");
    }
}

static void page(int fd, int status, const char *title, const char *text) {
    char html[1400];
    snprintf(html, sizeof(html),
             "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
             "<title>Desk screen</title><body style='margin:0;min-height:100vh;display:grid;"
             "place-items:center;background:#fff;color:#2B4351;font:600 18px/1.5 system-ui'>"
             "<main style='padding:2rem;max-width:24rem'><h1 style='font-size:2rem;margin:0 0 .5rem'>%s</h1>"
             "<p style='color:#7A99AC'>%s</p></main>",
             title, text);
    reply(fd, status, status == 200 ? "OK" : "Bad Request", "", html);
}

static bool exchange(const char *code) {
    char form[2048], c[1024], r[256];
    urlenc(c, sizeof(c), code);
    urlenc(r, sizeof(r), REDIRECT);
    snprintf(form, sizeof(form),
             "grant_type=authorization_code&code=%s&redirect_uri=%s&client_id=%s&code_verifier=%s", c, r,
             client_id, verifier);
    if (!token_call(form) || !refresh_token[0]) {
        return false;
    }
    token_save();
    return true;
}

static void handle(int fd, const char *ip) {
    char req[4096];
    struct timeval tv = {3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ssize_t n = read(fd, req, sizeof(req) - 1);
    if (n <= 0) {
        return;
    }
    req[n] = '\0';
    char path[2048] = "";
    sscanf(req, "GET %2047s", path);
    char *query = strchr(path, '?');
    if (query) {
        *query++ = '\0';
    }

    if (!strcmp(path, "/")) {
        // A fresh verifier and state per scan: the phone goes on to Spotify.
        uint8_t digest[32];
        char nonce[17], challenge[64], scope[256], redirect[256], url[1400], loc[1500];
        random_text(verifier, 65);
        random_text(nonce, sizeof(nonce));
        sha256((const uint8_t *)verifier, strlen(verifier), digest);
        b64url(digest, 32, challenge);
        snprintf(pending_state, sizeof(pending_state), "%s~%s~%d", nonce, ip, PORT);
        urlenc(scope, sizeof(scope), SCOPES);
        urlenc(redirect, sizeof(redirect), REDIRECT);
        snprintf(url, sizeof(url),
                 "https://accounts.spotify.com/authorize?response_type=code&client_id=%s&scope=%s"
                 "&redirect_uri=%s&code_challenge_method=S256&code_challenge=%s&state=%s",
                 client_id, scope, redirect, challenge, pending_state);
        snprintf(loc, sizeof(loc), "Location: %s\r\n", url);
        reply(fd, 302, "Found", loc, "");
        deck_log("spotify: a phone opened the login");
        return;
    }
    if (strcmp(path, "/callback") != 0) {
        reply(fd, 404, "Not Found", "", "");
        return;
    }
    char state[128] = "", code[1024] = "", err[64] = "";
    query_get(query, "state", state, sizeof(state));
    query_get(query, "code", code, sizeof(code));
    query_get(query, "error", err, sizeof(err));
    if (!pending_state[0] || strcmp(state, pending_state) != 0) {
        page(fd, 400, "Old link", "Scan the QR code on the desk screen again.");
        return;
    }
    pending_state[0] = '\0';
    if (err[0] || !code[0]) {
        page(fd, 400, "Not logged in", "The login was cancelled. Scan the QR code to try again.");
        deck_log("spotify: login cancelled (%s)", err);
        return;
    }
    page(fd, 200, "Logged in ✓", "The desk screen has it. You can close this page.");
    shutdown(fd, SHUT_RDWR); // the phone is done; the token swap can take a moment
    PUBLISH(st->auth = SP_LOGGING_IN);
    if (exchange(code)) {
        deck_log("spotify: logged in");
        PUBLISH(st->auth = SP_LOGGED_IN; st->message[0] = '\0');
    } else {
        PUBLISH(st->auth = SP_LOGGED_OUT; copy(st->message, sizeof(st->message), "LOGIN FAILED: SCAN AGAIN"));
    }
}

// Logged out: keep the QR code's address current and answer the phone.
static void login_step(void) {
    char ip[INET_ADDRSTRLEN] = "", url[64] = "";
    if (local_ip(ip, sizeof(ip))) {
        snprintf(url, sizeof(url), "http://%s:%d/", ip, PORT);
    }
    pthread_mutex_lock(&mu);
    if (sh.st.auth != SP_LOGGED_OUT || strcmp(sh.st.login_url, url) != 0) {
        sh.st.auth = SP_LOGGED_OUT;
        copy(sh.st.login_url, sizeof(sh.st.login_url), url);
        sh.st.version++;
        sh.dirty = true;
    }
    pthread_mutex_unlock(&mu);
    if (!ip[0]) {
        sleep(1);
        return;
    }
    if (srv < 0) {
        srv = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        int one = 1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(PORT), .sin_addr.s_addr = INADDR_ANY};
        if (bind(srv, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(srv, 4) < 0) {
            deck_log("spotify: can't listen on port %d: %s", PORT, strerror(errno));
            close(srv);
            srv = -1;
            sleep(5);
            return;
        }
        deck_log("spotify: waiting for the phone at %s", url);
    }
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(srv, &rd);
    struct timeval tv = {0, 500000};
    if (select(srv + 1, &rd, NULL, NULL, &tv) > 0) {
        int fd = accept4(srv, NULL, NULL, SOCK_CLOEXEC);
        if (fd >= 0) {
            handle(fd, ip);
            close(fd);
        }
    }
}

// ── Player (worker) ──────────────────────────────────────────────────────────

static void join_artists(const cJSON *artists, char *out, size_t size) {
    const cJSON *a;
    out[0] = '\0';
    cJSON_ArrayForEach(a, artists) {
        size_t n = strlen(out);
        snprintf(out + n, size - n, "%s%s", n ? ", " : "", jstr(a, "name"));
    }
}

static void check_liked(const char *uri) {
    char enc[128], path[200];
    urlenc(enc, sizeof(enc), uri);
    snprintf(path, sizeof(path), "/me/library/contains?uris=%s", enc);
    cJSON *j;
    if (api("GET", path, NULL, &j) == 200 && cJSON_IsArray(j)) {
        bool liked = cJSON_IsTrue(cJSON_GetArrayItem(j, 0));
        PUBLISH(st->liked = liked);
    }
    cJSON_Delete(j);
}

static void poll_player(void) {
    uint32_t seq = done_local;
    cJSON *j;
    long status = api("GET", "/me/player?additional_types=episode", NULL, &j);
    if (status == 0) {
        message("NO CONNECTION");
        cJSON_Delete(j);
        return;
    }
    if (status == 204 || (status == 200 && !cJSON_GetObjectItem(j, "item"))) {
        pthread_mutex_lock(&mu);
        sh.st.has_player = false;
        sh.st.playing = false;
        sh.st.version++;
        sh.done_seq = seq;
        sh.dirty = true;
        pthread_mutex_unlock(&mu);
        cur_uri[0] = '\0';
        cJSON_Delete(j);
        return;
    }
    if (status != 200) {
        deck_log("spotify: player %ld", status);
        cJSON_Delete(j);
        return;
    }
    const cJSON *item = cJSON_GetObjectItem(j, "item"), *device = cJSON_GetObjectItem(j, "device");
    struct sp_track t = {0};
    copy(t.uri, sizeof(t.uri), jstr(item, "uri"));
    copy(t.title, sizeof(t.title), jstr(item, "name"));
    t.duration_ms = (uint32_t)jnum(item, "duration_ms");
    const cJSON *images;
    if (!strcmp(jstr(item, "type"), "episode")) {
        const cJSON *show = cJSON_GetObjectItem(item, "show");
        copy(t.artist, sizeof(t.artist), jstr(show, "name"));
        copy(t.album, sizeof(t.album), jstr(show, "publisher"));
        images = cJSON_GetObjectItem(item, "images");
    } else {
        const cJSON *album = cJSON_GetObjectItem(item, "album");
        join_artists(cJSON_GetObjectItem(item, "artists"), t.artist, sizeof(t.artist));
        copy(t.album, sizeof(t.album), jstr(album, "name"));
        images = cJSON_GetObjectItem(album, "images");
    }
    copy(t.cover_url, sizeof(t.cover_url), pick_image(images, SP_COVER_PX));
    const char *rep = jstr(j, "repeat_state");
    bool changed = strcmp(t.uri, cur_uri) != 0;

    pthread_mutex_lock(&mu);
    struct sp_state *st = &sh.st;
    st->has_player = true;
    st->playing = jbool(j, "is_playing");
    st->shuffle = jbool(j, "shuffle_state");
    st->repeat = !strcmp(rep, "track") ? SP_REPEAT_TRACK : !strcmp(rep, "context") ? SP_REPEAT_CONTEXT
                                                                                    : SP_REPEAT_OFF;
    st->progress_ms = (uint32_t)jnum(j, "progress_ms");
    st->progress_tick = now_ms();
    st->track = t;
    copy(st->device, sizeof(st->device), jstr(device, "name"));
    copy(st->device_type, sizeof(st->device_type), jstr(device, "type"));
    if (changed) {
        st->liked = false;
    }
    if (!strcmp(st->message, "NO CONNECTION")) {
        st->message[0] = '\0';
    }
    st->version++;
    sh.done_seq = seq;
    sh.dirty = true;
    pthread_mutex_unlock(&mu);
    cJSON_Delete(j);

    if (changed) {
        copy(cur_uri, sizeof(cur_uri), t.uri);
        if (!strncmp(t.uri, "spotify:track:", 14)) {
            check_liked(t.uri);
        }
    }
    if (t.cover_url[0] && strcmp(t.cover_url, cur_cover) != 0) {
        copy(cur_cover, sizeof(cur_cover), t.cover_url);
        uint16_t *px = fetch_image(t.cover_url, SP_COVER_PX);
        pthread_mutex_lock(&mu);
        free(sh.cover);
        sh.cover = px;
        copy(sh.cover_url, sizeof(sh.cover_url), t.cover_url);
        pthread_mutex_unlock(&mu);
    }
}

static void run(const struct cmd *c) {
    char path[512], body[256], enc[200];
    long status = 0;
    switch (c->kind) {
    case C_PLAY:
        status = api("PUT", "/me/player/play", NULL, NULL);
        break;
    case C_PAUSE:
        status = api("PUT", "/me/player/pause", NULL, NULL);
        break;
    case C_NEXT:
        status = api("POST", "/me/player/next", NULL, NULL);
        break;
    case C_PREV:
        status = api("POST", "/me/player/previous", NULL, NULL);
        break;
    case C_SEEK:
        snprintf(path, sizeof(path), "/me/player/seek?position_ms=%lld", (long long)c->arg);
        status = api("PUT", path, NULL, NULL);
        break;
    case C_SHUFFLE:
        snprintf(path, sizeof(path), "/me/player/shuffle?state=%s", c->arg ? "true" : "false");
        status = api("PUT", path, NULL, NULL);
        break;
    case C_REPEAT:
        snprintf(path, sizeof(path), "/me/player/repeat?state=%s",
                 c->arg == SP_REPEAT_TRACK ? "track" : c->arg == SP_REPEAT_CONTEXT ? "context" : "off");
        status = api("PUT", path, NULL, NULL);
        break;
    case C_LIKE:
        urlenc(enc, sizeof(enc), c->str);
        snprintf(path, sizeof(path), "/me/library?uris=%s", enc);
        status = api(c->arg ? "PUT" : "DELETE", path, NULL, NULL);
        break;
    case C_PLAY_URI: {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "context_uri", c->str);
        char *s = cJSON_PrintUnformatted(o);
        status = api("PUT", "/me/player/play", s, NULL);
        free(s);
        cJSON_Delete(o);
        break;
    }
    case C_TRANSFER:
        snprintf(body, sizeof(body), "{\"device_ids\":[\"%.100s\"],\"play\":true}", c->str);
        status = api("PUT", "/me/player", body, NULL);
        break;
    case C_LOGOUT:
        deck_log("spotify: logged out on the screen");
        logged_out("");
        return;
    }
    if (status == 404) {
        message("NO DEVICE PLAYING: PICK ONE IN DEVICES");
    } else if (status == 403) {
        message("SPOTIFY SAID NO (PREMIUM NEEDED?)");
    } else if (status >= 300 || status == 0) {
        deck_log("spotify: command %d failed (%ld)", c->kind, status);
        message(status ? "SPOTIFY DIDN'T TAKE THAT" : "NO CONNECTION");
    } else {
        message("");
    }
}

// ── Lists (worker) ───────────────────────────────────────────────────────────

static int list_fill(enum sp_list kind, struct sp_item *it) {
    cJSON *j;
    const cJSON *e;
    int n = 0;
    switch (kind) {
    case SP_LIST_PLAYLISTS:
        if (user_id[0]) {
            copy(it[n].name, sizeof(it[n].name), "Liked Songs");
            copy(it[n].sub, sizeof(it[n].sub), "YOUR LIBRARY");
            snprintf(it[n].uri, sizeof(it[n].uri), "spotify:user:%s:collection", user_id);
            n++;
        }
        if (api("GET", "/me/playlists?limit=49", NULL, &j) != 200) {
            break;
        }
        cJSON_ArrayForEach(e, cJSON_GetObjectItem(j, "items")) {
            if (n >= SP_MAX_ITEMS) {
                break;
            }
            // "items" (Feb 2026), "tracks" before.
            const cJSON *count = cJSON_GetObjectItem(e, "items");
            if (!count) {
                count = cJSON_GetObjectItem(e, "tracks");
            }
            copy(it[n].name, sizeof(it[n].name), jstr(e, "name"));
            snprintf(it[n].sub, sizeof(it[n].sub), "%s · %d", jstr(cJSON_GetObjectItem(e, "owner"), "display_name"),
                     (int)jnum(count, "total"));
            copy(it[n].uri, sizeof(it[n].uri), jstr(e, "uri"));
            copy(it[n].image, sizeof(it[n].image), pick_image(cJSON_GetObjectItem(e, "images"), SP_THUMB_PX));
            n++;
        }
        cJSON_Delete(j);
        return n;
    case SP_LIST_ALBUMS:
        if (api("GET", "/me/albums?limit=50", NULL, &j) != 200) {
            break;
        }
        cJSON_ArrayForEach(e, cJSON_GetObjectItem(j, "items")) {
            const cJSON *a = cJSON_GetObjectItem(e, "album");
            if (n >= SP_MAX_ITEMS) {
                break;
            }
            copy(it[n].name, sizeof(it[n].name), jstr(a, "name"));
            join_artists(cJSON_GetObjectItem(a, "artists"), it[n].sub, sizeof(it[n].sub));
            copy(it[n].uri, sizeof(it[n].uri), jstr(a, "uri"));
            copy(it[n].image, sizeof(it[n].image), pick_image(cJSON_GetObjectItem(a, "images"), SP_THUMB_PX));
            n++;
        }
        cJSON_Delete(j);
        return n;
    case SP_LIST_QUEUE:
        if (api("GET", "/me/player/queue", NULL, &j) != 200) {
            break;
        }
        cJSON_ArrayForEach(e, cJSON_GetObjectItem(j, "queue")) {
            if (n >= 30) {
                break;
            }
            copy(it[n].name, sizeof(it[n].name), jstr(e, "name"));
            const cJSON *album = cJSON_GetObjectItem(e, "album");
            if (album) {
                join_artists(cJSON_GetObjectItem(e, "artists"), it[n].sub, sizeof(it[n].sub));
                copy(it[n].image, sizeof(it[n].image), pick_image(cJSON_GetObjectItem(album, "images"), SP_THUMB_PX));
            } else {
                copy(it[n].sub, sizeof(it[n].sub), jstr(cJSON_GetObjectItem(e, "show"), "name"));
                copy(it[n].image, sizeof(it[n].image), pick_image(cJSON_GetObjectItem(e, "images"), SP_THUMB_PX));
            }
            copy(it[n].uri, sizeof(it[n].uri), jstr(e, "uri"));
            n++;
        }
        cJSON_Delete(j);
        return n;
    case SP_LIST_DEVICES:
        if (api("GET", "/me/player/devices", NULL, &j) != 200) {
            break;
        }
        cJSON_ArrayForEach(e, cJSON_GetObjectItem(j, "devices")) {
            if (n >= SP_MAX_ITEMS) {
                break;
            }
            copy(it[n].name, sizeof(it[n].name), jstr(e, "name"));
            copy(it[n].sub, sizeof(it[n].sub), jstr(e, "type"));
            copy(it[n].uri, sizeof(it[n].uri), jstr(e, "id"));
            it[n].active = jbool(e, "is_active");
            n++;
        }
        cJSON_Delete(j);
        return n;
    }
    cJSON_Delete(j);
    return -1;
}

// ── Worker loop ──────────────────────────────────────────────────────────────

static void *worker(void *arg) {
    (void)arg;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    token_load();
    uint32_t next_poll = 0;
    PUBLISH(st->auth = refresh_token[0] ? SP_LOGGED_IN : SP_LOGGED_OUT);

    for (;;) {
        if (!refresh_token[0]) {
            login_step();
            continue;
        }
        if (srv >= 0) {
            close(srv);
            srv = -1;
        }
        if (!access_token[0] && !refresh()) {
            if (refresh_token[0]) {
                message("NO CONNECTION");
                sleep(5);
            }
            continue;
        }
        if (!user_id[0]) {
            cJSON *me;
            if (api("GET", "/me", NULL, &me) == 200) {
                copy(user_id, sizeof(user_id), jstr(me, "id"));
            }
            cJSON_Delete(me);
        }

        // Commands, in order.
        bool ran = false;
        for (;;) {
            struct cmd c;
            pthread_mutex_lock(&mu);
            bool have = sh.cmd_len > 0;
            if (have) {
                c = sh.cmds[sh.cmd_head];
                sh.cmd_head = (sh.cmd_head + 1) % CMDS;
                sh.cmd_len--;
            }
            pthread_mutex_unlock(&mu);
            if (!have) {
                break;
            }
            run(&c);
            done_local++;
            ran = true;
            if (!refresh_token[0]) {
                break; // logged out
            }
        }
        if (!refresh_token[0]) {
            continue;
        }
        if (ran) {
            usleep(300 * 1000); // let Spotify catch up before asking it
            next_poll = 0;
        }

        // A list someone asked for.
        pthread_mutex_lock(&mu);
        int want = sh.list_req;
        sh.list_req = -1;
        pthread_mutex_unlock(&mu);
        if (want >= 0) {
            static struct sp_item items[SP_MAX_ITEMS];
            memset(items, 0, sizeof(items));
            int n = list_fill((enum sp_list)want, items);
            pthread_mutex_lock(&mu);
            memcpy(sh.list, items, sizeof(items));
            sh.list_count = LV_MAX(n, 0);
            sh.list_ok = n >= 0;
            sh.list_kind = (enum sp_list)want;
            sh.list_done = true;
            pthread_mutex_unlock(&mu);
        }

        // One thumbnail per turn, so commands never wait long.
        char turl[256] = "";
        pthread_mutex_lock(&mu);
        if (sh.thumb_qn && sh.thumb_dn < (int)(sizeof(sh.thumb_done) / sizeof(sh.thumb_done[0]))) {
            copy(turl, sizeof(turl), sh.thumb_q[0]);
            memmove(sh.thumb_q[0], sh.thumb_q[1], (size_t)(--sh.thumb_qn) * sizeof(sh.thumb_q[0]));
        }
        pthread_mutex_unlock(&mu);
        if (turl[0]) {
            uint16_t *px = fetch_image(turl, SP_THUMB_PX);
            pthread_mutex_lock(&mu);
            copy(sh.thumb_done[sh.thumb_dn].url, sizeof(sh.thumb_done[0].url), turl);
            sh.thumb_done[sh.thumb_dn++].px = px;
            pthread_mutex_unlock(&mu);
        }

        pthread_mutex_lock(&mu);
        if (sh.kick) {
            sh.kick = false;
            next_poll = 0;
        }
        pthread_mutex_unlock(&mu);
        if ((int32_t)(now_ms() - next_poll) >= 0) {
            poll_player();
            pthread_mutex_lock(&mu);
            next_poll = now_ms() + (sh.visible ? POLL_SHOWN_MS : POLL_HIDDEN_MS);
            pthread_mutex_unlock(&mu);
        }

        // Sleep until the next poll or new work.
        pthread_mutex_lock(&mu);
        if (!sh.cmd_len && sh.list_req < 0 && !sh.thumb_qn && !sh.kick) {
            int32_t wait = (int32_t)(next_poll - now_ms());
            if (wait > 0) {
                struct timespec until;
                clock_gettime(CLOCK_REALTIME, &until);
                until.tv_sec += wait / 1000;
                until.tv_nsec += (long)(wait % 1000) * 1000000;
                if (until.tv_nsec >= 1000000000) {
                    until.tv_sec++;
                    until.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(&cv, &mu, &until);
            }
        }
        pthread_mutex_unlock(&mu);
    }
    return NULL;
}

// ── LVGL side ────────────────────────────────────────────────────────────────

static struct sp_state pub;
static bool started;
static uint32_t cmd_sent;
static uint16_t *cover_px;
static char cover_for[256];
static lv_image_dsc_t cover_dsc;
static sp_list_cb list_cb;

static struct thumb {
    char url[256];
    uint16_t *px;
    lv_image_dsc_t dsc;
    uint32_t used;
} thumbs[THUMB_CACHE];
static uint32_t thumb_clock;

static struct waiter {
    char url[256];
    sp_thumb_cb cb;
    void *arg;
} waiters[128];

static void image_dsc(lv_image_dsc_t *d, const uint16_t *px, int side) {
    memset(d, 0, sizeof(*d));
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = side;
    d->header.h = side;
    d->header.stride = side * 2;
    d->data = (const uint8_t *)px;
    d->data_size = (uint32_t)side * side * 2;
}

static void thumb_store(const char *url, uint16_t *px) {
    struct thumb *slot = &thumbs[0];
    for (int i = 0; i < THUMB_CACHE; i++) {
        if (!thumbs[i].url[0]) {
            slot = &thumbs[i];
            break;
        }
        if (thumbs[i].used < slot->used) {
            slot = &thumbs[i];
        }
    }
    if (slot->px) {
        lv_image_cache_drop(&slot->dsc);
        free(slot->px);
    }
    copy(slot->url, sizeof(slot->url), url);
    slot->px = px;
    slot->used = ++thumb_clock;
    image_dsc(&slot->dsc, px, SP_THUMB_PX);
    for (unsigned i = 0; i < sizeof(waiters) / sizeof(waiters[0]); i++) {
        if (waiters[i].cb && !strcmp(waiters[i].url, url)) {
            sp_thumb_cb cb = waiters[i].cb;
            waiters[i].cb = NULL;
            cb(url, px ? &slot->dsc : NULL, waiters[i].arg);
        }
    }
}

static void collect(lv_timer_t *t) {
    (void)t;
    bool list_ready = false;
    static struct sp_item items[SP_MAX_ITEMS];
    int count = 0;
    bool ok = false;
    enum sp_list kind = SP_LIST_PLAYLISTS;
    struct thumb_px done[16];
    int dn = 0;
    uint16_t *new_cover = NULL;
    char new_cover_url[256] = "";

    pthread_mutex_lock(&mu);
    if (sh.dirty) {
        sh.dirty = false;
        // Until the worker has run every command sent, keep the optimistic
        // control states; the rest (track, progress, device) is news.
        bool settled = sh.done_seq >= cmd_sent;
        struct sp_state s = sh.st;
        if (!settled && pub.auth == SP_LOGGED_IN) {
            s.playing = pub.playing;
            s.shuffle = pub.shuffle;
            s.repeat = pub.repeat;
            s.liked = pub.liked;
        }
        s.version = pub.version + 1;
        pub = s;
    }
    if (sh.list_done) {
        sh.list_done = false;
        list_ready = true;
        count = sh.list_count;
        ok = sh.list_ok;
        kind = sh.list_kind;
        memcpy(items, sh.list, sizeof(items));
    }
    dn = sh.thumb_dn;
    memcpy(done, sh.thumb_done, sizeof(done[0]) * (size_t)dn);
    sh.thumb_dn = 0;
    if (sh.cover) {
        new_cover = sh.cover;
        copy(new_cover_url, sizeof(new_cover_url), sh.cover_url);
        sh.cover = NULL;
    }
    pthread_mutex_unlock(&mu);

    if (new_cover) {
        if (cover_px) {
            lv_image_cache_drop(&cover_dsc);
            free(cover_px);
        }
        cover_px = new_cover;
        copy(cover_for, sizeof(cover_for), new_cover_url);
        image_dsc(&cover_dsc, cover_px, SP_COVER_PX);
        pub.version++;
    }
    for (int i = 0; i < dn; i++) {
        thumb_store(done[i].url, done[i].px);
    }
    if (list_ready && list_cb) {
        sp_list_cb cb = list_cb;
        list_cb = NULL;
        cb(kind, items, count, ok);
    }
}

static void enqueue(enum cmd_kind kind, int64_t arg, const char *str) {
    if (!started) {
        return;
    }
    pthread_mutex_lock(&mu);
    if (sh.cmd_len < CMDS) {
        struct cmd *c = &sh.cmds[(sh.cmd_head + sh.cmd_len++) % CMDS];
        c->kind = kind;
        c->arg = arg;
        copy(c->str, sizeof(c->str), str);
        cmd_sent++;
    }
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
    pub.version++;
}

void spotify_start(const char *id, const char *data_dir) {
    if (!id || !id[0]) {
        pub.auth = SP_NO_CLIENT;
        deck_log("spotify: no spotify_client_id in deck.conf");
        return;
    }
    copy(client_id, sizeof(client_id), id);
    mkdir(data_dir, 0700);
    snprintf(token_path, sizeof(token_path), "%s/spotify.json", data_dir);
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) != 0) {
        return;
    }
    pthread_detach(t);
    started = true;
    pub.auth = SP_LOGGED_OUT;
    lv_timer_create(collect, 100, NULL);
}

const struct sp_state *spotify_state(void) { return &pub; }

const lv_image_dsc_t *spotify_cover(void) {
    return cover_px && !strcmp(cover_for, pub.track.cover_url) ? &cover_dsc : NULL;
}

void spotify_visible(bool visible) {
    pthread_mutex_lock(&mu);
    sh.kick |= visible && !sh.visible;
    sh.visible = visible;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
}

void spotify_play_pause(void) {
    pub.playing = !pub.playing;
    pub.progress_tick = (uint32_t)lv_tick_get();
    enqueue(pub.playing ? C_PLAY : C_PAUSE, 0, NULL);
}

void spotify_next(void) { enqueue(C_NEXT, 0, NULL); }
void spotify_previous(void) { enqueue(C_PREV, 0, NULL); }

void spotify_seek(uint32_t ms) {
    pub.progress_ms = ms;
    pub.progress_tick = (uint32_t)lv_tick_get();
    enqueue(C_SEEK, ms, NULL);
}

void spotify_set_shuffle(bool on) {
    pub.shuffle = on;
    enqueue(C_SHUFFLE, on, NULL);
}

void spotify_set_repeat(enum sp_repeat r) {
    pub.repeat = r;
    enqueue(C_REPEAT, r, NULL);
}

void spotify_set_liked(bool liked) {
    pub.liked = liked;
    enqueue(C_LIKE, liked, pub.track.uri);
}

void spotify_play_uri(const char *uri) { enqueue(C_PLAY_URI, 0, uri); }
void spotify_transfer(const char *device_id) { enqueue(C_TRANSFER, 0, device_id); }

void spotify_logout(void) {
    pub.auth = SP_LOGGED_OUT;
    pub.has_player = false;
    enqueue(C_LOGOUT, 0, NULL);
}

void spotify_fetch(enum sp_list kind, sp_list_cb cb) {
    if (!started) {
        cb(kind, NULL, 0, false);
        return;
    }
    list_cb = cb;
    pthread_mutex_lock(&mu);
    sh.list_req = kind;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
}

void spotify_thumb(const char *url, sp_thumb_cb cb, void *arg) {
    if (!url || !url[0] || !started) {
        return;
    }
    for (int i = 0; i < THUMB_CACHE; i++) {
        if (thumbs[i].url[0] && !strcmp(thumbs[i].url, url)) {
            thumbs[i].used = ++thumb_clock;
            cb(url, thumbs[i].px ? &thumbs[i].dsc : NULL, arg);
            return;
        }
    }
    for (unsigned i = 0; i < sizeof(waiters) / sizeof(waiters[0]); i++) {
        if (!waiters[i].cb) {
            copy(waiters[i].url, sizeof(waiters[i].url), url);
            waiters[i].cb = cb;
            waiters[i].arg = arg;
            break;
        }
    }
    pthread_mutex_lock(&mu);
    bool queued = false;
    for (int i = 0; i < sh.thumb_qn && !queued; i++) {
        queued = !strcmp(sh.thumb_q[i], url);
    }
    if (!queued && sh.thumb_qn < THUMB_QUEUE) {
        copy(sh.thumb_q[sh.thumb_qn++], sizeof(sh.thumb_q[0]), url);
    }
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
}

void spotify_thumb_cancel(void *arg) {
    for (unsigned i = 0; i < sizeof(waiters) / sizeof(waiters[0]); i++) {
        if (waiters[i].cb && waiters[i].arg == arg) {
            waiters[i].cb = NULL;
        }
    }
}
