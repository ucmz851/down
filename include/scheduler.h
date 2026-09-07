#ifndef DOWN_SCHEDULER_H
#define DOWN_SCHEDULER_H

#include "down.h"
#include "meta.h"

typedef enum {
    CHUNK_UNCLAIMED = 0,
    CHUNK_CLAIMED,
    CHUNK_COMPLETED
} chunk_status_t;

typedef struct {
    uint32_t chunk_idx;
    uint64_t start_offset;
    uint64_t end_offset;    /* inclusive */
    chunk_status_t status;
    int claimed_by_worker;
} chunk_desc_t;

typedef struct {
    pthread_mutex_t lock;
    _Atomic uint64_t current_offset;
    uint64_t assigned_start;
    uint64_t assigned_end;  /* inclusive */
    _Atomic bool is_active;
} worker_slot_t;

typedef enum {
    SCHED_WORK_ASSIGNED,
    SCHED_WORK_DONE,
    SCHED_WORK_WAIT
} sched_result_t;

typedef struct {
    down_meta_t *meta;
    uint64_t file_size;
    uint32_t chunk_size;
    uint32_t num_chunks;
    int num_workers;
    bool is_static;

    /* Dynamic chunk queue & allocation */
    chunk_desc_t *chunks;
    uint32_t next_unclaimed_chunk;

    /* Worker slots for lagger-bisection */
    worker_slot_t workers[MAX_NUM_WORKERS];

    /* Synchronization */
    pthread_mutex_t sched_lock;
    pthread_cond_t sched_cond;

    _Atomic uint32_t remaining_chunks;
    _Atomic bool all_completed;
} down_scheduler_t;

typedef down_scheduler_t inlay_scheduler_t;

/* Initialize scheduler (supports both dynamic work-stealing and static partitioning) */
int scheduler_init(down_scheduler_t *sched, down_meta_t *meta,
                   uint64_t file_size, uint32_t chunk_size, int num_workers, bool is_static);

/* Claim work: returns assigned byte range [out_start, out_end] */
sched_result_t scheduler_get_work(down_scheduler_t *sched, int worker_id,
                                  uint64_t *out_start, uint64_t *out_end);

/* Worker reports current writing offset (used for lagger bisection) */
void scheduler_update_progress(down_scheduler_t *sched, int worker_id, uint64_t current_offset);

/* Check if worker's active range was truncated by bisection */
bool scheduler_should_worker_stop(down_scheduler_t *sched, int worker_id, uint64_t current_offset);

/* Mark chunk as completed in scheduler and meta */
void scheduler_chunk_completed(down_scheduler_t *sched, uint32_t chunk_idx);

/* Reclaim incomplete chunk range upon failure */
void scheduler_reclaim_range(down_scheduler_t *sched, int worker_id, uint64_t from_offset, uint64_t to_offset);

/* Cleanup scheduler resources */
void scheduler_destroy(down_scheduler_t *sched);

#endif /* DOWN_SCHEDULER_H */
