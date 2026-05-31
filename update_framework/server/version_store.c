
#include "version_store.h"
#include <stdint.h>

static uint32_t g_latest_version = 0;

void version_store_init(uint32_t latest) {
    g_latest_version = latest;
}

uint32_t version_store_get_latest(void) {
    return g_latest_version;
}