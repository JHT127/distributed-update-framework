
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Reads a KEY=VALUE config file and returns the value for the given key.
// Returns NULL if the key is not found.
char *parse_config(const char *filename, const char *key) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "[ERROR] Cannot open config file: %s\n", filename);
        return NULL;
    }

    static char value[256];
    char line[512];

    while (fgets(line, sizeof(line), f)) {
        // Strip trailing newline
        line[strcspn(line, "\r\n")] = '\0';

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *k = line;
        char *v = eq + 1;

        if (strcmp(k, key) == 0) {
            strncpy(value, v, sizeof(value) - 1);
            value[sizeof(value) - 1] = '\0';
            fclose(f);
            return value;
        }
    }

    fclose(f);
    return NULL;
}