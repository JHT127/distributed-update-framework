
#pragma once
#include <stdint.h>

#define MAX_FILENAME  128
#define CHECKSUM_LEN  32

typedef struct {
    uint32_t version;
} VersionRequest;

typedef struct {
    uint8_t  update_available;
    uint32_t file_size;
    char     filename[MAX_FILENAME];
    char     checksum[CHECKSUM_LEN + 1];
} UpdateResponse;