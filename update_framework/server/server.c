
#include <stdio.h>
#include <stdlib.h>
#include "../common/config.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        return 1;
    }

    const char *cfg = argv[1];

    char *port     = parse_config(cfg, "PORT");
    char *version  = parse_config(cfg, "LATEST_VERSION");
    char *upd_file = parse_config(cfg, "UPDATE_FILE");
    char *log_file = parse_config(cfg, "LOG_FILE");
    char *pool_sz  = parse_config(cfg, "THREAD_POOL_SIZE");

    if (!port || !version || !upd_file || !log_file || !pool_sz) {
        fprintf(stderr, "[ERROR] Missing required config values.\n");
        return 1;
    }

    printf("[CONFIG] Port: %s\n", port);
    printf("[CONFIG] Latest version: %s\n", version);
    printf("[CONFIG] Update file: %s\n", upd_file);
    printf("[CONFIG] Log file: %s\n", log_file);
    printf("[CONFIG] Thread pool size: %s\n", pool_sz);
    printf("Server ready. Exiting (no socket yet).\n");

    return 0;
}