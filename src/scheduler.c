#include "scheduler.h"
#include <sys/time.h>

#define CHUNKS_PER_CLAIM 4

int scheduler_init(down_scheduler_t *sched, down_meta_t *meta,
                   uint64_t file_size, uint32_t chunk_size, int num_workers, bool is_static) {
    if (!sched || !meta || file_size == 0 || chunk_size == 0 || num_workers <= 0) return -1;
    memset(sched, 0, sizeof(*sched));

    sched->meta = meta;
    sched->file_size = file_size;
    sched->chunk_size = chunk_size;
    sched->num_workers = num_workers > MAX_NUM_WORKERS ? MAX_NUM_WORKERS : num_workers;
    sched->is_static = is_static;

    sched->num_chunks = (uint32_t)((file_size + chunk_size - 1) / chunk_size);
    if (sched->num_chunks == 0) sched->num_chunks = 1;

    sched->chunks = (chunk_desc_t *)calloc(sched->num_chunks, sizeof(chunk_desc_t));
    if (!sched->chunks) {
        fprintf(stderr, "[!] Out of memory allocating chunk table for %u chunks\n", sched->num_chunks);
        return -1;
    }

    pthread_mutex_init(&sched->sched_lock, NULL);
    pthread_cond_init(&sched->sched_cond, NULL);

    uint32_t remaining = 0;
    for (uint32_t i = 0; i < sched->num_chunks; i++) {
        sched->chunks[i].chunk_idx = i;
        sched->chunks[i].start_offset = (uint64_t)i * chunk_size;
        uint64_t end = (uint64_t)(i + 1) * chunk_size - 1;
        if (end >= file_size) end = file_size - 1;
        sched->chunks[i].end_offset = end;
        sched->chunks[i].claimed_by_worker = -1;

        if (meta_is_chunk_done(meta, i)) {
            sched->chunks[i].status = CHUNK_COMPLETED;
        } else {
            sched->chunks[i].status = CHUNK_UNCLAIMED;
            remaining++;
        }
    }

    atomic_store(&sched->remaining_chunks, remaining);
    atomic_store(&sched->all_completed, (remaining == 0));
    sched->next_unclaimed_chunk = 0;

    for (int w = 0; w < sched->num_workers; w++) {
        pthread_mutex_init(&sched->workers[w].lock, NULL);
        atomic_store(&sched->workers[w].current_offset, 0);
        sched->workers[w].assigned_start = 0;
        sched->workers[w].assigned_end = 0;
        atomic_store(&sched->workers[w].is_active, false);
    }

    return 0;
}

