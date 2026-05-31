
#include <stdio.h>
#include <stdlib.h>
#include "../common/config.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        return 1;
    }

    const char *cfg = argv[1];

    char *server_ip   = parse_config(cfg, "SERVER_IP");
    char *server_port = parse_config(cfg, "SERVER_PORT");
    char *cur_version = parse_config(cfg, "CURRENT_VERSION");
    char *dl_dir      = parse_config(cfg, "DOWNLOAD_DIR");

    if (!server_ip || !server_port || !cur_version || !dl_dir) {
        fprintf(stderr, "[ERROR] Missing required config values.\n");
        return 1;
    }

    printf("[CONFIG] Server: %s:%s\n", server_ip, server_port);
    printf("[CONFIG] Current version: %s\n", cur_version);
    printf("[CONFIG] Download dir: %s\n", dl_dir);
    printf("Client ready. Exiting (no socket yet).\n");

    return 0;
}