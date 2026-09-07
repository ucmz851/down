#include "down.h"
#include "probe.h"
#include "storage.h"
#include "meta.h"
#include "scheduler.h"
#include "worker.h"
#include "telemetry.h"
#include "cli.h"
#include "checksum.h"
#include "s3.h"
#include "batch.h"
#include "history.h"

volatile sig_atomic_t g_shutdown_requested = 0;

static void handle_signal(int sig) {
    (void)sig;
    g_shutdown_requested = 1;
}

static void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Ignore SIGPIPE for multi-threaded socket safety */
    signal(SIGPIPE, SIG_IGN);
}

static struct curl_slist *clone_slist(const struct curl_slist *src) {
    struct curl_slist *dst = NULL;
    for (const struct curl_slist *p = src; p != NULL; p = p->next) {
        dst = curl_slist_append(dst, p->data);
    }
    return dst;
}

static void print_download_spec(const down_config_t *config, const down_probe_t *probe,
                                bool is_resumed, uint32_t completed_chunks, uint32_t total_chunks,
                                uint64_t initial_bytes) {
    if (config->quiet) return;

    bool color = isatty(STDOUT_FILENO) && !getenv("NO_COLOR");
    const char *dim = color ? "\033[38;5;244m" : "";
    const char *bold = color ? "\033[1m" : "";
    const char *cyan = color ? "\033[1;36m" : "";
    const char *yellow = color ? "\033[1;33m" : "";
    const char *reset = color ? "\033[0m" : "";

    char size_str[32];
    if (probe->length_known) {
        format_bytes(probe->content_length, size_str, sizeof(size_str));
    } else {
        snprintf(size_str, sizeof(size_str), "unknown");
    }

    printf("\n%s── down %s ─────────────────────────────────────────────────────────%s\n", dim, DOWN_VERSION, reset);
    printf(" Target   : %s%s%s\n", bold, config->output_path, reset);
    if (probe->length_known) {
        printf(" Size     : %s%s%s (%" PRIu64 " bytes)\n", bold, size_str, reset, probe->content_length);
    } else {
        printf(" Size     : %s%s%s\n", bold, size_str, reset);
    }
    printf(" Source   : %s\n", config->url);

    if (probe->supports_range && probe->length_known && !config->force_single_stream && config->num_workers > 1) {
        printf(" Engine   : %s%s%s (%d workers, %u KB chunk size)\n",
               cyan, config->use_static ? "Static Partitioning" : "Dynamic Work-Stealing", reset,
               config->num_workers, config->chunk_size / 1024);
    } else {
        printf(" Engine   : %sSingle-Stream Sequential%s\n", cyan, reset);
    }

    if (config->http_version == CURL_HTTP_VERSION_3) {
        printf(" Protocol : HTTP/3 (QUIC) with fallback\n");
    } else if (config->http_version == CURL_HTTP_VERSION_3ONLY) {
        printf(" Protocol : HTTP/3 (QUIC only)\n");
    }

    if (config->aws_sigv4_enabled) {
        printf(" Auth     : AWS SigV4 (Region: %s, Service: %s)\n",
               config->aws_region[0] ? config->aws_region : "us-east-1",
               config->aws_service[0] ? config->aws_service : "s3");
    }

    if (config->expected_checksum[0] != '\0') {
        printf(" Checksum : %s:%s\n", config->checksum_algo, config->expected_checksum);
    }

    if (!config->no_fallocate && probe->length_known) {
        printf(" Storage  : Contiguous blocks pre-allocated (posix_fallocate)\n");
    }

    if (config->max_speed_limit > 0) {
        char limit_str[32];
        format_bytes(config->max_speed_limit, limit_str, sizeof(limit_str));
        printf(" Speed    : Rate-limited to %s/s\n", limit_str);
    }

    if (config->insecure) {
        printf(" Security : TLS verification disabled (insecure mode)\n");
    }

    if (is_resumed && completed_chunks > 0) {
        char init_str[32];
        format_bytes(initial_bytes, init_str, sizeof(init_str));
        double percent = probe->content_length > 0 ? ((double)initial_bytes / (double)probe->content_length) * 100.0 : 0.0;
        printf(" Status   : %sResuming from chunk %u/%u (%s / %.1f%% completed)%s\n",
               yellow, completed_chunks, total_chunks, init_str, percent, reset);
    }
    printf("%s─────────────────────────────────────────────────────────────────────────%s\n\n", dim, reset);
    fflush(stdout);
}

