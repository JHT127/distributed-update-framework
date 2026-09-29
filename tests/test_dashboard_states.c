/*
 * test_dashboard_states.c
 *
 * Validates the dashboard state machine and data structures
 * WITHOUT opening an OpenGL window. Each test exercises the
 * public dashboard API and checks that g_stats / the event
 * ring reflect the expected values.
 *
 * Build:  see Makefile target  test-dashboard
 * Run:    ./bin/test_dashboard_states
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "../visualizer/dashboard.h"

/* ── tiny test helpers ──────────────────────────────────────────── */

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg)                                         \
    do {                                                         \
        if (cond) {                                              \
            printf("  [PASS] %s\n", msg);                        \
            g_passed++;                                          \
        } else {                                                 \
            printf("  [FAIL] %s  (line %d)\n", msg, __LINE__);  \
            g_failed++;                                          \
        }                                                        \
    } while(0)

#define SECTION(name) printf("\n=== %s ===\n", name)

/* ── TEST 1: init zeros everything ─────────────────────────────── */

static void test_init(void) {
    SECTION("TC-DASH-01: dashboard_init resets all counters");

    dashboard_init(4, "/tmp/test.log");

    pthread_mutex_lock(&g_stats_mutex);

    CHECK(g_stats.active_connections  == 0, "active_connections  starts at 0");
    CHECK(g_stats.updates_sent        == 0, "updates_sent        starts at 0");
    CHECK(g_stats.up_to_date_count    == 0, "up_to_date_count    starts at 0");
    CHECK(g_stats.auth_failures       == 0, "auth_failures       starts at 0");
    CHECK(g_stats.pool_size           == 4, "pool_size           set to 4");
    CHECK(g_stats.log_feed_count      == 0, "log_feed_count      starts at 0");

    /* All thread slots should be IDLE */
    int all_idle = 1;
    for (int i = 0; i < 4; i++)
        if (g_stats.threads[i].state != THREAD_IDLE) { all_idle = 0; break; }
    CHECK(all_idle, "all thread slots start as THREAD_IDLE");

    pthread_mutex_unlock(&g_stats_mutex);
}

/* ── TEST 2: connect / disconnect accounting ────────────────────── */

static void test_connect_disconnect(void) {
    SECTION("TC-DASH-02: connect/disconnect stat tracking");

    dashboard_init(4, NULL);

    dashboard_on_connect();
    dashboard_on_connect();
    dashboard_on_connect();

    pthread_mutex_lock(&g_stats_mutex);
    CHECK(g_stats.active_connections == 3, "active_connections == 3 after 3 connects");
    CHECK(g_stats.total_served       == 3, "total_served       == 3 after 3 connects");
    pthread_mutex_unlock(&g_stats_mutex);

    /* One client gets an update, two are already up-to-date */
    dashboard_on_disconnect(DISCONNECT_REASON_UPDATE_SENT);
    dashboard_on_disconnect(DISCONNECT_REASON_UP_TO_DATE);
    dashboard_on_disconnect(DISCONNECT_REASON_UP_TO_DATE);

    pthread_mutex_lock(&g_stats_mutex);
    CHECK(g_stats.active_connections == 0, "active_connections == 0 after all disconnect");
    CHECK(g_stats.updates_sent       == 1, "updates_sent       == 1");
    CHECK(g_stats.up_to_date_count   == 2, "up_to_date_count   == 2");
    pthread_mutex_unlock(&g_stats_mutex);
}

/* ── TEST 3: active_connections never goes below 0 ─────────────── */

static void test_no_negative_connections(void) {
    SECTION("TC-DASH-03: active_connections never goes negative");

    dashboard_init(4, NULL);

    /* Disconnect without any prior connect */
    dashboard_on_disconnect(DISCONNECT_REASON_ERROR);

    pthread_mutex_lock(&g_stats_mutex);
    CHECK(g_stats.active_connections >= 0,
          "active_connections >= 0 even on spurious disconnect");
    pthread_mutex_unlock(&g_stats_mutex);
}

/* ── TEST 4: thread state transitions ──────────────────────────── */

