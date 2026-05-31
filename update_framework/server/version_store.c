#include "version_store.h"
#include <stdint.h>
#include <string.h>

static uint32_t g_latest_version = 0;
static char     g_filepath[256]  = {0};

void version_store_init(uint32_t latest, const char *filepath) {
    g_latest_version = latest;
    strncpy(g_filepath, filepath, sizeof(g_filepath) - 1);
}

uint32_t version_store_get_latest(void) {
    return g_latest_version;
}

const char *version_store_get_file(void) {
    return g_filepath;
}