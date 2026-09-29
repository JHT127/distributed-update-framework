
#pragma once
#include <pthread.h>

typedef enum {
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} LogLevel;

void logger_init(const char *filepath);
void logger_write(LogLevel level, int thread_id, const char *client_ip, const char *fmt, ...);
void logger_close(void);