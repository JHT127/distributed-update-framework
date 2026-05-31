#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <openssl/md5.h>
#include "../common/config.h"
#include "../common/protocol.h"

static char     g_server_ip[64];
static int      g_server_port;
static uint32_t g_current_version;
static char     g_download_dir[256];
static char     g_auth_token[TOKEN_LEN + 1];
static int      g_max_retries;
static int      g_retry_delay;

static int compute_md5(const char *filepath, char *out_hex) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;

    MD5_CTX ctx;
    MD5_Init(&ctx);
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        MD5_Update(&ctx, buf, n);
    fclose(f);

    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_Final(digest, &ctx);
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
        sprintf(out_hex + i * 2, "%02x", digest[i]);
    out_hex[32] = '\0';
    return 0;
}

// returns 1 on success (up to date or downloaded+verified), 0 on failure
static int try_update(void) {
    // check for partial download to resume
    char out_path[512];
    snprintf(out_path, sizeof(out_path), "%s/update_v2.pkg", g_download_dir);

    uint32_t resume_offset = 0;
    struct stat st;
    if (stat(out_path, &st) == 0) {
        resume_offset = (uint32_t)st.st_size;
        printf("[CLIENT] Partial file found — will resume from byte %u\n", resume_offset);
    }

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        fprintf(stderr, "[CLIENT] socket() failed\n");
        return 0;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(g_server_port);
    inet_pton(AF_INET, g_server_ip, &server_addr.sin_addr);

    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "[CLIENT] connect() failed — is the server running?\n");
        close(sock_fd);
        return 0;
    }
    printf("[CLIENT] Connected to %s:%d\n", g_server_ip, g_server_port);

    // send auth token
    AuthRequest auth_req;
    memset(&auth_req, 0, sizeof(auth_req));
    strncpy(auth_req.token, g_auth_token, TOKEN_LEN);
    send(sock_fd, &auth_req, sizeof(auth_req), 0);
    printf("[CLIENT] Sent auth token\n");

    // receive auth response
    AuthResponse auth_resp;
    int n = recv(sock_fd, &auth_resp, sizeof(auth_resp), MSG_WAITALL);
    if (n != (int)sizeof(auth_resp) || !auth_resp.accepted) {
        fprintf(stderr, "[CLIENT] Server rejected authentication — exiting\n");
        close(sock_fd);
        return 0;
    }
    printf("[CLIENT] Auth accepted by server\n");

    // send version request with resume offset
    VersionRequest req;
    req.version       = htonl(g_current_version);
    req.resume_offset = htonl(resume_offset);
    send(sock_fd, &req, sizeof(req), 0);
    printf("[CLIENT] Sent version: %u (resume offset: %u)\n", g_current_version, resume_offset);

    // receive response header
    UpdateResponse resp;
    n = recv(sock_fd, &resp, sizeof(resp), MSG_WAITALL);
    if (n != (int)sizeof(resp)) {
        fprintf(stderr, "[CLIENT] Failed to receive response from server\n");
        close(sock_fd);
        return 0;
    }
    resp.file_size = ntohl(resp.file_size);

    if (!resp.update_available) {
        printf("[CLIENT] Server says: already up to date\n");
        close(sock_fd);
        return 1;
    }

    printf("[CLIENT] Server says: update available — %u bytes incoming\n", resp.file_size);
    printf("[CLIENT] Expected MD5 (full file): %s\n", resp.checksum);

    mkdir(g_download_dir, 0755);

    // open in append mode if resuming, write mode if fresh start
    FILE *out = fopen(out_path, resume_offset > 0 ? "ab" : "wb");
    if (!out) {
        fprintf(stderr, "[CLIENT] Cannot open output file: %s\n", out_path);
        close(sock_fd);
        return 0;
    }

    // receive exactly file_size bytes
    char buf[4096];
    uint32_t remaining = resp.file_size;
    while (remaining > 0) {
        uint32_t to_read = remaining < sizeof(buf) ? remaining : sizeof(buf);
        int received = recv(sock_fd, buf, to_read, 0);
        if (received <= 0) {
            fprintf(stderr, "[CLIENT] Connection lost during download — %u bytes remaining\n", remaining);
            fclose(out);
            close(sock_fd);
            return 0;
        }
        fwrite(buf, 1, received, out);
        remaining -= received;
    }
    fclose(out);
    close(sock_fd);

    printf("[CLIENT] Download complete — saved to %s\n", out_path);

    // verify checksum against full file
    char actual_checksum[33];
    if (compute_md5(out_path, actual_checksum) < 0) {
        fprintf(stderr, "[CLIENT] Cannot compute MD5 of downloaded file\n");
        return 0;
    }
    printf("[CLIENT] Computed MD5: %s\n", actual_checksum);

    if (strcmp(resp.checksum, actual_checksum) == 0) {
        printf("[CLIENT] Checksum OK — file integrity verified\n");
        printf("[CLIENT] Simulating installation...\n");
        printf("[CLIENT] Update applied successfully\n");
        return 1;
    } else {
        fprintf(stderr, "[CLIENT] ERROR: Checksum mismatch — deleting corrupted file\n");
        remove(out_path);
        return 0;
    }
}

void CheckForUpdate(void) {
    for (int attempt = 1; attempt <= g_max_retries; attempt++) {
        if (attempt > 1)
            printf("[CLIENT] Retry %d/%d — waiting %d seconds...\n", attempt, g_max_retries, g_retry_delay);

        if (try_update())
            return;

        if (attempt < g_max_retries)
            sleep(g_retry_delay);
    }
    fprintf(stderr, "[CLIENT] All %d attempts failed\n", g_max_retries);
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
    strncpy(g_download_dir, tmp, sizeof(g_download_dir) - 1);

    tmp = parse_config(cfg, "AUTH_TOKEN");
    if (!tmp) { fprintf(stderr, "[CLIENT] Missing AUTH_TOKEN\n"); return 1; }
    strncpy(g_auth_token, tmp, TOKEN_LEN);

    tmp = parse_config(cfg, "MAX_RETRIES");
    if (!tmp) { fprintf(stderr, "[CLIENT] Missing MAX_RETRIES\n"); return 1; }
    g_max_retries = atoi(tmp);

    tmp = parse_config(cfg, "RETRY_DELAY_SECONDS");
    if (!tmp) { fprintf(stderr, "[CLIENT] Missing RETRY_DELAY_SECONDS\n"); return 1; }
    g_retry_delay = atoi(tmp);

    CheckForUpdate();
    return 0;
}