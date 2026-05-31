#include "client_handler.h"
#include "logger.h"
#include "version_store.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <pthread.h>
#include <openssl/md5.h>
#include "../common/protocol.h"

// compute MD5 of a file, write 32-char hex string into out_hex (must be 33+ bytes)
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

    UpdateResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.update_available = (req.version < latest) ? 1 : 0;

    if (!resp.update_available) {
        resp.file_size = htonl(0);
        send(fd, &resp, sizeof(resp), 0);
        logger_write(LOG_INFO, tid, client_ip, "[SERVER] Client is up to date — no transfer needed");
        close(fd);
        return;
    }

    // get update file path from version store
    const char *filepath = version_store_get_file();

    // get file size
    struct stat st;
    if (stat(filepath, &st) < 0) {
        logger_write(LOG_ERROR, tid, client_ip, "[SERVER] Cannot stat update file: %s", filepath);
        close(fd);
        return;
    }
    uint32_t file_size = (uint32_t)st.st_size;

    // compute checksum before sending
    char checksum[33];
    if (compute_md5(filepath, checksum) < 0) {
        logger_write(LOG_ERROR, tid, client_ip, "[SERVER] Cannot compute MD5 of: %s", filepath);
        close(fd);
        return;
    }

    // fill response
    resp.file_size = htonl(file_size);
    strncpy(resp.filename, filepath, MAX_FILENAME - 1);
    strncpy(resp.checksum, checksum, CHECKSUM_LEN);

    send(fd, &resp, sizeof(resp), 0);
    logger_write(LOG_INFO, tid, client_ip, "[SERVER] Client is outdated (latest: %u) — sending %u bytes", latest, file_size);
    logger_write(LOG_INFO, tid, client_ip, "[SERVER] MD5 checksum: %s", checksum);

    // stream the file in chunks
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        logger_write(LOG_ERROR, tid, client_ip, "[SERVER] Cannot open update file");
        close(fd);
        return;
    }

    char buf[4096];
    size_t bytes_read;
    uint32_t total_sent = 0;
    while ((bytes_read = fread(buf, 1, sizeof(buf), f)) > 0) {
        send(fd, buf, bytes_read, 0);
        total_sent += bytes_read;
    }
    fclose(f);

    logger_write(LOG_INFO, tid, client_ip, "[SERVER] Transfer complete — %u bytes sent", total_sent);
    close(fd);
    logger_write(LOG_INFO, tid, client_ip, "[SERVER] Connection closed");
}