static int execute_download(down_config_t *config) {
    if (!config) return -1;

    /* S3 URL handling and auth */
    if (s3_is_s3_url(config->url)) {
        if (s3_transform_url(config) != 0) {
            return -1;
        }
    }
    s3_init_auth(config);

    /* Probe remote resource */
    if (!config->quiet) {
        printf("[*] Probing remote resource: %s\n", config->url);
        fflush(stdout);
    }

    down_probe_t probe;
    if (probe_url(config, &probe) != 0) {
        fprintf(stderr, "[!] Failed to probe remote URL\n");
        return -1;
    }

    /* Resolve destination filename and directory */
    char resolved_filename[1024];
    if (config->output_path[0] != '\0') {
        snprintf(resolved_filename, sizeof(resolved_filename), "%s", config->output_path);
    } else {
        snprintf(resolved_filename, sizeof(resolved_filename), "%s", probe.suggested_filename);
    }

    if (config->output_dir[0] != '\0') {
        if (make_directory_recursive(config->output_dir) != 0) {
            fprintf(stderr, "[!] Error: failed to create destination directory '%s': %s\n",
                    config->output_dir, strerror(errno));
            return -1;
        }
        size_t dlen = strlen(config->output_dir);
        int rem = (int)sizeof(config->output_path) - (int)dlen - 2;
        if (rem < 1) rem = 1;
        if (config->output_dir[dlen - 1] == '/') {
            snprintf(config->output_path, sizeof(config->output_path), "%s%.*s",
                     config->output_dir, rem, resolved_filename);
        } else {
            snprintf(config->output_path, sizeof(config->output_path), "%s/%.*s",
                     config->output_dir, rem, resolved_filename);
        }
    } else {
        snprintf(config->output_path, sizeof(config->output_path), "%s", resolved_filename);
    }

    /* Check if control file exists for resume (.down first, fallback to .inlay) */
    char meta_path[1200];
    snprintf(meta_path, sizeof(meta_path), "%s%s", config->output_path, DOWN_META_EXT);
    struct stat st;
    bool has_meta_file = (stat(meta_path, &st) == 0);
    if (!has_meta_file) {
        char legacy_meta[1200];
        snprintf(legacy_meta, sizeof(legacy_meta), "%s%s", config->output_path, INLAY_META_EXT);
        if (stat(legacy_meta, &st) == 0) {
            has_meta_file = true;
            strncpy(meta_path, legacy_meta, sizeof(meta_path) - 1);
            meta_path[sizeof(meta_path) - 1] = '\0';
        }
    }

    if (has_meta_file) {
        config->resume_mode = true;
    }

    /* Initialize target file and pre-allocate disk blocks */
    down_storage_t storage;
    if (storage_init(&storage, config->output_path, probe.content_length, config->no_fallocate, config->resume_mode) != 0) {
        fprintf(stderr, "[!] Storage initialization failed\n");
        return -1;
    }

    /* Record start in history database */
    history_record_start(config, probe.content_length);

    int download_ret = 0;

    /* Multi-worker ranged download path */
    if (probe.supports_range && probe.length_known && !config->force_single_stream && config->num_workers > 1) {
        down_meta_t meta;
        if (meta_open(&meta, config->output_path, probe.effective_url,
                      probe.content_length, config->chunk_size, config->resume_mode) != 0) {
            fprintf(stderr, "[!] Failed to initialize .down control file\n");
            storage_close(&storage);
            return -1;
        }

        uint32_t completed_chunks = atomic_load(&meta.hdr->completed_chunks);
        uint64_t initial_bytes = (uint64_t)completed_chunks * config->chunk_size;
        if (initial_bytes > probe.content_length) initial_bytes = probe.content_length;

        if (completed_chunks >= meta.hdr->num_chunks) {
            printf("[+] File is already completely downloaded: %s\n", config->output_path);
            meta_remove(&meta);
            storage_close(&storage);
            return 0;
        }

        /* Print clean download spec card */
        print_download_spec(config, &probe, meta.is_resumed, completed_chunks,
                            meta.hdr->num_chunks, initial_bytes);

        down_telemetry_t telemetry;
        telemetry_init(&telemetry, probe.content_length, initial_bytes, config->quiet);
        telemetry_start(&telemetry);

        down_scheduler_t scheduler;
        if (scheduler_init(&scheduler, &meta, probe.content_length, config->chunk_size,
                           config->num_workers, config->use_static) != 0) {
            fprintf(stderr, "[!] Scheduler initialization failed\n");
            telemetry_stop(&telemetry);
            meta_close(&meta);
            storage_close(&storage);
            return -1;
        }

        worker_context_t workers[MAX_NUM_WORKERS];
        workers_start(workers, config->num_workers, config, &storage, &scheduler, &telemetry);

        /* Wait for workers to finish */
        workers_join(workers, config->num_workers);

        /* Stop telemetry and flush storage */
        telemetry_stop(&telemetry);
        storage_sync(&storage);

        bool all_done = (atomic_load(&meta.hdr->completed_chunks) >= meta.hdr->num_chunks);
        scheduler_destroy(&scheduler);

        if (all_done) {
            /* Checksum validation if requested */
            if (config->expected_checksum[0] != '\0') {
                if (!config->quiet) {
                    printf("[*] Verifying file checksum (%s)...\n", config->checksum_algo);
                    fflush(stdout);
                }
                char actual_hex[256] = {0};
                int v_res = checksum_verify_file(config->output_path, config->checksum_algo,
                                                 config->expected_checksum, actual_hex, sizeof(actual_hex));
                if (v_res == 0) {
                    if (!config->quiet) {
                        printf("[+] Checksum verified: %s: %s\n", config->checksum_algo, actual_hex);
                    }
                    meta_remove(&meta);
                    telemetry_print_complete(&telemetry, config->output_path);
                    history_record_complete(config, probe.content_length);
                    download_ret = 0;
                } else if (v_res == 1) {
                    fprintf(stderr, "\n[!] ERROR: Checksum mismatch for %s!\n", config->output_path);
                    fprintf(stderr, "    Expected: %s\n", config->expected_checksum);
                    fprintf(stderr, "    Actual  : %s\n", actual_hex);
                    fprintf(stderr, "    Digest  : %s\n\n", config->checksum_algo);
                    meta_sync(&meta, true);
                    meta_close(&meta);
                    download_ret = 2;
                } else {
                    fprintf(stderr, "\n[!] ERROR: Checksum calculation failed for %s\n", config->output_path);
                    meta_sync(&meta, true);
                    meta_close(&meta);
                    download_ret = -1;
                }
            } else {
                meta_remove(&meta);
                telemetry_print_complete(&telemetry, config->output_path);
                history_record_complete(config, probe.content_length);
                download_ret = 0;
            }
        } else if (g_shutdown_requested) {
            meta_sync(&meta, true);
            meta_close(&meta);
            uint64_t cur = atomic_load(&telemetry.downloaded_bytes);
            history_record_update(config, cur, probe.content_length, DOWN_STATUS_INTERRUPTED);
            telemetry_print_paused(&telemetry, config->output_path, meta_path, config->url);
            download_ret = -2;
        } else {
            meta_sync(&meta, true);
            meta_close(&meta);
            uint64_t cur = atomic_load(&telemetry.downloaded_bytes);
            history_record_update(config, cur, probe.content_length, DOWN_STATUS_INTERRUPTED);
            fprintf(stderr, "\n[!] Download incomplete due to network transfer error. Resume with -c.\n");
            download_ret = -1;
        }
    } else {
        /* Single-stream download path */
        print_download_spec(config, &probe, false, 0, 0, 0);

        down_telemetry_t telemetry;
        telemetry_init(&telemetry, probe.content_length, 0, config->quiet);
        telemetry_start(&telemetry);

        int res = worker_download_single_stream(config, &storage, &telemetry, probe.content_length);

        telemetry_stop(&telemetry);
        storage_sync(&storage);

        if (res == 0 && !g_shutdown_requested) {
            /* Checksum validation if requested */
            if (config->expected_checksum[0] != '\0') {
                if (!config->quiet) {
                    printf("[*] Verifying file checksum (%s)...\n", config->checksum_algo);
                    fflush(stdout);
                }
                char actual_hex[256] = {0};
                int v_res = checksum_verify_file(config->output_path, config->checksum_algo,
                                                 config->expected_checksum, actual_hex, sizeof(actual_hex));
                if (v_res == 0) {
                    if (!config->quiet) {
                        printf("[+] Checksum verified: %s: %s\n", config->checksum_algo, actual_hex);
                    }
                    telemetry_print_complete(&telemetry, config->output_path);
                    history_record_complete(config, probe.content_length);
                    download_ret = 0;
                } else if (v_res == 1) {
                    fprintf(stderr, "\n[!] ERROR: Checksum mismatch for %s!\n", config->output_path);
                    fprintf(stderr, "    Expected: %s\n", config->expected_checksum);
                    fprintf(stderr, "    Actual  : %s\n", actual_hex);
                    fprintf(stderr, "    Digest  : %s\n\n", config->checksum_algo);
                    download_ret = 2;
                } else {
                    fprintf(stderr, "\n[!] ERROR: Checksum calculation failed for %s\n", config->output_path);
                    download_ret = -1;
                }
            } else {
                telemetry_print_complete(&telemetry, config->output_path);
                history_record_complete(config, probe.content_length);
                download_ret = 0;
            }
        } else if (g_shutdown_requested) {
            uint64_t cur = atomic_load(&telemetry.downloaded_bytes);
            history_record_update(config, cur, probe.content_length, DOWN_STATUS_INTERRUPTED);
            printf("\n[!] Single stream download aborted by user.\n");
            download_ret = -2;
        } else {
            uint64_t cur = atomic_load(&telemetry.downloaded_bytes);
            history_record_update(config, cur, probe.content_length, DOWN_STATUS_FAILED);
            fprintf(stderr, "\n[!] Single stream download failed.\n");
            download_ret = -1;
        }
    }

    storage_close(&storage);
    return download_ret;
}

