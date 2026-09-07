#ifndef DOWN_PROBE_H
#define DOWN_PROBE_H

#include "down.h"

typedef struct {
    char effective_url[2048];
    char suggested_filename[512];
    uint64_t content_length;
    bool length_known;
    bool supports_range;
    long http_status;
} down_probe_t;

typedef down_probe_t inlay_probe_t;

/* Probe remote HTTP/HTTPS resource capabilities and metadata */
int probe_url(const down_config_t *config, down_probe_t *probe_res);

/* Extract filename from URL path if header did not supply one */
void probe_filename_from_url(const char *url, char *dest, size_t dest_size);

#endif /* DOWN_PROBE_H */
