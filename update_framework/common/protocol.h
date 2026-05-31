#pragma once
#include <stdint.h>

#define MAX_FILENAME  128
#define CHECKSUM_LEN  32
#define TOKEN_LEN     64

typedef struct {
    uint32_t version;
    uint32_t resume_offset;
} VersionRequest;

typedef struct {
    uint8_t  update_available;
    uint32_t file_size;
    char     filename[MAX_FILENAME];
    char     checksum[CHECKSUM_LEN + 1];
} UpdateResponse;

typedef struct {
    char token[TOKEN_LEN + 1];
} AuthRequest;

typedef struct {
    uint8_t accepted;
} AuthResponse;