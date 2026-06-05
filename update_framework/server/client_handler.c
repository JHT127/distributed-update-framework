#include "client_handler.h"
#include "logger.h"
#include "version_store.h"
#include "../visualizer/dashboard.h"
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
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
#pragma GCC diagnostic pop

/* Map a thread's pthread_self() to a dashboard slot 0..pool_size-1.
   Simple modulo — good enough for up to MAX_THREADS workers. */
static int get_slot(void) {
    return (int)(pthread_self() % MAX_THREADS);
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

    int tid  = (int)(pthread_self() % 100);
    int slot = get_slot();

    /* ---- STEP 1: authentication ---- */
    dashboard_set_thread(slot, THREAD_AUTH, 0.0f, client_ip, 0, 0);

    AuthRequest  auth_req;
    AuthResponse auth_resp;

    int n = recv(fd, &auth_req, sizeof(auth_req), MSG_WAITALL);
    if (n != (int)sizeof(auth_req)) {
        logger_write(LOG_WARN, tid, client_ip, "[SERVER] Failed to receive AuthRequest");
        dashboard_set_thread(slot, THREAD_IDLE, 0.0f, "", 0, 0);
        dashboard_on_disconnect(0);
        close(fd);
        return;
    }
    auth_req.token[TOKEN_LEN] = '\0';

    if (!version_store_check_token(auth_req.token)) {
        auth_resp.accepted = 0;
        send(fd, &auth_resp, sizeof(auth_resp), 0);
        logger_write(LOG_WARN, tid, client_ip, "[SERVER] Auth REJECTED — token invalid");

        /* count auth failure in dashboard */
        pthread_mutex_lock(&g_stats_mutex);
        g_stats.auth_failures++;
        pthread_mutex_unlock(&g_stats_mutex);

        dashboard_set_thread(slot, THREAD_IDLE, 0.0f, "", 0, 0);
        dashboard_on_disconnect(0);
        close(fd);
        return;
    }

    auth_resp.accepted = 1;
    send(fd, &auth_resp, sizeof(auth_resp), 0);
    logger_write(LOG_INFO, tid, client_ip, "[SERVER] Auth accepted");

    /* ---- STEP 2: version check ---- */
    dashboard_set_thread(slot, THREAD_VERSION_CHECK, 0.0f, client_ip, 0, 0);

    VersionRequest req;
    n = recv(fd, &req, sizeof(req), MSG_WAITALL);
    if (n != (int)sizeof(req)) {
        logger_write(LOG_WARN, tid, client_ip, "[SERVER] Failed to receive VersionRequest");
        dashboard_set_thread(slot, THREAD_IDLE, 0.0f, "", 0, 0);
        dashboard_on_disconnect(0);
        close(fd);
        return;
    }
    req.version       = ntohl(req.version);
    req.resume_offset = ntohl(req.resume_offset);
    logger_write(LOG_INFO, tid, client_ip, "[SERVER] Client version: %u, resume offset: %u",
                 req.version, req.resume_offset);

    uint32_t latest = version_store_get_latest();

    UpdateResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.update_available = (req.version < latest) ? 1 : 0;

    if (!resp.update_available) {
        resp.file_size = htonl(0);
        send(fd, &resp, sizeof(resp), 0);
        logger_write(LOG_INFO, tid, client_ip, "[SERVER] Client is up to date — no transfer needed");
        dashboard_set_thread(slot, THREAD_DONE, 1.0f, client_ip, 0, 0);
        dashboard_on_disconnect(0);   /* 0 = no update sent */

        /* brief pause so DONE state is visible in the dashboard */
        usleep(400000);
        dashboard_set_thread(slot, THREAD_IDLE, 0.0f, "", 0, 0);
        close(fd);
        return;
    }

    const char *filepath = version_store_get_file();

    struct stat st;
    if (stat(filepath, &st) < 0) {
        logger_write(LOG_ERROR, tid, client_ip, "[SERVER] Cannot stat update file: %s", filepath);
        dashboard_set_thread(slot, THREAD_IDLE, 0.0f, "", 0, 0);
        dashboard_on_disconnect(0);
        close(fd);
        return;
    }
    uint32_t total_size = (uint32_t)st.st_size;

    if (req.resume_offset >= total_size)
        req.resume_offset = 0;

    uint32_t bytes_to_send = total_size - req.resume_offset;

    char checksum[33];
    if (compute_md5(filepath, checksum) < 0) {
        logger_write(LOG_ERROR, tid, client_ip, "[SERVER] Cannot compute MD5");
        dashboard_set_thread(slot, THREAD_IDLE, 0.0f, "", 0, 0);
        dashboard_on_disconnect(0);
        close(fd);
        return;
    }

    resp.file_size = htonl(bytes_to_send);
    strncpy(resp.filename, filepath, MAX_FILENAME - 1);
    strncpy(resp.checksum, checksum, CHECKSUM_LEN);
    send(fd, &resp, sizeof(resp), 0);

    logger_write(LOG_INFO, tid, client_ip,
                 "[SERVER] Client outdated (latest: %u) — sending %u bytes from offset %u",
                 latest, bytes_to_send, req.resume_offset);
    logger_write(LOG_INFO, tid, client_ip, "[SERVER] MD5 checksum: %s", checksum);

    /* ---- STEP 3: file transfer with live progress ---- */
    dashboard_set_thread(slot, THREAD_TRANSFERRING, 0.0f, client_ip, bytes_to_send, 0);

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        logger_write(LOG_ERROR, tid, client_ip, "[SERVER] Cannot open update file");
        dashboard_set_thread(slot, THREAD_IDLE, 0.0f, "", 0, 0);
        dashboard_on_disconnect(0);
        close(fd);
        return;
    }

    if (req.resume_offset > 0)
        fseek(f, req.resume_offset, SEEK_SET);

    char buf[4096];
    size_t bytes_read;
    uint32_t total_sent = 0;

    while ((bytes_read = fread(buf, 1, sizeof(buf), f)) > 0) {
        ssize_t sent = send(fd, buf, bytes_read, 0);
        if (sent <= 0) {
            logger_write(LOG_WARN, tid, client_ip, "[SERVER] send() failed mid-transfer");
            break;
        }
        total_sent += (uint32_t)sent;

        /* update progress in dashboard */
        float progress = (bytes_to_send > 0)
                         ? (float)total_sent / (float)bytes_to_send
                         : 1.0f;
        dashboard_set_thread(slot, THREAD_TRANSFERRING, progress,
                             client_ip, bytes_to_send, total_sent);
    }
    fclose(f);

    logger_write(LOG_INFO, tid, client_ip, "[SERVER] Transfer complete — %u bytes sent", total_sent);

    /* show DONE briefly then go idle */
    dashboard_set_thread(slot, THREAD_DONE, 1.0f, client_ip, bytes_to_send, total_sent);
    dashboard_on_disconnect(1);   /* 1 = update was sent */

    usleep(600000);   /* 0.6 s so operator can see the DONE state */
    dashboard_set_thread(slot, THREAD_IDLE, 0.0f, "", 0, 0);

    close(fd);
    logger_write(LOG_INFO, tid, client_ip, "[SERVER] Connection closed");
}
