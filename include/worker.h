#ifndef INLAY_WORKER_H
#define INLAY_WORKER_H

#include "inlay.h"
#include "storage.h"
#include "scheduler.h"
#include "telemetry.h"

typedef struct worker_context worker_context_t;

struct worker_context {
    int worker_id;
    const inlay_config_t *config;
    inlay_storage_t *storage;
    inlay_scheduler_t *scheduler;
    inlay_telemetry_t *telemetry;

    pthread_t thread;
    CURL *curl;

    _Atomic uint64_t bytes_downloaded;
    _Atomic bool is_running;
    bool error_occurred;
};

/* Start all worker threads */
int workers_start(worker_context_t *workers, int num_workers,
                  const inlay_config_t *config, inlay_storage_t *storage,
                  inlay_scheduler_t *scheduler, inlay_telemetry_t *telemetry);

/* Wait for all worker threads to finish */
void workers_join(worker_context_t *workers, int num_workers);

/* Single stream download fallback (when server does not support ranges) */
int worker_download_single_stream(const inlay_config_t *config, inlay_storage_t *storage,
                                 inlay_telemetry_t *telemetry, uint64_t total_size);

#endif /* INLAY_WORKER_H */
