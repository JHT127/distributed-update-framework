#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../common/config.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        return 1;
    }

    const char *cfg = argv[1];
    char server_ip[64], server_port[64], cur_version[64], dl_dir[256];
    char *tmp;

    tmp = parse_config(cfg, "SERVER_IP");
    if (!tmp) { fprintf(stderr, "[ERROR] Missing SERVER_IP\n"); return 1; }
    strncpy(server_ip, tmp, sizeof(server_ip) - 1);

    tmp = parse_config(cfg, "SERVER_PORT");
    if (!tmp) { fprintf(stderr, "[ERROR] Missing SERVER_PORT\n"); return 1; }
    strncpy(server_port, tmp, sizeof(server_port) - 1);

    tmp = parse_config(cfg, "CURRENT_VERSION");
    if (!tmp) { fprintf(stderr, "[ERROR] Missing CURRENT_VERSION\n"); return 1; }
    strncpy(cur_version, tmp, sizeof(cur_version) - 1);

    tmp = parse_config(cfg, "DOWNLOAD_DIR");
    if (!tmp) { fprintf(stderr, "[ERROR] Missing DOWNLOAD_DIR\n"); return 1; }
    strncpy(dl_dir, tmp, sizeof(dl_dir) - 1);

    // create TCP socket
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        fprintf(stderr, "[ERROR] socket() failed\n");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(atoi(server_port));

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "[ERROR] Invalid server IP: %s\n", server_ip);
        return 1;
    }

    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "[ERROR] connect() failed — is the server running?\n");
        return 1;
    }

    printf("Connected to %s:%s\n", server_ip, server_port);

    // send HELLO
    send(sock_fd, "HELLO", 5, 0);

    // read response
    char buf[256];
    memset(buf, 0, sizeof(buf));
    int n = recv(sock_fd, buf, sizeof(buf) - 1, 0);
    if (n > 0)
        printf("Received: %s\n", buf);
    else
        fprintf(stderr, "[ERROR] No response from server\n");

    close(sock_fd);
    return 0;
}