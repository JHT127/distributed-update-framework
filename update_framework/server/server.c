#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../common/config.h"
#include "logger.h"
#include "thread_pool.h"
#include "client_handler.h"
#include "version_store.h"

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

    logger_init(log_file);
    version_store_init((uint32_t)atoi(version));

    logger_write(LOG_INFO, 0, "--", "[SERVER] Starting on port %s, latest version %s, pool size %s", port, version, pool_sz);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        logger_write(LOG_ERROR, 0, "--", "[SERVER] socket() failed");
        logger_close();
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(atoi(port));

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        logger_write(LOG_ERROR, 0, "--", "[SERVER] bind() failed on port %s", port);
        logger_close();
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        logger_write(LOG_ERROR, 0, "--", "[SERVER] listen() failed");
        logger_close();
        return 1;
    }

    ThreadPool *pool = thread_pool_create(atoi(pool_sz), atoi(pool_sz) * 4);
    logger_write(LOG_INFO, 0, "--", "[SERVER] Thread pool ready (%s workers) — listening...", pool_sz);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (*client_fd < 0) {
            logger_write(LOG_WARN, 0, "--", "[SERVER] accept() failed, continuing...");
            free(client_fd);
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        logger_write(LOG_INFO, 0, client_ip, "[SERVER] Accepted connection — dispatching to pool");

        thread_pool_submit(pool, handle_client, client_fd);
    }

    thread_pool_destroy(pool);
    close(server_fd);
    logger_close();
    return 0;
}