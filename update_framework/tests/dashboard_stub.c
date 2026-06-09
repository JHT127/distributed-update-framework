/*
 * dashboard_stub.c
 *
 * Headless implementation of the dashboard public API.
 * Provides the same data-layer logic as dashboard.c — mutex-protected
 * writes to g_stats and the event ring — but with NO OpenGL / GLUT
 * calls so the test binary links without a display.
 *
 * dashboard_start() is a no-op here; the window simply never opens.
 */

#include "../visualizer/dashboard.h"
#include <string.h>
#include <time.h>
#include <stdio.h>

/* ── globals required by dashboard.h ─────────────────────────────── */

ServerStats     g_stats;
pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;

/* per-slot client version (mirrors dashboard.c) */
static uint32_t g_slot_version[MAX_THREADS];

/* ── event ring (mirrors dashboard.c — same logic, no GL draw) ────── */

#define EV_MAX  32
#define EV_MSG  96

typedef enum { EV_INFO, EV_CONN, EV_UPDT, EV_OK, EV_DONE, EV_WARN } EvType;

typedef struct {
    EvType type;
    char   time_str[12];
    char   msg[EV_MSG];
} LogEvent;

static LogEvent        g_events[EV_MAX];
static int             g_ev_count = 0;
static int             g_ev_head  = 0;
static pthread_mutex_t g_ev_mutex = PTHREAD_MUTEX_INITIALIZER;

static EvType classify_event(const char *line) {
    if (strstr(line, "connected"))                        return EV_CONN;
    if (strstr(line, "sending") || strstr(line, "outdated")
                                || strstr(line, "Transfer")) return EV_UPDT;
    if (strstr(line, "up to date") || strstr(line, "DONE")
                                   || strstr(line, "complete")) return EV_DONE;
    if (strstr(line, "Auth accepted") || strstr(line, "OK"))    return EV_OK;
    if (strstr(line, "ERROR") || strstr(line, "WARN")
                              || strstr(line, "failed"))         return EV_WARN;
    return EV_INFO;
}

/* ── Public API ──────────────────────────────────────────────────── */

void dashboard_init(int pool_size, const char *log_path) {
    (void)log_path;
    pthread_mutex_lock(&g_stats_mutex);
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.pool_size  = pool_size;
    g_stats.start_time = time(NULL);
    pthread_mutex_unlock(&g_stats_mutex);

    memset(g_slot_version, 0, sizeof(g_slot_version));

    pthread_mutex_lock(&g_ev_mutex);
    memset(g_events, 0, sizeof(g_events));
    g_ev_count = 0;
    g_ev_head  = 0;
    pthread_mutex_unlock(&g_ev_mutex);
}

/* No-op: tests don't need the OpenGL window. */
void dashboard_start(void) { }

void dashboard_set_thread(int slot, ThreadState state, float progress,
                          const char *ip, uint32_t total, uint32_t sent) {
    if (slot < 0 || slot >= MAX_THREADS) return;
    pthread_mutex_lock(&g_stats_mutex);
    ThreadInfo *t  = &g_stats.threads[slot];
    t->state       = state;
    t->progress    = progress;
    t->bytes_total = total;
    t->bytes_sent  = sent;
    if (ip) strncpy(t->client_ip, ip, sizeof(t->client_ip) - 1);
    pthread_mutex_unlock(&g_stats_mutex);
}

void dashboard_set_thread_version(int slot, uint32_t client_version) {
    if (slot < 0 || slot >= MAX_THREADS) return;
    g_slot_version[slot] = client_version;
}

void dashboard_push_log(const char *line) {
    /* update log feed in g_stats */
    pthread_mutex_lock(&g_stats_mutex);
    for (int i = LOG_FEED_LINES - 1; i > 0; i--)
        memcpy(g_stats.log_feed[i], g_stats.log_feed[i-1], LOG_LINE_LEN);
    strncpy(g_stats.log_feed[0], line, LOG_LINE_LEN - 1);
    g_stats.log_feed[0][LOG_LINE_LEN - 1] = '\0';
    if (g_stats.log_feed_count < LOG_FEED_LINES)
        g_stats.log_feed_count++;
    pthread_mutex_unlock(&g_stats_mutex);

    /* update event ring */
    pthread_mutex_lock(&g_ev_mutex);
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    LogEvent *ev = &g_events[g_ev_head % EV_MAX];
    snprintf(ev->time_str, sizeof(ev->time_str), "%02d:%02d:%02d",
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    ev->type = classify_event(line);
    strncpy(ev->msg, line, EV_MSG - 1);
    ev->msg[EV_MSG - 1] = '\0';
    int mlen = (int)strlen(ev->msg);
    if (mlen > 0 && ev->msg[mlen-1] == '\n') ev->msg[mlen-1] = '\0';
    g_ev_head++;
    if (g_ev_count < EV_MAX) g_ev_count++;
    pthread_mutex_unlock(&g_ev_mutex);
}

void dashboard_on_connect(void) {
    pthread_mutex_lock(&g_stats_mutex);
    g_stats.active_connections++;
    g_stats.connections_this_second++;
    g_stats.total_served++;
    pthread_mutex_unlock(&g_stats_mutex);
}

void dashboard_on_disconnect(int update_was_sent) {
    pthread_mutex_lock(&g_stats_mutex);
    if (g_stats.active_connections > 0)
        g_stats.active_connections--;
    if (update_was_sent)
        g_stats.updates_sent++;
    else
        g_stats.up_to_date_count++;
    pthread_mutex_unlock(&g_stats_mutex);
}
