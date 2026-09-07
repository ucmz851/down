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
#include "swarm.h"

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

struct curl_slist *clone_slist(const struct curl_slist *src) {
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
    const char *dim    = color ? "\033[38;5;240m" : "";
    const char *lbl    = color ? "\033[38;5;246m" : "";
    const char *bold   = color ? "\033[1;37m" : "";
    const char *cyan   = color ? "\033[1;38;5;45m" : "";
    const char *green  = color ? "\033[1;38;5;48m" : "";
    const char *yellow = color ? "\033[1;38;5;214m" : "";
    const char *pur    = color ? "\033[38;5;141m" : "";
    const char *reset  = color ? "\033[0m" : "";

    char size_str[32];
    char bytes_str[32];
    if (probe->length_known) {
        format_bytes(probe->content_length, size_str, sizeof(size_str));
        format_number_commas(probe->content_length, bytes_str, sizeof(bytes_str));
    } else {
        snprintf(size_str, sizeof(size_str), "unknown");
        snprintf(bytes_str, sizeof(bytes_str), "unknown");
    }

    printf("\n%s──%s %s⚡ down v%s%s %s──────────────────────────────────────────────────%s\n",
           dim, reset, cyan, DOWN_VERSION, reset, dim, reset);
    printf("  %sTarget%s   : %s%s%s\n", lbl, reset, bold, config->output_path, reset);
    if (probe->length_known) {
        printf("  %sSize%s     : %s%s%s %s(%s bytes)%s\n", lbl, reset, bold, size_str, reset, dim, bytes_str, reset);
    } else {
        printf("  %sSize%s     : %s%s%s\n", lbl, reset, bold, size_str, reset);
    }
    printf("  %sSource%s   : %s%s%s\n", lbl, reset, dim, config->url, reset);

    if (probe->supports_range && probe->length_known && !config->force_single_stream && config->num_workers > 1) {
        printf("  %sEngine%s   : %s%s%s %s(%d workers • %u KB chunk size)%s\n",
               lbl, reset, cyan, config->use_static ? "Static Partitioning" : "Dynamic Work-Stealing", reset,
               pur, config->num_workers, config->chunk_size / 1024, reset);
    } else {
        printf("  %sEngine%s   : %sSingle-Stream Sequential%s\n", lbl, reset, cyan, reset);
    }

    if (config->http_version == CURL_HTTP_VERSION_3) {
        printf("  %sProtocol%s : %sHTTP/3 (QUIC) with fallback%s\n", lbl, reset, cyan, reset);
    } else if (config->http_version == CURL_HTTP_VERSION_3ONLY) {
        printf("  %sProtocol%s : %sHTTP/3 (QUIC only)%s\n", lbl, reset, cyan, reset);
    }

    if (config->aws_sigv4_enabled) {
        printf("  %sAuth%s     : %sAWS SigV4%s %s(Region: %s, Service: %s)%s\n",
               lbl, reset, cyan, reset, dim,
               config->aws_region[0] ? config->aws_region : "us-east-1",
               config->aws_service[0] ? config->aws_service : "s3", reset);
    }

    if (config->expected_checksum[0] != '\0') {
        printf("  %sChecksum%s : %s%s:%s%s\n", lbl, reset, green, config->checksum_algo, config->expected_checksum, reset);
    }

    if (!config->no_fallocate && probe->length_known) {
#if defined(__APPLE__)
        printf("  %sStorage%s  : Pre-allocated contiguous blocks %s(F_PREALLOCATE)%s\n", lbl, reset, dim, reset);
#else
        printf("  %sStorage%s  : Pre-allocated contiguous blocks %s(posix_fallocate)%s\n", lbl, reset, dim, reset);
#endif
    }

    if (config->max_speed_limit > 0) {
        char limit_str[32];
        format_bytes(config->max_speed_limit, limit_str, sizeof(limit_str));
        printf("  %sSpeed%s    : %sRate-limited to %s/s%s\n", lbl, reset, yellow, limit_str, reset);
    }

    if (config->insecure) {
        printf("  %sSecurity%s : %sTLS verification disabled (insecure mode)%s\n", lbl, reset, yellow, reset);
    }

    if (is_resumed && completed_chunks > 0) {
        char init_str[32];
        format_bytes(initial_bytes, init_str, sizeof(init_str));
        double percent = probe->content_length > 0 ? ((double)initial_bytes / (double)probe->content_length) * 100.0 : 0.0;
        printf("  %sStatus%s   : %sResuming from chunk %u/%u (%s / %.1f%% completed)%s\n",
               lbl, reset, yellow, completed_chunks, total_chunks, init_str, percent, reset);
    }
    printf("%s─────────────────────────────────────────────────────────────────%s\n\n", dim, reset);
    fflush(stdout);
}

