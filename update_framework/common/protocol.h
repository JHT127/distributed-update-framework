
#pragma once
#include <stdint.h>

#define MAX_FILENAME 128

// Sent by client to server: "I am on version X"
typedef struct {
    uint32_t version;
} VersionRequest;

// Sent by server to client: "here's what I know"
typedef struct {
    uint8_t  update_available;
    uint32_t file_size;
    char     filename[MAX_FILENAME];
} UpdateResponse;