#include "worker.h"
#include "s3.h"

typedef struct {
    worker_context_t *ctx;
    uint64_t assigned_start;
    uint64_t assigned_end;
    uint64_t current_offset;
    uint32_t next_chunk_to_complete;
    bool bisected;
} worker_stream_state_t;

static size_t worker_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total_bytes = size * nmemb;
    if (total_bytes == 0) return 0;

    worker_stream_state_t *state = (worker_stream_state_t *)userdata;
    worker_context_t *ctx = state->ctx;

    if (g_shutdown_requested) {
        return 0; /* Abort curl transfer immediately on shutdown signal */
    }

    /* Check if upper half was stolen by lagger-bisection */
    if (scheduler_should_worker_stop(ctx->scheduler, ctx->worker_id, state->current_offset)) {
        state->bisected = true;
        return 0; /* Stop current range transfer; scheduler will give new work */
    }

    /* Ensure we do not write beyond our current assigned end */
    uint64_t end = state->assigned_end;
    if (state->current_offset > end) {
        return 0;
    }

    uint64_t available_space = (end >= state->current_offset) ? (end - state->current_offset + 1) : 0;
    size_t to_write = (total_bytes <= available_space) ? total_bytes : (size_t)available_space;

    if (to_write > 0) {
        /* Direct positional write to target file descriptor - Zero file locks needed! */
        int ret = storage_pwrite_all(ctx->storage->fd, ptr, to_write, (off_t)state->current_offset);
        if (ret < 0) {
            fprintf(stderr, "[Worker %d] Failed pwrite at offset %" PRIu64 "\n",
                    ctx->worker_id, state->current_offset);
            return 0; /* Abort */
        }

        state->current_offset += to_write;

        /* Telemetry & scheduler progress update */
        telemetry_add_bytes(ctx->telemetry, to_write);
        atomic_fetch_add_explicit(&ctx->bytes_downloaded, to_write, memory_order_relaxed);
        scheduler_update_progress(ctx->scheduler, ctx->worker_id, state->current_offset);

        /* Incrementally complete all chunks whose end offset was reached */
        while (state->next_chunk_to_complete < ctx->scheduler->num_chunks) {
            uint64_t chunk_end = ctx->scheduler->chunks[state->next_chunk_to_complete].end_offset;
            if (state->current_offset > chunk_end ||
                (state->next_chunk_to_complete == ctx->scheduler->num_chunks - 1 &&
                 state->current_offset >= ctx->scheduler->file_size)) {
                scheduler_chunk_completed(ctx->scheduler, state->next_chunk_to_complete);
                state->next_chunk_to_complete++;
            } else {
                break;
            }
        }
    }

    if (to_write < total_bytes) {
        /* Truncated by bisection or reached end */
        state->bisected = true;
        return 0;
    }

    return total_bytes;
}