static void test_thread_states(void) {
    SECTION("TC-DASH-04: thread state transitions per slot");

    dashboard_init(4, NULL);

    /* Simulate a full client lifecycle on slot 0 */
    dashboard_set_thread(0, THREAD_AUTH,          0.0f, "10.0.0.1", 0,    0);
    pthread_mutex_lock(&g_stats_mutex);
    CHECK(g_stats.threads[0].state == THREAD_AUTH, "slot 0: AUTH set");
    CHECK(strcmp(g_stats.threads[0].client_ip, "10.0.0.1") == 0, "slot 0: IP stored");
    pthread_mutex_unlock(&g_stats_mutex);

    dashboard_set_thread(0, THREAD_VERSION_CHECK, 0.0f, "10.0.0.1", 0,    0);
    pthread_mutex_lock(&g_stats_mutex);
    CHECK(g_stats.threads[0].state == THREAD_VERSION_CHECK, "slot 0: VERSION_CHECK set");
    pthread_mutex_unlock(&g_stats_mutex);

    dashboard_set_thread(0, THREAD_TRANSFERRING,  0.5f, "10.0.0.1", 2048, 1024);
    pthread_mutex_lock(&g_stats_mutex);
    CHECK(g_stats.threads[0].state       == THREAD_TRANSFERRING, "slot 0: TRANSFERRING set");
    CHECK(g_stats.threads[0].progress    == 0.5f,                "slot 0: progress == 0.5");
    CHECK(g_stats.threads[0].bytes_total == 2048,                "slot 0: bytes_total set");
    CHECK(g_stats.threads[0].bytes_sent  == 1024,                "slot 0: bytes_sent set");
    pthread_mutex_unlock(&g_stats_mutex);

    dashboard_set_thread(0, THREAD_DONE,          1.0f, "10.0.0.1", 2048, 2048);
    pthread_mutex_lock(&g_stats_mutex);
    CHECK(g_stats.threads[0].state    == THREAD_DONE, "slot 0: DONE set");
    CHECK(g_stats.threads[0].progress == 1.0f,        "slot 0: progress == 1.0 at DONE");
    pthread_mutex_unlock(&g_stats_mutex);

    dashboard_set_thread(0, THREAD_IDLE,          0.0f, "",          0,    0);
    pthread_mutex_lock(&g_stats_mutex);
    CHECK(g_stats.threads[0].state == THREAD_IDLE, "slot 0: back to IDLE");
    pthread_mutex_unlock(&g_stats_mutex);
}

/* ── TEST 5: slot boundary guard ───────────────────────────────── */

static void test_slot_boundary(void) {
    SECTION("TC-DASH-05: out-of-range slot is silently ignored");

    dashboard_init(4, NULL);

    /* Should not crash or corrupt memory */
    dashboard_set_thread(-1, THREAD_TRANSFERRING, 0.5f, "1.2.3.4", 100, 50);
    dashboard_set_thread(MAX_THREADS, THREAD_AUTH, 0.0f, "1.2.3.4", 0, 0);
    dashboard_set_thread(MAX_THREADS + 999, THREAD_DONE, 1.0f, "1.2.3.4", 0, 0);

    /* All slots still IDLE */
    pthread_mutex_lock(&g_stats_mutex);
    int all_idle = 1;
    for (int i = 0; i < 4; i++)
        if (g_stats.threads[i].state != THREAD_IDLE) { all_idle = 0; break; }
    CHECK(all_idle, "all slots still IDLE after invalid slot calls");
    pthread_mutex_unlock(&g_stats_mutex);
}

/* ── TEST 6: multi-slot independence ───────────────────────────── */

static void test_multi_slot(void) {
    SECTION("TC-DASH-06: multiple concurrent slots are independent");

    dashboard_init(4, NULL);

    dashboard_set_thread(0, THREAD_AUTH,         0.0f, "192.168.1.10", 0,    0);
    dashboard_set_thread(1, THREAD_TRANSFERRING, 0.3f, "192.168.1.11", 4096, 1228);
    dashboard_set_thread(2, THREAD_DONE,         1.0f, "192.168.1.12", 4096, 4096);
    dashboard_set_thread(3, THREAD_IDLE,         0.0f, "",             0,    0);

    pthread_mutex_lock(&g_stats_mutex);
    CHECK(g_stats.threads[0].state == THREAD_AUTH,         "slot 0: AUTH");
    CHECK(g_stats.threads[1].state == THREAD_TRANSFERRING, "slot 1: TRANSFERRING");
    CHECK(g_stats.threads[2].state == THREAD_DONE,         "slot 2: DONE");
    CHECK(g_stats.threads[3].state == THREAD_IDLE,         "slot 3: IDLE");
    /* Verify IPs did not bleed across slots */
    CHECK(strcmp(g_stats.threads[0].client_ip, "192.168.1.10") == 0, "slot 0 IP correct");
    CHECK(strcmp(g_stats.threads[1].client_ip, "192.168.1.11") == 0, "slot 1 IP correct");
    CHECK(strcmp(g_stats.threads[2].client_ip, "192.168.1.12") == 0, "slot 2 IP correct");
    pthread_mutex_unlock(&g_stats_mutex);
}

/* ── TEST 7: log feed push ordering ────────────────────────────── */

static void test_log_feed(void) {
    SECTION("TC-DASH-07: log feed newest-first ordering");

    dashboard_init(4, NULL);

    dashboard_push_log("first line\n");
    dashboard_push_log("second line\n");
    dashboard_push_log("third line\n");

    pthread_mutex_lock(&g_stats_mutex);
    /* index 0 should be the most recently pushed entry */
    CHECK(strstr(g_stats.log_feed[0], "third")  != NULL, "log_feed[0] == most recent line");
    CHECK(strstr(g_stats.log_feed[1], "second") != NULL, "log_feed[1] == second line");
    CHECK(strstr(g_stats.log_feed[2], "first")  != NULL, "log_feed[2] == first line");
    CHECK(g_stats.log_feed_count == 3,                   "log_feed_count == 3");
    pthread_mutex_unlock(&g_stats_mutex);
}

