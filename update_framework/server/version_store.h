
#pragma once

#include <stdint.h>

void     version_store_init(uint32_t latest);
uint32_t version_store_get_latest(void);