static void *worker_thread_fn(void *arg) {
    worker_context_t *ctx = (worker_context_t *)arg;
    atomic_store(&ctx->is_running, true);

    ctx->curl = curl_easy_init();
    if (!ctx->curl) {
        fprintf(stderr, "[Worker %d] Failed to init curl handle\n", ctx->worker_id);
        atomic_store(&ctx->is_running, false);
        return NULL;
    }

    char range_buf[64];
    curl_easy_setopt(ctx->curl, CURLOPT_URL, ctx->config->url);
    curl_easy_setopt(ctx->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(ctx->curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(ctx->curl, CURLOPT_USERAGENT, ctx->config->user_agent[0] ? ctx->config->user_agent : ("inlay/" INLAY_VERSION));
    curl_easy_setopt(ctx->curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(ctx->curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(ctx->curl, CURLOPT_WRITEFUNCTION, worker_write_callback);
    curl_easy_setopt(ctx->curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(ctx->curl, CURLOPT_LOW_SPEED_TIME, 30L);
    if (ctx->config->custom_headers) {
        curl_easy_setopt(ctx->curl, CURLOPT_HTTPHEADER, ctx->config->custom_headers);
    }
    if (ctx->config->insecure) {
        curl_easy_setopt(ctx->curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(ctx->curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    if (ctx->config->ip_version != CURL_IPRESOLVE_WHATEVER) {
        curl_easy_setopt(ctx->curl, CURLOPT_IPRESOLVE, ctx->config->ip_version);
    }
    if (ctx->config->http_version != 0) {
        curl_easy_setopt(ctx->curl, CURLOPT_HTTP_VERSION, ctx->config->http_version);
    }
    if (ctx->config->aws_sigv4_enabled) {
        s3_apply_curl_opts(ctx->curl, ctx->config);
    }
    if (ctx->config->max_speed_limit > 0) {
        curl_off_t per_worker = (curl_off_t)(ctx->config->max_speed_limit / ctx->config->num_workers);
        if (per_worker < 1024) per_worker = 1024;
        curl_easy_setopt(ctx->curl, CURLOPT_MAX_RECV_SPEED_LARGE, per_worker);
    }
    if (ctx->config->timeout_sec > 0) {
        curl_easy_setopt(ctx->curl, CURLOPT_TIMEOUT, ctx->config->timeout_sec);
    }
    if (ctx->config->verbose) {
        curl_easy_setopt(ctx->curl, CURLOPT_VERBOSE, 1L);
    }

    while (!g_shutdown_requested) {
        uint64_t start = 0, end = 0;
        sched_result_t res = scheduler_get_work(ctx->scheduler, ctx->worker_id, &start, &end);

        if (res == SCHED_WORK_DONE) {
            break;
        }

        if (res == SCHED_WORK_WAIT) {
            usleep(20000); /* 20ms */
            continue;
        }

        /* Work assigned */
        atomic_fetch_add(&ctx->telemetry->active_connections, 1);

        uint32_t start_chunk = (uint32_t)(start / ctx->scheduler->chunk_size);
        worker_stream_state_t stream_state = {
            .ctx = ctx,
            .assigned_start = start,
            .assigned_end = end,
            .current_offset = start,
            .next_chunk_to_complete = start_chunk,
            .bisected = false
        };

        snprintf(range_buf, sizeof(range_buf), "%" PRIu64 "-%" PRIu64, start, end);
        curl_easy_setopt(ctx->curl, CURLOPT_RANGE, range_buf);
        curl_easy_setopt(ctx->curl, CURLOPT_WRITEDATA, &stream_state);

        CURLcode curl_res = curl_easy_perform(ctx->curl);

        atomic_fetch_sub(&ctx->telemetry->active_connections, 1);

        long http_code = 0;
        curl_easy_getinfo(ctx->curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (g_shutdown_requested) {
            break;
        }

        if (curl_res == CURLE_OK || stream_state.bisected) {
            /* Ensure any completed chunk is marked done */
            while (stream_state.next_chunk_to_complete < ctx->scheduler->num_chunks) {
                uint64_t chunk_end = ctx->scheduler->chunks[stream_state.next_chunk_to_complete].end_offset;
                if (stream_state.current_offset > chunk_end ||
                    (stream_state.next_chunk_to_complete == ctx->scheduler->num_chunks - 1 &&
                     stream_state.current_offset >= ctx->scheduler->file_size)) {
                    scheduler_chunk_completed(ctx->scheduler, stream_state.next_chunk_to_complete);
                    stream_state.next_chunk_to_complete++;
                } else {
                    break;
                }
            }
        } else {
            /* Real network or transfer error: return incomplete byte range to queue */
            if (ctx->config->verbose) {
                fprintf(stderr, "[Worker %d] Transfer error on range %s: %s (HTTP %ld)\n",
                        ctx->worker_id, range_buf, curl_easy_strerror(curl_res), http_code);
            }
            scheduler_reclaim_range(ctx->scheduler, ctx->worker_id, stream_state.current_offset, end);
            usleep(100000); /* 100ms backoff before retrying */
        }
    }

    curl_easy_cleanup(ctx->curl);
    ctx->curl = NULL;
    atomic_store(&ctx->is_running, false);
    return NULL;
}

int workers_start(worker_context_t *workers, int num_workers,
                  const inlay_config_t *config, inlay_storage_t *storage,
                  inlay_scheduler_t *scheduler, inlay_telemetry_t *telemetry) {
    for (int i = 0; i < num_workers; i++) {
        workers[i].worker_id = i;
        workers[i].config = config;
        workers[i].storage = storage;
        workers[i].scheduler = scheduler;
        workers[i].telemetry = telemetry;
        atomic_store(&workers[i].bytes_downloaded, 0);
        atomic_store(&workers[i].is_running, false);
        workers[i].error_occurred = false;

        if (pthread_create(&workers[i].thread, NULL, worker_thread_fn, &workers[i]) != 0) {
            fprintf(stderr, "[!] Failed to create thread for worker %d: %s\n", i, strerror(errno));
            return -1;
        }
    }
    return 0;
}

void workers_join(worker_context_t *workers, int num_workers) {
    for (int i = 0; i < num_workers; i++) {
        if (workers[i].thread) {
            pthread_join(workers[i].thread, NULL);
            workers[i].thread = 0;
        }
    }
}

typedef struct {
    inlay_storage_t *storage;
    inlay_telemetry_t *telemetry;
    uint64_t current_offset;
} single_stream_state_t;

static size_t single_stream_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total_bytes = size * nmemb;
    single_stream_state_t *s = (single_stream_state_t *)userdata;

    if (g_shutdown_requested) return 0;

    int ret = storage_pwrite_all(s->storage->fd, ptr, total_bytes, (off_t)s->current_offset);
    if (ret < 0) return 0;

    s->current_offset += total_bytes;
    telemetry_add_bytes(s->telemetry, total_bytes);
    return total_bytes;
}

int worker_download_single_stream(const inlay_config_t *config, inlay_storage_t *storage,
                                 inlay_telemetry_t *telemetry, uint64_t total_size) {
    (void)total_size;
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    single_stream_state_t state = {
        .storage = storage,
        .telemetry = telemetry,
        .current_offset = 0
    };

    curl_easy_setopt(curl, CURLOPT_URL, config->url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, config->user_agent[0] ? config->user_agent : ("inlay/" INLAY_VERSION));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, single_stream_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    if (config->custom_headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, config->custom_headers);
    }
    if (config->insecure) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    if (config->ip_version != CURL_IPRESOLVE_WHATEVER) {
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, config->ip_version);
    }
    if (config->http_version != 0) {
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, config->http_version);
    }
    if (config->aws_sigv4_enabled) {
        s3_apply_curl_opts(curl, config);
    }
    if (config->max_speed_limit > 0) {
        curl_easy_setopt(curl, CURLOPT_MAX_RECV_SPEED_LARGE, (curl_off_t)config->max_speed_limit);
    }
    if (config->timeout_sec > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, config->timeout_sec);
    }
    if (config->verbose) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    atomic_store(&telemetry->active_connections, 1);
    CURLcode res = curl_easy_perform(curl);
    atomic_store(&telemetry->active_connections, 0);

    curl_easy_cleanup(curl);
    return (res == CURLE_OK) ? 0 : -1;
}
