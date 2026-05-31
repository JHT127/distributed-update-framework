#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../common/config.h"
#include "../common/protocol.h"

static char g_server_ip[64];
static int  g_server_port;
static uint32_t g_current_version;

void CheckForUpdate(void) {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        fprintf(stderr, "[CLIENT] socket() failed\n");
        return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(g_server_port);
    inet_pton(AF_INET, g_server_ip, &server_addr.sin_addr);

    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "[CLIENT] connect() failed — is the server running?\n");
        close(sock_fd);
        return;
    }
    printf("[CLIENT] Connected to %s:%d\n", g_server_ip, g_server_port);

    // send version request
    VersionRequest req;
    req.version = htonl(g_current_version);
    send(sock_fd, &req, sizeof(req), 0);
    printf("[CLIENT] Sent version: %u\n", g_current_version);

    // receive response
    UpdateResponse resp;
    int n = recv(sock_fd, &resp, sizeof(resp), MSG_WAITALL);
    if (n != (int)sizeof(resp)) {
        fprintf(stderr, "[CLIENT] Failed to receive response from server\n");
        close(sock_fd);
        return;
    }
    resp.file_size = ntohl(resp.file_size);

    if (resp.update_available) {
        printf("[CLIENT] Server says: update available\n");
    } else {
        printf("[CLIENT] Server says: already up to date\n");
    }

    close(sock_fd);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        return 1;
    }

    const char *cfg = argv[1];
    char *tmp;

    tmp = parse_config(cfg, "SERVER_IP");
    if (!tmp) { fprintf(stderr, "[CLIENT] Missing SERVER_IP\n"); return 1; }
    strncpy(g_server_ip, tmp, sizeof(g_server_ip) - 1);

    tmp = parse_config(cfg, "SERVER_PORT");
    if (!tmp) { fprintf(stderr, "[CLIENT] Missing SERVER_PORT\n"); return 1; }
    g_server_port = atoi(tmp);

    tmp = parse_config(cfg, "CURRENT_VERSION");
    if (!tmp) { fprintf(stderr, "[CLIENT] Missing CURRENT_VERSION\n"); return 1; }
    g_current_version = (uint32_t)atoi(tmp);

    tmp = parse_config(cfg, "DOWNLOAD_DIR");
    if (!tmp) { fprintf(stderr, "[CLIENT] Missing DOWNLOAD_DIR\n"); return 1; }
    // stored in config but used in step 6

    CheckForUpdate();
    return 0;
}