sched_result_t scheduler_get_work(down_scheduler_t *sched, int worker_id,
                                  uint64_t *out_start, uint64_t *out_end) {
    if (!sched || worker_id < 0 || worker_id >= sched->num_workers) return SCHED_WORK_DONE;

    pthread_mutex_lock(&sched->sched_lock);

    /* Mark this worker currently idle while searching for work */
    atomic_store(&sched->workers[worker_id].is_active, false);

    while (true) {
        if (atomic_load(&sched->all_completed) || atomic_load(&sched->remaining_chunks) == 0) {
            pthread_mutex_unlock(&sched->sched_lock);
            return SCHED_WORK_DONE;
        }

        /* --- MODE 1: Static Partitioning (Phase 2) --- */
        if (sched->is_static) {
            worker_slot_t *slot = &sched->workers[worker_id];
            if (slot->assigned_end > 0 || atomic_load(&slot->is_active)) {
                /* Static range already assigned and finished */
                pthread_mutex_unlock(&sched->sched_lock);
                return SCHED_WORK_DONE;
            }

            uint64_t slice = sched->file_size / sched->num_workers;
            uint64_t start = (uint64_t)worker_id * slice;
            uint64_t end = (worker_id == sched->num_workers - 1) ? (sched->file_size - 1) : ((uint64_t)(worker_id + 1) * slice - 1);

            slot->assigned_start = start;
            slot->assigned_end = end;
            atomic_store(&slot->current_offset, start);
            atomic_store(&slot->is_active, true);

            *out_start = start;
            *out_end = end;
            pthread_mutex_unlock(&sched->sched_lock);
            return SCHED_WORK_ASSIGNED;
        }

        /* --- MODE 2: Dynamic Chunk Claiming --- */
        /* Find next unclaimed chunk */
        while (sched->next_unclaimed_chunk < sched->num_chunks &&
               sched->chunks[sched->next_unclaimed_chunk].status != CHUNK_UNCLAIMED) {
            sched->next_unclaimed_chunk++;
        }

        if (sched->next_unclaimed_chunk < sched->num_chunks) {
            uint32_t first = sched->next_unclaimed_chunk;
            uint32_t last = first;
            sched->chunks[first].status = CHUNK_CLAIMED;
            sched->chunks[first].claimed_by_worker = worker_id;

            /* Group up to CHUNKS_PER_CLAIM contiguous chunks for efficiency */
            for (uint32_t i = 1; i < CHUNKS_PER_CLAIM && (first + i) < sched->num_chunks; i++) {
                if (sched->chunks[first + i].status == CHUNK_UNCLAIMED) {
                    sched->chunks[first + i].status = CHUNK_CLAIMED;
                    sched->chunks[first + i].claimed_by_worker = worker_id;
                    last = first + i;
                } else {
                    break;
                }
            }

            sched->next_unclaimed_chunk = last + 1;

            uint64_t start = sched->chunks[first].start_offset;
            uint64_t end = sched->chunks[last].end_offset;

            worker_slot_t *slot = &sched->workers[worker_id];
            pthread_mutex_lock(&slot->lock);
            slot->assigned_start = start;
            slot->assigned_end = end;
            atomic_store(&slot->current_offset, start);
            atomic_store(&slot->is_active, true);
            pthread_mutex_unlock(&slot->lock);

            *out_start = start;
            *out_end = end;
            pthread_mutex_unlock(&sched->sched_lock);
            return SCHED_WORK_ASSIGNED;
        }

        /* --- MODE 3: Dynamic Work-Stealing / Lagger-Bisection (Phase 3) --- */
        int lagger_id = -1;
        uint64_t max_remaining = 0;

        for (int w = 0; w < sched->num_workers; w++) {
            if (w == worker_id) continue;
            if (!atomic_load(&sched->workers[w].is_active)) continue;

            pthread_mutex_lock(&sched->workers[w].lock);
            uint64_t cur = atomic_load(&sched->workers[w].current_offset);
            uint64_t end = sched->workers[w].assigned_end;
            pthread_mutex_unlock(&sched->workers[w].lock);

            if (end > cur) {
                uint64_t remaining = end - cur;
                if (remaining > max_remaining) {
                    max_remaining = remaining;
                    lagger_id = w;
                }
            }
        }

        /* Only bisect if lagger has at least 2 full chunks remaining */
        if (lagger_id >= 0 && max_remaining >= (2ULL * sched->chunk_size)) {
            worker_slot_t *lagger = &sched->workers[lagger_id];
            pthread_mutex_lock(&lagger->lock);

            /* Re-check under lock */
            uint64_t cur = atomic_load(&lagger->current_offset);
            uint64_t end = lagger->assigned_end;

            if (end >= cur && (end - cur + 1) >= (2ULL * sched->chunk_size)) {
                uint32_t first_c = (uint32_t)(cur / sched->chunk_size);
                if ((uint64_t)first_c * sched->chunk_size < cur) {
                    first_c++;
                }
                uint32_t last_c = (uint32_t)(end / sched->chunk_size);

                if (last_c > first_c) {
                    uint32_t count = last_c - first_c + 1;
                    uint32_t split_chunk = first_c + (count / 2);
                    uint64_t split_point = (uint64_t)split_chunk * sched->chunk_size;

                    if (split_point > cur && split_point <= end) {
                        /* Shorten lagger's range */
                        lagger->assigned_end = split_point - 1;
                        pthread_mutex_unlock(&lagger->lock);

                        /* Stolen range is [split_point, end] */
                        uint32_t first_stolen_chunk = split_chunk;
                        uint32_t last_stolen_chunk = last_c;
                        if (last_stolen_chunk >= sched->num_chunks) {
                            last_stolen_chunk = sched->num_chunks - 1;
                        }

                        for (uint32_t c = first_stolen_chunk; c <= last_stolen_chunk; c++) {
                            if (sched->chunks[c].status != CHUNK_COMPLETED) {
                                sched->chunks[c].status = CHUNK_CLAIMED;
                                sched->chunks[c].claimed_by_worker = worker_id;
                            }
                        }

                        worker_slot_t *my_slot = &sched->workers[worker_id];
                        pthread_mutex_lock(&my_slot->lock);
                        my_slot->assigned_start = split_point;
                        my_slot->assigned_end = end;
                        atomic_store(&my_slot->current_offset, split_point);
                        atomic_store(&my_slot->is_active, true);
                        pthread_mutex_unlock(&my_slot->lock);

                        *out_start = split_point;
                        *out_end = end;
                        pthread_mutex_unlock(&sched->sched_lock);
                        return SCHED_WORK_ASSIGNED;
                    }
                }
            }
            pthread_mutex_unlock(&lagger->lock);
        }

        /* Check if any worker is active */
        bool any_active = false;
        for (int w = 0; w < sched->num_workers; w++) {
            if (atomic_load(&sched->workers[w].is_active)) {
                any_active = true;
                break;
            }
        }

        if (!any_active) {
            /* No unclaimed chunks and no active workers -> all done */
            atomic_store(&sched->all_completed, true);
            pthread_mutex_unlock(&sched->sched_lock);
            return SCHED_WORK_DONE;
        }

        /* Wait briefly for active workers to make progress or complete */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 50 * 1000000L; /* 50ms */
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&sched->sched_cond, &sched->sched_lock, &ts);
    }
}