int down_execute_single(down_config_t *config, down_telemetry_t *external_telem, bool is_swarm) {
    if (!config) return -1;

    /* S3 URL handling and auth */
    if (s3_is_s3_url(config->url)) {
        if (s3_transform_url(config) != 0) {
            return -1;
        }
    }
    s3_init_auth(config);

    /* Probe remote resource */
    if (!config->quiet && !is_swarm) {
        printf("[*] Probing remote resource: %s\n", config->url);
        fflush(stdout);
    }

    down_probe_t probe;
    if (probe_url(config, &probe) != 0) {
        if (!is_swarm) {
            fprintf(stderr, "[!] Failed to probe remote URL\n");
        }
        return -1;
    }

    if (external_telem) {
        atomic_store(&external_telem->total_size, probe.content_length);
    }

    /* Resolve destination filename and directory */
    char resolved_filename[1024];
    if (config->output_path[0] != '\0') {
        snprintf(resolved_filename, sizeof(resolved_filename), "%s", config->output_path);
    } else {
        snprintf(resolved_filename, sizeof(resolved_filename), "%s", probe.suggested_filename);
    }

    /* Strip leading "./" to ensure clean canonical filenames */
    const char *clean_fname = resolved_filename;
    while (clean_fname[0] == '.' && clean_fname[1] == '/') {
        clean_fname += 2;
    }

    if (config->output_dir[0] != '\0' && strcmp(config->output_dir, ".") != 0 && strcmp(config->output_dir, "./") != 0) {
        if (make_directory_recursive(config->output_dir) != 0) {
            if (!is_swarm) {
                fprintf(stderr, "[!] Error: failed to create destination directory '%s': %s\n",
                        config->output_dir, strerror(errno));
            }
            return -1;
        }
        size_t dlen = strlen(config->output_dir);
        int rem = (int)sizeof(config->output_path) - (int)dlen - 2;
        if (rem < 1) rem = 1;
        if (config->output_dir[dlen - 1] == '/') {
            snprintf(config->output_path, sizeof(config->output_path), "%s%.*s",
                     config->output_dir, rem, clean_fname);
        } else {
            snprintf(config->output_path, sizeof(config->output_path), "%s/%.*s",
                     config->output_dir, rem, clean_fname);
        }
    } else {
        snprintf(config->output_path, sizeof(config->output_path), "%s", clean_fname);
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
        if (!is_swarm) {
            fprintf(stderr, "[!] Storage initialization failed\n");
        }
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
            if (!is_swarm) {
                fprintf(stderr, "[!] Failed to initialize .down control file\n");
            }
            storage_close(&storage);
            return -1;
        }

        uint32_t completed_chunks = atomic_load(&meta.hdr->completed_chunks);
        uint64_t initial_bytes = (uint64_t)completed_chunks * config->chunk_size;
        if (initial_bytes > probe.content_length) initial_bytes = probe.content_length;

        if (completed_chunks >= meta.hdr->num_chunks) {
            if (!is_swarm) {
                printf("[+] File is already completely downloaded: %s\n", config->output_path);
            }
            meta_remove(&meta);
            storage_close(&storage);
            if (external_telem) {
                atomic_store(&external_telem->downloaded_bytes, probe.content_length);
                atomic_store(&external_telem->total_size, probe.content_length);
            }
            history_record_complete(config, probe.content_length);
            return 0;
        }

        /* Print clean download spec card */
        if (!is_swarm) {
            print_download_spec(config, &probe, meta.is_resumed, completed_chunks,
                                meta.hdr->num_chunks, initial_bytes);
        }

        down_telemetry_t local_telem;
        down_telemetry_t *telem_ptr = external_telem ? external_telem : &local_telem;
        telemetry_init(telem_ptr, probe.content_length, initial_bytes, is_swarm ? true : config->quiet);
        if (!is_swarm) {
            telemetry_start(telem_ptr);
        }

        down_scheduler_t scheduler;
        if (scheduler_init(&scheduler, &meta, probe.content_length, config->chunk_size,
                           config->num_workers, config->use_static) != 0) {
            if (!is_swarm) {
                fprintf(stderr, "[!] Scheduler initialization failed\n");
                telemetry_stop(telem_ptr);
            }
            meta_close(&meta);
            storage_close(&storage);
            return -1;
        }

        worker_context_t workers[MAX_NUM_WORKERS];
        workers_start(workers, config->num_workers, config, &storage, &scheduler, telem_ptr);

        /* Wait for workers to finish */
        workers_join(workers, config->num_workers);

        /* Stop telemetry and flush storage */
        if (!is_swarm) {
            telemetry_stop(telem_ptr);
        }
        storage_sync(&storage);

        bool all_done = (atomic_load(&meta.hdr->completed_chunks) >= meta.hdr->num_chunks);
        scheduler_destroy(&scheduler);

        if (all_done) {
            /* Checksum validation if requested */
            if (config->expected_checksum[0] != '\0') {
                if (!config->quiet && !is_swarm) {
                    printf("[*] Verifying file checksum (%s)...\n", config->checksum_algo);
                    fflush(stdout);
                }
                char actual_hex[256] = {0};
                int v_res = checksum_verify_file(config->output_path, config->checksum_algo,
                                                 config->expected_checksum, actual_hex, sizeof(actual_hex));
                if (v_res == 0) {
                    if (!config->quiet && !is_swarm) {
                        printf("[+] Checksum verified: %s: %s\n", config->checksum_algo, actual_hex);
                    }
                    meta_remove(&meta);
                    if (!is_swarm) telemetry_print_complete(telem_ptr, config->output_path);
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
                if (!is_swarm) telemetry_print_complete(telem_ptr, config->output_path);
                history_record_complete(config, probe.content_length);
                download_ret = 0;
            }
        } else if (g_shutdown_requested) {
            meta_sync(&meta, true);
            meta_close(&meta);
            uint64_t cur = atomic_load(&telem_ptr->downloaded_bytes);
            history_record_update(config, cur, probe.content_length, DOWN_STATUS_INTERRUPTED);
            if (!is_swarm) telemetry_print_paused(telem_ptr, config->output_path, meta_path, config->url);
            download_ret = -2;
        } else {
            meta_sync(&meta, true);
            meta_close(&meta);
            uint64_t cur = atomic_load(&telem_ptr->downloaded_bytes);
            history_record_update(config, cur, probe.content_length, DOWN_STATUS_INTERRUPTED);
            if (!is_swarm) {
                fprintf(stderr, "\n[!] Download incomplete due to network transfer error. Resume with -c.\n");
            }
            download_ret = -1;
        }
    } else {
        /* Single-stream download path */
        if (!is_swarm) {
            print_download_spec(config, &probe, false, 0, 0, 0);
        }

        down_telemetry_t local_telem;
        down_telemetry_t *telem_ptr = external_telem ? external_telem : &local_telem;
        telemetry_init(telem_ptr, probe.content_length, 0, is_swarm ? true : config->quiet);
        if (!is_swarm) {
            telemetry_start(telem_ptr);
        }

        int res = worker_download_single_stream(config, &storage, telem_ptr, probe.content_length);

        if (!is_swarm) {
            telemetry_stop(telem_ptr);
        }
        storage_sync(&storage);

        if (res == 0 && !g_shutdown_requested) {
            /* Checksum validation if requested */
            if (config->expected_checksum[0] != '\0') {
                if (!config->quiet && !is_swarm) {
                    printf("[*] Verifying file checksum (%s)...\n", config->checksum_algo);
                    fflush(stdout);
                }
                char actual_hex[256] = {0};
                int v_res = checksum_verify_file(config->output_path, config->checksum_algo,
                                                 config->expected_checksum, actual_hex, sizeof(actual_hex));
                if (v_res == 0) {
                    if (!config->quiet && !is_swarm) {
                        printf("[+] Checksum verified: %s: %s\n", config->checksum_algo, actual_hex);
                    }
                    if (!is_swarm) telemetry_print_complete(telem_ptr, config->output_path);
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
                if (!is_swarm) telemetry_print_complete(telem_ptr, config->output_path);
                history_record_complete(config, probe.content_length);
                download_ret = 0;
            }
        } else if (g_shutdown_requested) {
            uint64_t cur = atomic_load(&telem_ptr->downloaded_bytes);
            history_record_update(config, cur, probe.content_length, DOWN_STATUS_INTERRUPTED);
            if (!is_swarm) printf("\n[!] Single stream download aborted by user.\n");
            download_ret = -2;
        } else {
            uint64_t cur = atomic_load(&telem_ptr->downloaded_bytes);
            history_record_update(config, cur, probe.content_length, DOWN_STATUS_FAILED);
            if (!is_swarm) fprintf(stderr, "\n[!] Single stream download failed.\n");
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

    if (config.queue.count > 1) {
        final_status = swarm_execute(&config, &config.queue, config.max_concurrent_downloads);
    } else {
        if (config.queue.count == 1) {
            snprintf(config.url, sizeof(config.url), "%s", config.queue.entries[0].url);
            if (config.queue.entries[0].output_name[0] != '\0') {
                snprintf(config.output_path, sizeof(config.output_path), "%s", config.queue.entries[0].output_name);
            }
            if (config.queue.entries[0].checksum_spec[0] != '\0') {
                snprintf(config.checksum_spec, sizeof(config.checksum_spec), "%s", config.queue.entries[0].checksum_spec);
                checksum_parse_spec(config.checksum_spec, config.checksum_algo,
                                    sizeof(config.checksum_algo), config.expected_checksum,
                                    sizeof(config.expected_checksum));
            }
        }
        if (config.url[0] != '\0') {
            final_status = down_execute_single(&config, NULL, false);
        }
    }

    config_cleanup(&config);
    curl_global_cleanup();

    if (final_status == -2 || g_shutdown_requested) {
        return 130;
    }
    return (final_status == 0) ? 0 : (final_status == 2 ? 2 : 1);
}
