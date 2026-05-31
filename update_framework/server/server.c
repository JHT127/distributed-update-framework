
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
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

    logger_init(log_file);
    logger_write(LOG_INFO, 0, "--", "Server starting on port %s", port);

    // create TCP socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        logger_write(LOG_ERROR, 0, "--", "socket() failed");
        logger_close();
        return 1;
    }

    // allow immediate reuse of the port after restart
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        logger_write(LOG_ERROR, 0, "--", "setsockopt() failed");
        logger_close();
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(atoi(port));

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        logger_write(LOG_ERROR, 0, "--", "bind() failed on port %s", port);
        logger_close();
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        logger_write(LOG_ERROR, 0, "--", "listen() failed");
        logger_close();
        return 1;
    }

    logger_write(LOG_INFO, 0, "--", "Listening on port %s — waiting for one client...", port);

    // accept one client
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
        logger_write(LOG_ERROR, 0, "--", "accept() failed");
        close(server_fd);
        logger_close();
        return 1;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    logger_write(LOG_INFO, 0, client_ip, "Accepted connection");

    // read message from client
    char buf[256];
    memset(buf, 0, sizeof(buf));
    int n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        logger_write(LOG_INFO, 0, client_ip, "Received: %s", buf);
        send(client_fd, "ACK", 3, 0);
        logger_write(LOG_INFO, 0, client_ip, "Sent: ACK");
    } else {
        logger_write(LOG_WARN, 0, client_ip, "recv() returned nothing");
    }

    close(client_fd);
    logger_write(LOG_INFO, 0, client_ip, "Connection closed");

    close(server_fd);
    logger_close();
    return 0;
}