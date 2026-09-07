#ifndef DOWN_WORKER_H
#define DOWN_WORKER_H

#include "down.h"
#include "storage.h"
#include "scheduler.h"
#include "telemetry.h"

typedef struct worker_context worker_context_t;

struct worker_context {
    int worker_id;
    const down_config_t *config;
    down_storage_t *storage;
    down_scheduler_t *scheduler;
    down_telemetry_t *telemetry;

    pthread_t thread;
    CURL *curl;

    _Atomic uint64_t bytes_downloaded;
    _Atomic bool is_running;
    bool error_occurred;
};

/* Start all worker threads */
int workers_start(worker_context_t *workers, int num_workers,
                  const down_config_t *config, down_storage_t *storage,
                  down_scheduler_t *scheduler, down_telemetry_t *telemetry);

/* Wait for all worker threads to finish */
void workers_join(worker_context_t *workers, int num_workers);

/* Single stream download fallback (when server does not support ranges) */
int worker_download_single_stream(const down_config_t *config, down_storage_t *storage,
                                 down_telemetry_t *telemetry, uint64_t total_size);

#endif /* DOWN_WORKER_H */
