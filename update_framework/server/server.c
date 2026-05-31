#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/config.h"
#include "logger.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        return 1;
    }

    const char *cfg = argv[1];
    char port[64], version[64], upd_file[256], log_file[256], pool_sz[64];
    char *tmp;

    tmp = parse_config(cfg, "PORT");
    if (!tmp) { fprintf(stderr, "[ERROR] Missing PORT\n"); return 1; }
    strncpy(port, tmp, sizeof(port) - 1);

    tmp = parse_config(cfg, "LATEST_VERSION");
    if (!tmp) { fprintf(stderr, "[ERROR] Missing LATEST_VERSION\n"); return 1; }
    strncpy(version, tmp, sizeof(version) - 1);

    tmp = parse_config(cfg, "UPDATE_FILE");
    if (!tmp) { fprintf(stderr, "[ERROR] Missing UPDATE_FILE\n"); return 1; }
    strncpy(upd_file, tmp, sizeof(upd_file) - 1);

    tmp = parse_config(cfg, "LOG_FILE");
    if (!tmp) { fprintf(stderr, "[ERROR] Missing LOG_FILE\n"); return 1; }
    strncpy(log_file, tmp, sizeof(log_file) - 1);

    tmp = parse_config(cfg, "THREAD_POOL_SIZE");
    if (!tmp) { fprintf(stderr, "[ERROR] Missing THREAD_POOL_SIZE\n"); return 1; }
    strncpy(pool_sz, tmp, sizeof(pool_sz) - 1);

    // init logger first — everything after this uses logger_write()
    logger_init(log_file);

    logger_write(LOG_INFO, 0, "--", "Server starting on port %s", port);
    logger_write(LOG_INFO, 0, "--", "Latest version: %s", version);
    logger_write(LOG_INFO, 0, "--", "Update file: %s", upd_file);
    logger_write(LOG_INFO, 0, "--", "Thread pool size: %s", pool_sz);
    logger_write(LOG_INFO, 0, "--", "Server ready. Exiting (no socket yet).");

    logger_close();
    return 0;
}