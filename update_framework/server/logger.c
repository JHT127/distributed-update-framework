
#include "logger.h"
#include "../visualizer/dashboard.h"   /* push lines to the visual feed */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

static FILE           *g_log_file = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

void logger_init(const char *filepath) {
    g_log_file = fopen(filepath, "a");
    if (!g_log_file) {
        fprintf(stderr, "[ERROR] Cannot open log file: %s\n", filepath);
        exit(1);
    }
    logger_write(LOG_INFO, 0, "--", "Logger initialized");
}

void logger_write(LogLevel level, int thread_id, const char *client_ip, const char *fmt, ...) {
    /* timestamp */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    const char *lvl_str;
    if      (level == LOG_INFO)  lvl_str = "INFO ";
    else if (level == LOG_WARN)  lvl_str = "WARN ";
    else                         lvl_str = "ERROR";

    char msg[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    char full_line[640];
    snprintf(full_line, sizeof(full_line),
             "[%s] [%s] [TID:%d] [%s] %s",
             time_buf, lvl_str, thread_id, client_ip, msg);

    pthread_mutex_lock(&g_log_mutex);
    if (g_log_file) {
        fprintf(g_log_file, "%s\n", full_line);
        fflush(g_log_file);
    }
    printf("%s\n", full_line);
    pthread_mutex_unlock(&g_log_mutex);

    /* push to OpenGL dashboard feed (thread-safe inside) */
    dashboard_push_log(full_line);
}

void logger_close(void) {
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}
