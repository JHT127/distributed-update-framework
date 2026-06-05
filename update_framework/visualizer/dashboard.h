#pragma once
#include <pthread.h>
#include <stdint.h>

/* ---------------------------------------------------------------
 * dashboard.h
 * Shared state between the server and the OpenGL dashboard thread.
 *
 * The server writes to g_stats under g_stats_mutex.
 * The dashboard thread reads g_stats under the same mutex to draw.
 * --------------------------------------------------------------- */

#define MAX_THREADS     16      /* match THREAD_POOL_SIZE upper bound */
#define TIMELINE_LEN    60      /* seconds of connection history shown  */
#define LOG_FEED_LINES  10      /* lines shown in the live log panel    */
#define LOG_LINE_LEN    128

/* Per-thread state visible to the dashboard */
typedef enum {
    THREAD_IDLE,
    THREAD_AUTH,
    THREAD_VERSION_CHECK,
    THREAD_TRANSFERRING,
    THREAD_DONE
} ThreadState;

typedef struct {
    ThreadState state;
    float       progress;       /* 0.0 – 1.0 during transfer          */
    char        client_ip[32];
    uint32_t    bytes_total;
    uint32_t    bytes_sent;
} ThreadInfo;

/* Global stats struct — written by server, read by dashboard */
typedef struct {
    int        active_connections;
    int        total_served;
    int        updates_sent;
    int        up_to_date_count;
    int        auth_failures;
    int        pool_size;

    ThreadInfo threads[MAX_THREADS];   /* index = worker slot 0..pool_size-1 */

    /* rolling timeline: connections_per_second[i] = count at second i     */
    int        timeline[TIMELINE_LEN];
    int        timeline_head;          /* next slot to write                */

    /* live log feed — newest entry at index 0                             */
    char       log_feed[LOG_FEED_LINES][LOG_LINE_LEN];
    int        log_feed_count;

    /* server start time for uptime display */
    time_t     start_time;
} ServerStats;

extern ServerStats      g_stats;
extern pthread_mutex_t  g_stats_mutex;

/* -----------------------------------------------------------
 * Call once from server.c after config is loaded.
 * pool_size  – number of workers in the thread pool
 * log_path   – path to the log file for the tail feed
 * ----------------------------------------------------------- */
void dashboard_init(int pool_size, const char *log_path);

/* Spawns the OpenGL window in its own thread. Call after dashboard_init(). */
void dashboard_start(void);

/* Called from handle_client() to keep per-thread progress updated.
 * slot  – index 0..pool_size-1 (use thread_pool_get_slot() or tid % pool_size)
 */
void dashboard_set_thread(int slot, ThreadState state, float progress,
                          const char *ip, uint32_t total, uint32_t sent);

/* Called from logger_write() hook to push a line into the log feed. */
void dashboard_push_log(const char *line);

/* Called when a connection is accepted — increments active + timeline. */
void dashboard_on_connect(void);

/* Called when a connection closes. */
void dashboard_on_disconnect(int update_was_sent);
