#ifndef INLAY_BATCH_H
#define INLAY_BATCH_H

#include "inlay.h"

typedef struct {
    char url[2048];
    char output_name[512];
    char checksum_spec[256];
} batch_entry_t;

typedef struct {
    batch_entry_t *entries;
    size_t count;
    size_t capacity;
} batch_queue_t;

/* Initialize batch queue */
void batch_queue_init(batch_queue_t *queue);

/* Add entry to queue */
int batch_queue_add(batch_queue_t *queue, const char *url,
                    const char *output_name, const char *checksum_spec);

/* Load batch entries from input file (supporting plain URLs and aria2-style options) */
int batch_load_file(const char *filepath, batch_queue_t *queue);

/* Free heap allocated memory in queue */
void batch_queue_free(batch_queue_t *queue);

#endif /* INLAY_BATCH_H */
