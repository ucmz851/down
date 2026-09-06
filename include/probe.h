#ifndef INLAY_PROBE_H
#define INLAY_PROBE_H

#include "inlay.h"

typedef struct {
    char effective_url[2048];
    char suggested_filename[512];
    uint64_t content_length;
    bool length_known;
    bool supports_range;
    long http_status;
} inlay_probe_t;

/* Probe remote HTTP/HTTPS resource capabilities and metadata */
int probe_url(const inlay_config_t *config, inlay_probe_t *probe_res);

/* Extract filename from URL path if header did not supply one */
void probe_filename_from_url(const char *url, char *dest, size_t dest_size);

#endif /* INLAY_PROBE_H */
