#include "version_store.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t g_latest_version  = 0;
static char     g_filepath[256]   = {0};

#define MAX_TOKENS 64
static char g_tokens[MAX_TOKENS][65] = {0};
static int  g_token_count = 0;

void version_store_init(uint32_t latest, const char *filepath, const char *tokens_file) {
    g_latest_version = latest;
    strncpy(g_filepath, filepath, sizeof(g_filepath) - 1);

    // load valid tokens from file
    FILE *f = fopen(tokens_file, "r");
    if (!f) {
        fprintf(stderr, "[ERROR] Cannot open tokens file: %s\n", tokens_file);
        return;
    }
    char line[128];
    while (g_token_count < MAX_TOKENS && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;
        strncpy(g_tokens[g_token_count], line, 64);
        g_token_count++;
    }
    fclose(f);
}

uint32_t version_store_get_latest(void) {
    return g_latest_version;
}

const char *version_store_get_file(void) {
    return g_filepath;
}

int version_store_check_token(const char *token) {
    for (int i = 0; i < g_token_count; i++) {
        if (strcmp(g_tokens[i], token) == 0)
            return 1;
    }
    return 0;
}