void scheduler_update_progress(down_scheduler_t *sched, int worker_id, uint64_t current_offset) {
    if (!sched || worker_id < 0 || worker_id >= sched->num_workers) return;
    atomic_store_explicit(&sched->workers[worker_id].current_offset, current_offset, memory_order_release);
}

bool scheduler_should_worker_stop(down_scheduler_t *sched, int worker_id, uint64_t current_offset) {
    if (!sched || worker_id < 0 || worker_id >= sched->num_workers) return true;
    worker_slot_t *slot = &sched->workers[worker_id];
    pthread_mutex_lock(&slot->lock);
    uint64_t end = slot->assigned_end;
    pthread_mutex_unlock(&slot->lock);
    return (current_offset > end);
}

void scheduler_chunk_completed(down_scheduler_t *sched, uint32_t chunk_idx) {
    if (!sched || chunk_idx >= sched->num_chunks) return;

    pthread_mutex_lock(&sched->sched_lock);
    if (sched->chunks[chunk_idx].status != CHUNK_COMPLETED) {
        sched->chunks[chunk_idx].status = CHUNK_COMPLETED;
        meta_mark_chunk_done(sched->meta, chunk_idx);

        uint32_t rem = atomic_fetch_sub(&sched->remaining_chunks, 1);
        if (rem <= 1) {
            atomic_store(&sched->all_completed, true);
            pthread_cond_broadcast(&sched->sched_cond);
        }
    }
    pthread_mutex_unlock(&sched->sched_lock);
}

void scheduler_reclaim_range(down_scheduler_t *sched, int worker_id, uint64_t from_offset, uint64_t to_offset) {
    if (!sched) return;

    pthread_mutex_lock(&sched->sched_lock);
    uint32_t first = (uint32_t)(from_offset / sched->chunk_size);
    uint32_t last = (uint32_t)(to_offset / sched->chunk_size);
    if (last >= sched->num_chunks) last = sched->num_chunks - 1;

    for (uint32_t c = first; c <= last; c++) {
        if (sched->chunks[c].status == CHUNK_CLAIMED && sched->chunks[c].claimed_by_worker == worker_id) {
            if (!meta_is_chunk_done(sched->meta, c)) {
                sched->chunks[c].status = CHUNK_UNCLAIMED;
                sched->chunks[c].claimed_by_worker = -1;
                if (c < sched->next_unclaimed_chunk) {
                    sched->next_unclaimed_chunk = c;
                }
            } else {
                sched->chunks[c].status = CHUNK_COMPLETED;
            }
        }
    }

    atomic_store(&sched->workers[worker_id].is_active, false);
    pthread_cond_broadcast(&sched->sched_cond);
    pthread_mutex_unlock(&sched->sched_lock);
}

void scheduler_destroy(down_scheduler_t *sched) {
    if (!sched) return;
    for (int w = 0; w < sched->num_workers; w++) {
        pthread_mutex_destroy(&sched->workers[w].lock);
    }
    pthread_mutex_destroy(&sched->sched_lock);
    pthread_cond_destroy(&sched->sched_cond);

    if (sched->chunks) {
        free(sched->chunks);
        sched->chunks = NULL;
    }
}
