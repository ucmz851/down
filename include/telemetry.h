#ifndef INLAY_TELEMETRY_H
#define INLAY_TELEMETRY_H

#include "inlay.h"

typedef struct {
    _Atomic uint64_t total_size;
    _Atomic uint64_t downloaded_bytes;
    _Atomic uint64_t initial_bytes;
    _Atomic int active_connections;
    _Atomic bool stop_requested;
    bool quiet;

    pthread_t thread;
    uint64_t start_time_us;
} inlay_telemetry_t;

/* Initialize telemetry context */
int telemetry_init(inlay_telemetry_t *telem, uint64_t total_size, uint64_t initial_bytes, bool quiet);

/* Start background telemetry display thread */
int telemetry_start(inlay_telemetry_t *telem);

/* Stop background telemetry thread */
void telemetry_stop(inlay_telemetry_t *telem);

/* Print clean summary card upon completion */
void telemetry_print_complete(const inlay_telemetry_t *telem, const char *filepath);

/* Print clean summary card upon pause/interruption */
void telemetry_print_paused(const inlay_telemetry_t *telem, const char *filepath,
                            const char *meta_path, const char *url);

/* Format duration (e.g., 01m 24s or 45s) */
void format_duration(uint64_t seconds, char *buf, size_t buf_size);

/* Add bytes directly to downloaded counter */
static inline void telemetry_add_bytes(inlay_telemetry_t *telem, size_t bytes) {
    if (telem) {
        atomic_fetch_add_explicit(&telem->downloaded_bytes, bytes, memory_order_relaxed);
    }
}

#endif /* INLAY_TELEMETRY_H */