int main(int argc, char **argv) {
    setup_signals();

    down_config_t config;
    if (cli_parse_args(argc, argv, &config) != 0) {
        config_cleanup(&config);
        return 1;
    }

    if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
        fprintf(stderr, "[!] Failed to initialize libcurl\n");
        config_cleanup(&config);
        return 1;
    }

    int final_status = 0;

    /* Check if batch mode is requested */
    if (config.input_file[0] != '\0') {
        batch_queue_t queue;
        batch_queue_init(&queue);

        if (batch_load_file(config.input_file, &queue) != 0) {
            fprintf(stderr, "[!] Failed to read batch input file: %s\n", config.input_file);
            config_cleanup(&config);
            curl_global_cleanup();
            return 1;
        }

        /* If a positional URL was also specified, append or prepend it */
        if (config.url[0] != '\0') {
            batch_queue_add(&queue, config.url, config.output_path[0] ? config.output_path : NULL,
                            config.checksum_spec[0] ? config.checksum_spec : NULL);
        }

        if (queue.count == 0) {
            fprintf(stderr, "[!] Error: input file '%s' contains no valid URLs\n", config.input_file);
            batch_queue_free(&queue);
            config_cleanup(&config);
            curl_global_cleanup();
            return 1;
        }

        if (!config.quiet) {
            printf("[*] Loaded %zu URLs from '%s'\n", queue.count, config.input_file);
        }

        size_t success_count = 0;
        size_t fail_count = 0;

        for (size_t i = 0; i < queue.count; i++) {
            if (g_shutdown_requested) {
                printf("\n[!] Batch download aborted by user.\n");
                break;
            }

            if (!config.quiet) {
                printf("\n=========================================================================\n");
                printf(" [Batch %zu/%zu] %s\n", i + 1, queue.count, queue.entries[i].url);
                printf("=========================================================================\n");
            }

            down_config_t item_config = config;
            item_config.custom_headers = clone_slist(config.custom_headers);
            snprintf(item_config.url, sizeof(item_config.url), "%s", queue.entries[i].url);

            if (queue.entries[i].output_name[0] != '\0') {
                snprintf(item_config.output_path, sizeof(item_config.output_path), "%s", queue.entries[i].output_name);
            } else if (queue.count > 1) {
                item_config.output_path[0] = '\0'; /* Auto-detect filename per entry */
            }

            if (queue.entries[i].checksum_spec[0] != '\0') {
                snprintf(item_config.checksum_spec, sizeof(item_config.checksum_spec), "%s", queue.entries[i].checksum_spec);
                checksum_parse_spec(queue.entries[i].checksum_spec, item_config.checksum_algo,
                                    sizeof(item_config.checksum_algo), item_config.expected_checksum,
                                    sizeof(item_config.expected_checksum));
            }

            int ret = execute_download(&item_config);
            config_cleanup(&item_config);

            if (ret == 0) {
                success_count++;
            } else {
                fail_count++;
            }
        }

        if (!config.quiet) {
            printf("\n─────────────────────────────────────────────────────────────────────────\n");
            printf(" Batch Complete: %zu succeeded, %zu failed (Total: %zu)\n",
                   success_count, fail_count, queue.count);
            printf("─────────────────────────────────────────────────────────────────────────\n");
        }

        batch_queue_free(&queue);
        final_status = (fail_count == 0 && !g_shutdown_requested) ? 0 : 1;
    } else {
        /* Single download execution */
        int ret = execute_download(&config);
        final_status = (ret == 0) ? 0 : (ret == 2 ? 2 : 1);
    }

    config_cleanup(&config);
    curl_global_cleanup();
    return final_status;
}