/* ── TEST 8: log feed capacity cap ─────────────────────────────── */

static void test_log_feed_cap(void) {
    SECTION("TC-DASH-08: log feed count never exceeds LOG_FEED_LINES");

    dashboard_init(4, NULL);

    for (int i = 0; i < LOG_FEED_LINES + 5; i++) {
        char line[64];
        snprintf(line, sizeof(line), "line %d\n", i);
        dashboard_push_log(line);
    }

    pthread_mutex_lock(&g_stats_mutex);
    CHECK(g_stats.log_feed_count == LOG_FEED_LINES,
          "log_feed_count capped at LOG_FEED_LINES");
    pthread_mutex_unlock(&g_stats_mutex);
}

/* ── TEST 9: transfer progress clamping ────────────────────────── */

static void test_progress_range(void) {
    SECTION("TC-DASH-09: progress value stored as-is (caller responsibility)");

    dashboard_init(4, NULL);

    dashboard_set_thread(0, THREAD_TRANSFERRING, 0.0f, "1.2.3.4", 1000, 0);
    pthread_mutex_lock(&g_stats_mutex);
    CHECK(g_stats.threads[0].progress == 0.0f, "progress 0.0 stored");
    pthread_mutex_unlock(&g_stats_mutex);

    dashboard_set_thread(0, THREAD_TRANSFERRING, 0.75f, "1.2.3.4", 1000, 750);
    pthread_mutex_lock(&g_stats_mutex);
    CHECK(g_stats.threads[0].progress == 0.75f, "progress 0.75 stored");
    pthread_mutex_unlock(&g_stats_mutex);

    dashboard_set_thread(0, THREAD_DONE, 1.0f, "1.2.3.4", 1000, 1000);
    pthread_mutex_lock(&g_stats_mutex);
    CHECK(g_stats.threads[0].progress == 1.0f, "progress 1.0 stored at DONE");
    pthread_mutex_unlock(&g_stats_mutex);
}

/* ── TEST 10: auth failure counter ─────────────────────────────── */

static void test_auth_failures(void) {
    SECTION("TC-DASH-10: auth_failures counter increments correctly");

    dashboard_init(4, NULL);

    dashboard_on_connect();
    dashboard_on_disconnect(DISCONNECT_REASON_AUTH_REJECTED);
    dashboard_on_connect();
    dashboard_on_disconnect(DISCONNECT_REASON_AUTH_REJECTED);
    dashboard_on_connect();
    dashboard_on_disconnect(DISCONNECT_REASON_AUTH_REJECTED);

    pthread_mutex_lock(&g_stats_mutex);
    CHECK(g_stats.auth_failures == 3, "auth_failures == 3 after 3 rejected connections");
    CHECK(g_stats.active_connections == 0, "rejected connections are closed");
    pthread_mutex_unlock(&g_stats_mutex);
}

/* ── TEST 11: pool size propagation ────────────────────────────── */

static void test_pool_sizes(void) {
    SECTION("TC-DASH-11: pool_size stored for various sizes");

    int sizes[] = {1, 2, 4, 8, 16};
    for (int s = 0; s < 5; s++) {
        dashboard_init(sizes[s], NULL);
        pthread_mutex_lock(&g_stats_mutex);
        int ok = (g_stats.pool_size == sizes[s]);
        pthread_mutex_unlock(&g_stats_mutex);
        char msg[64];
        snprintf(msg, sizeof(msg), "pool_size stored correctly for size=%d", sizes[s]);
        CHECK(ok, msg);
    }
}

/* ── TEST 12: timeline connections_this_second ──────────────────── */

static void test_timeline_counter(void) {
    SECTION("TC-DASH-12: connections_this_second increments via on_connect");

    dashboard_init(4, NULL);

    dashboard_on_connect();
    dashboard_on_connect();
    dashboard_on_connect();
    dashboard_on_connect();

    pthread_mutex_lock(&g_stats_mutex);
    CHECK(g_stats.connections_this_second == 4,
          "connections_this_second == 4 after 4 on_connect calls");
    pthread_mutex_unlock(&g_stats_mutex);
}

/* ── main ───────────────────────────────────────────────────────── */

int main(void) {
    printf("=================================================\n");
    printf("  Dashboard Visualization Test Suite\n");
    printf("=================================================\n");

    test_init();
    test_connect_disconnect();
    test_no_negative_connections();
    test_thread_states();
    test_slot_boundary();
    test_multi_slot();
    test_log_feed();
    test_log_feed_cap();
    test_progress_range();
    test_auth_failures();
    test_pool_sizes();
    test_timeline_counter();

    printf("\n=================================================\n");
    printf("  Results: %d passed, %d failed\n", g_passed, g_failed);
    printf("=================================================\n");

    return (g_failed == 0) ? 0 : 1;
}
