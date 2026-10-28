/*
 * Wifi through NetworkManager's nmcli, run on one worker thread: a scan takes
 * seconds and a connect up to half a minute, and LVGL isn't thread-safe. The
 * worker leaves results in `out` under the mutex; an LVGL timer picks them up
 * and calls the callbacks on the LVGL thread. Commands run without a shell
 * (fork/exec), so an SSID or password can't inject anything.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "deck.h"

#define STATUS_EVERY_S 5

enum job { JOB_NONE, JOB_SCAN, JOB_CONNECT };

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static bool started;

// Guarded by mu.
static enum job job;
static char job_ssid[33], job_pw[128];
static struct {
    bool scan_done, connect_done;
    struct wifi_net nets[WIFI_MAX_NETS];
    int count;
    bool ok;
    char message[160];
    struct wifi_status status;
} out;

// LVGL thread only.
static struct wifi_status shown;
static wifi_scan_cb scan_cb;
static wifi_connect_cb connect_cb;

// ── Running nmcli ────────────────────────────────────────────────────────────

// argv[0] with args, stdout+stderr into buf. Exit code, or -1.
static int run(char *const argv[], char *buf, size_t size) {
    int fds[2];
    if (pipe(fds) < 0) {
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[0]);
        close(fds[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    size_t n = 0;
    ssize_t r;
    while ((r = read(fds[0], buf + n, size - 1 - n)) > 0 || (r < 0 && errno == EINTR)) {
        if (r > 0) {
            n += (size_t)r;
        }
        if (n == size - 1) {
            char sink[256];
            while (read(fds[0], sink, sizeof(sink)) > 0) {
            }
            break;
        }
    }
    buf[n] = '\0';
    close(fds[0]);
    int st;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

// Next field of nmcli's terse output (-t): ':' separated, '\:' and '\\'
// escaped. Returns the start of the following field, or NULL at the line end.
static char *field(char *s, char *dst, size_t size) {
    size_t n = 0;
    for (; *s && *s != '\n'; s++) {
        if (*s == '\\' && s[1]) {
            s++;
        } else if (*s == ':') {
            s++;
            break;
        }
        if (n + 1 < size) {
            dst[n++] = *s;
        }
    }
    dst[n] = '\0';
    return (*s && s[-1] == ':') ? s : NULL;
}

static char *next_line(char *s) {
    char *nl = strchr(s, '\n');
    return nl ? nl + 1 : NULL;
}

// ── Jobs (worker thread) ─────────────────────────────────────────────────────

static void do_status(void) {
    static char buf[4096];
    struct wifi_status st = {.state = WIFI_OFF};
    char *argv[] = {"nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device", NULL};
    char dev[32] = "";

    if (run(argv, buf, sizeof(buf)) == 0) {
        for (char *l = buf; l && *l; l = next_line(l)) {
            char name[32], type[16], state[32], conn[64];
            char *p = field(l, name, sizeof(name));
            p = p ? field(p, type, sizeof(type)) : NULL;
            p = p ? field(p, state, sizeof(state)) : NULL;
            if (!p || strcmp(type, "wifi") != 0) {
                continue;
            }
            field(p, conn, sizeof(conn));
            snprintf(dev, sizeof(dev), "%s", name);
            if (!strcmp(state, "connected")) {
                st.state = WIFI_CONNECTED;
            } else if (!strncmp(state, "connecting", 10)) {
                st.state = WIFI_CONNECTING;
            } else if (!strcmp(state, "disconnected")) {
                st.state = WIFI_DISCONNECTED;
            }
            if (st.state >= WIFI_CONNECTING) {
                snprintf(st.ssid, sizeof(st.ssid), "%.32s", conn);
            }
            break;
        }
    }
    if (st.state == WIFI_CONNECTED && dev[0]) {
        char *ip[] = {"nmcli", "-g", "IP4.ADDRESS", "device", "show", dev, NULL};
        if (run(ip, buf, sizeof(buf)) == 0) {
            buf[strcspn(buf, "/\n")] = '\0';
            snprintf(st.ip, sizeof(st.ip), "%.39s", buf);
        }
    }
    pthread_mutex_lock(&mu);
    out.status = st;
    pthread_mutex_unlock(&mu);
}

static void do_scan(void) {
    static char buf[16384];
    struct wifi_net nets[WIFI_MAX_NETS];
    int count = 0;
    char *argv[] = {"nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi", "list",
                    "--rescan", "yes", NULL};

    if (run(argv, buf, sizeof(buf)) == 0) {
        for (char *l = buf; l && *l && count < WIFI_MAX_NETS; l = next_line(l)) {
            char inuse[4], ssid[33], signal[8], security[48];
            char *p = field(l, inuse, sizeof(inuse));
            p = p ? field(p, ssid, sizeof(ssid)) : NULL;
            p = p ? field(p, signal, sizeof(signal)) : NULL;
            if (!p || !ssid[0]) {
                continue; // hidden network
            }
            field(p, security, sizeof(security));
            bool dup = false; // one row per name: the access points of a mesh
            for (int i = 0; i < count && !dup; i++) {
                dup = !strcmp(nets[i].ssid, ssid);
            }
            if (dup) {
                continue;
            }
            struct wifi_net *n = &nets[count++];
            snprintf(n->ssid, sizeof(n->ssid), "%s", ssid);
            n->signal = (uint8_t)atoi(signal);
            n->secure = security[0] && strcmp(security, "--") != 0;
            n->active = inuse[0] == '*';
        }
    } else {
        deck_log("wifi: scan failed: %s", buf);
    }
    pthread_mutex_lock(&mu);
    memcpy(out.nets, nets, sizeof(nets));
    out.count = count;
    out.scan_done = true;
    pthread_mutex_unlock(&mu);
}

static void do_connect(const char *ssid, const char *pw) {
    static char buf[1024];
    char *with_pw[] = {"nmcli", "--wait", "40", "device", "wifi", "connect", (char *)ssid,
                       "password", (char *)pw, NULL};
    char *open_net[] = {"nmcli", "--wait", "40", "device", "wifi", "connect", (char *)ssid, NULL};
    int rc = run(pw[0] ? with_pw : open_net, buf, sizeof(buf));
    buf[strcspn(buf, "\n")] = '\0';
    deck_log("wifi: connect \"%s\": %s (%d)", ssid, buf, rc);

    pthread_mutex_lock(&mu);
    out.ok = rc == 0;
    // nmcli: "Error: Connection activation failed: Secrets were required, but not provided"
    if (rc == 0) {
        snprintf(out.message, sizeof(out.message), "CONNECTED");
    } else if (strstr(buf, "Secrets were required") || strstr(buf, "802-11-wireless-security")) {
        snprintf(out.message, sizeof(out.message), "WRONG PASSWORD?");
    } else if (strstr(buf, "No network with SSID")) {
        snprintf(out.message, sizeof(out.message), "NETWORK NOT FOUND");
    } else {
        snprintf(out.message, sizeof(out.message), "COULDN'T CONNECT");
    }
    out.connect_done = true;
    pthread_mutex_unlock(&mu);
}

static void *worker(void *arg) {
    (void)arg;
    static char buf[512];
    // Raspberry Pi OS keeps wifi soft-blocked until a country is set; the
    // country itself comes from the kernel command line (cmdline.txt).
    char *unblock[] = {"rfkill", "unblock", "wifi", NULL};
    char *radio[] = {"nmcli", "radio", "wifi", "on", NULL};
    run(unblock, buf, sizeof(buf));
    run(radio, buf, sizeof(buf));

    for (;;) {
        do_status();

        pthread_mutex_lock(&mu);
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += STATUS_EVERY_S;
        while (job == JOB_NONE) {
            if (pthread_cond_timedwait(&cv, &mu, &until) == ETIMEDOUT) {
                break;
            }
        }
        enum job j = job;
        char ssid[33], pw[128];
        memcpy(ssid, job_ssid, sizeof(ssid));
        memcpy(pw, job_pw, sizeof(pw));
        memset(job_pw, 0, sizeof(job_pw));
        job = JOB_NONE;
        pthread_mutex_unlock(&mu);

        if (j == JOB_SCAN) {
            do_scan();
        } else if (j == JOB_CONNECT) {
            do_connect(ssid, pw);
        }
        memset(pw, 0, sizeof(pw));
    }
    return NULL;
}

// ── LVGL side ────────────────────────────────────────────────────────────────

static void collect(lv_timer_t *t) {
    (void)t;
    bool scanned = false, connected = false, ok = false;
    struct wifi_net nets[WIFI_MAX_NETS];
    int count = 0;
    char message[160];

    pthread_mutex_lock(&mu);
    shown = out.status;
    if (out.scan_done) {
        out.scan_done = false;
        scanned = true;
        memcpy(nets, out.nets, sizeof(nets));
        count = out.count;
    }
    if (out.connect_done) {
        out.connect_done = false;
        connected = true;
        ok = out.ok;
        memcpy(message, out.message, sizeof(message));
    }
    pthread_mutex_unlock(&mu);

    if (scanned && scan_cb) {
        wifi_scan_cb cb = scan_cb;
        scan_cb = NULL;
        cb(nets, count);
    }
    if (connected && connect_cb) {
        wifi_connect_cb cb = connect_cb;
        connect_cb = NULL;
        cb(ok, message);
    }
}

void wifi_start(void) {
    pthread_t t;
    if (started || pthread_create(&t, NULL, worker, NULL) != 0) {
        return;
    }
    pthread_detach(t);
    started = true;
    lv_timer_create(collect, 100, NULL);
}

const struct wifi_status *wifi_status(void) { return &shown; }

void wifi_scan(wifi_scan_cb cb) {
    if (!started) {
        cb(NULL, 0);
        return;
    }
    scan_cb = cb;
    pthread_mutex_lock(&mu);
    job = JOB_SCAN;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
}

void wifi_connect(const char *ssid, const char *password, wifi_connect_cb cb) {
    if (!started) {
        cb(false, "NO WIFI HERE");
        return;
    }
    connect_cb = cb;
    pthread_mutex_lock(&mu);
    job = JOB_CONNECT;
    snprintf(job_ssid, sizeof(job_ssid), "%s", ssid);
    snprintf(job_pw, sizeof(job_pw), "%s", password);
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
}
