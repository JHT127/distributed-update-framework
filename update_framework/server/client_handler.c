#include "client_handler.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

void handle_client(void *arg) {
    int fd = *(int *)arg;
    free(arg);

    // get client IP for logging
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    char client_ip[INET_ADDRSTRLEN];
    if (getpeername(fd, (struct sockaddr *)&addr, &len) == 0)
        inet_ntop(AF_INET, &addr.sin_addr, client_ip, sizeof(client_ip));
    else
        strncpy(client_ip, "unknown", sizeof(client_ip));

    // get thread id for logging
    // pthread_self() returns a large opaque value — cast to int for a short readable id
    int tid = (int)(pthread_self() % 100);

    logger_write(LOG_INFO, tid, client_ip, "[SERVER] Handling client");

    char buf[256];
    memset(buf, 0, sizeof(buf));
    int n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        logger_write(LOG_INFO, tid, client_ip, "[SERVER] Received: %s", buf);
        send(fd, "ACK", 3, 0);
        logger_write(LOG_INFO, tid, client_ip, "[SERVER] Sent: ACK");
    } else {
        logger_write(LOG_WARN, tid, client_ip, "[SERVER] recv() returned nothing");
    }

    close(fd);
    logger_write(LOG_INFO, tid, client_ip, "[SERVER] Connection closed");
}