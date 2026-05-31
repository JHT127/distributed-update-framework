#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    printf("[CONFIG] Server: %s:%s\n", server_ip, server_port);
    printf("[CONFIG] Current version: %s\n", cur_version);
    printf("[CONFIG] Download dir: %s\n", dl_dir);
    printf("Client ready. Exiting (no socket yet).\n");

    return 0;
}