
#pragma once
#include <stdint.h>

void        version_store_init(uint32_t latest, const char *filepath);
uint32_t    version_store_get_latest(void);
const char *version_store_get_file(void);