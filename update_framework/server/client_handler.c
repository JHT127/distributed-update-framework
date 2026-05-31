#include "client_handler.h"
#include "logger.h"
#include "version_store.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include "../common/protocol.h"

void handle_client(void *arg) {
    int fd = *(int *)arg;
    free(arg);

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    char client_ip[INET_ADDRSTRLEN];
    if (getpeername(fd, (struct sockaddr *)&addr, &len) == 0)
        inet_ntop(AF_INET, &addr.sin_addr, client_ip, sizeof(client_ip));
    else
        strncpy(client_ip, "unknown", sizeof(client_ip));

    int tid = (int)(pthread_self() % 100);

    logger_write(LOG_INFO, tid, client_ip, "[SERVER] Client connected");

    // receive version request
    VersionRequest req;
    int n = recv(fd, &req, sizeof(req), MSG_WAITALL);
    if (n != sizeof(req)) {
        logger_write(LOG_WARN, tid, client_ip, "[SERVER] Failed to receive VersionRequest");
        close(fd);
        return;
    }
    req.version = ntohl(req.version);
    logger_write(LOG_INFO, tid, client_ip, "[SERVER] Client version: %u", req.version);

    uint32_t latest = version_store_get_latest();

    // build and send response
    UpdateResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.update_available = (req.version < latest) ? 1 : 0;
    resp.file_size        = htonl(0);

    if (resp.update_available) {
        logger_write(LOG_INFO, tid, client_ip, "[SERVER] Client is outdated (latest: %u) — update available", latest);
    } else {
        logger_write(LOG_INFO, tid, client_ip, "[SERVER] Client is up to date");
    }

    send(fd, &resp, sizeof(resp), 0);
    logger_write(LOG_INFO, tid, client_ip, "[SERVER] Response sent");

    close(fd);
    logger_write(LOG_INFO, tid, client_ip, "[SERVER] Connection closed");
}