#include "swarm.h"
#include "telemetry.h"
#include "checksum.h"
#include <sys/ioctl.h>
#include <sys/time.h>
#include <math.h>
#include <unistd.h>

#define SWARM_REFRESH_US 35000 /* 35 ms = ~28.5 Hz refresh */
#define SWARM_SPEED_SAMPLES 12

typedef struct {
    uint64_t timestamp_us;
    uint64_t bytes;
} swarm_speed_sample_t;

typedef struct {
    int slot_id;
    size_t file_index;             /* 1-based (e.g. 1 of 5) */
    char display_name[128];
    char url[2048];
    down_telemetry_t telem;
    down_config_t slot_config;
    _Atomic bool active;
    _Atomic bool done;
    _Atomic int exit_code;
    pthread_t thread;

    /* Speed sliding window */
    swarm_speed_sample_t samples[SWARM_SPEED_SAMPLES];
    int sample_head;
    int sample_count;
    double instant_speed;
} swarm_slot_t;

typedef struct {
    const down_config_t *base_config;
    batch_queue_t *queue;
    int max_concurrent;
    swarm_slot_t *slots;
    pthread_mutex_t queue_mutex;
    size_t next_queue_idx;
    _Atomic size_t active_count;
    _Atomic size_t completed_count;
    _Atomic size_t failed_count;
    _Atomic uint64_t finished_bytes;
    _Atomic uint64_t finished_total_size;
    _Atomic bool stop_requested;
    uint64_t start_time_us;
} swarm_coordinator_t;

typedef struct {
    swarm_coordinator_t *coord;
    int slot_id;
} swarm_worker_arg_t;

static void *swarm_slot_worker_thread(void *arg) {
    swarm_worker_arg_t *warg = (swarm_worker_arg_t *)arg;
    swarm_coordinator_t *coord = warg->coord;
    int slot_id = warg->slot_id;
    free(warg);

    swarm_slot_t *slot = &coord->slots[slot_id];

    while (!g_shutdown_requested && !atomic_load(&coord->stop_requested)) {
        size_t q_idx = 0;
        pthread_mutex_lock(&coord->queue_mutex);
        if (coord->next_queue_idx >= coord->queue->count) {
            pthread_mutex_unlock(&coord->queue_mutex);
            break;
        }
        q_idx = coord->next_queue_idx++;
        pthread_mutex_unlock(&coord->queue_mutex);

        const batch_entry_t *entry = &coord->queue->entries[q_idx];

        /* Setup slot config */
        memset(&slot->slot_config, 0, sizeof(slot->slot_config));
        slot->slot_config = *coord->base_config;
        batch_queue_init(&slot->slot_config.queue); /* Does not own coordinator's queue */
        slot->slot_config.custom_headers = clone_slist(coord->base_config->custom_headers);
        snprintf(slot->slot_config.url, sizeof(slot->slot_config.url), "%s", entry->url);
        snprintf(slot->url, sizeof(slot->url), "%s", entry->url);

        if (entry->output_name[0] != '\0') {
            snprintf(slot->slot_config.output_path, sizeof(slot->slot_config.output_path), "%s", entry->output_name);
        } else {
            slot->slot_config.output_path[0] = '\0';
        }

        if (entry->checksum_spec[0] != '\0') {
            snprintf(slot->slot_config.checksum_spec, sizeof(slot->slot_config.checksum_spec), "%s", entry->checksum_spec);
            checksum_parse_spec(entry->checksum_spec, slot->slot_config.checksum_algo,
                                sizeof(slot->slot_config.checksum_algo),
                                slot->slot_config.expected_checksum,
                                sizeof(slot->slot_config.expected_checksum));
        }

        /* Extract filename for display */
        const char *base = strrchr(entry->url, '/');
        base = (base && *(base + 1)) ? (base + 1) : entry->url;
        snprintf(slot->display_name, sizeof(slot->display_name), "%.127s", base);
        char *qmark = strchr(slot->display_name, '?');
        if (qmark) *qmark = '\0';
        if (slot->display_name[0] == '\0') {
            snprintf(slot->display_name, sizeof(slot->display_name), "file_%zu", q_idx + 1);
        }

        slot->file_index = q_idx + 1;
        slot->sample_head = 0;
        slot->sample_count = 0;
        slot->instant_speed = 0.0;
        slot->exit_code = 0;
        slot->done = false;
        telemetry_init(&slot->telem, 0, 0, true);
        atomic_store(&slot->active, true);
        atomic_fetch_add(&coord->active_count, 1);

        /* Execute download in swarm mode */
        int res = down_execute_single(&slot->slot_config, &slot->telem, true);
        slot->exit_code = res;

        atomic_store(&slot->active, false);
        slot->done = true;
        atomic_fetch_sub(&coord->active_count, 1);

        uint64_t file_bytes = atomic_load(&slot->telem.downloaded_bytes);
        uint64_t file_total = atomic_load(&slot->telem.total_size);
        atomic_fetch_add(&coord->finished_bytes, file_bytes);
        if (file_total > 0) {
            atomic_fetch_add(&coord->finished_total_size, file_total);
        } else {
            atomic_fetch_add(&coord->finished_total_size, file_bytes);
        }

        if (res == 0) {
            atomic_fetch_add(&coord->completed_count, 1);
        } else {
            atomic_fetch_add(&coord->failed_count, 1);
        }

        config_cleanup(&slot->slot_config);
    }

    return NULL;
}

static void *swarm_telemetry_thread_fn(void *arg) {
    swarm_coordinator_t *coord = (swarm_coordinator_t *)arg;
    bool is_tty = isatty(STDOUT_FILENO);
    bool color = is_tty && !getenv("NO_COLOR");
    const char *term = getenv("TERM");
    if (term && strcmp(term, "dumb") == 0) color = false;

    int last_rendered_lines = 0;
    uint64_t last_log_time = current_time_micros();

    static const char *sub_blocks[] = {"", "▏", "▎", "▍", "▌", "▋", "▊", "▉"};

    while (!atomic_load(&coord->stop_requested)) {
        usleep(SWARM_REFRESH_US);
        uint64_t now = current_time_micros();

        /* Update speed samples for all active slots */
        double agg_speed = 0.0;
        uint64_t active_downloaded = 0;
        uint64_t active_total = 0;

        for (int i = 0; i < coord->max_concurrent; i++) {
            swarm_slot_t *slot = &coord->slots[i];
            if (atomic_load(&slot->active)) {
                uint64_t cur = atomic_load(&slot->telem.downloaded_bytes);
                uint64_t tot = atomic_load(&slot->telem.total_size);
                active_downloaded += cur;
                active_total += tot;

                /* Record sample in ring buffer */
                slot->samples[slot->sample_head].timestamp_us = now;
                slot->samples[slot->sample_head].bytes = cur;
                slot->sample_head = (slot->sample_head + 1) % SWARM_SPEED_SAMPLES;
                if (slot->sample_count < SWARM_SPEED_SAMPLES) {
                    slot->sample_count++;
                }

                int ref_idx = (slot->sample_head - slot->sample_count + SWARM_SPEED_SAMPLES) % SWARM_SPEED_SAMPLES;
                uint64_t dt_us = now - slot->samples[ref_idx].timestamp_us;
                uint64_t delta_bytes = (cur >= slot->samples[ref_idx].bytes) ? (cur - slot->samples[ref_idx].bytes) : 0;
                double dt = (double)dt_us / 1000000.0;
                slot->instant_speed = (dt > 0.02) ? ((double)delta_bytes / dt) : 0.0;
                agg_speed += slot->instant_speed;
            } else {
                slot->instant_speed = 0.0;
            }
        }

        if (is_tty) {
            int term_cols = 80;
            struct winsize ws;
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 10) {
                term_cols = ws.ws_col;
            }

            /* Move cursor back up if we rendered previous frame */
            if (last_rendered_lines > 0) {
                printf("\r\033[%dA", last_rendered_lines);
            }

            int current_rendered_lines = 0;

            const char *dim   = color ? "\033[38;5;240m" : "";
            const char *lbl   = color ? "\033[38;5;246m" : "";
            const char *val   = color ? "\033[1;37m" : "";
            const char *cyan  = color ? "\033[1;38;5;45m" : "";
            const char *green = color ? "\033[1;38;5;48m" : "";
            const char *yel   = color ? "\033[38;5;214m" : "";
            const char *pur   = color ? "\033[38;5;141m" : "";
            const char *rst   = color ? "\033[0m" : "";

            size_t active_c = atomic_load(&coord->active_count);
            size_t completed_c = atomic_load(&coord->completed_count);
            size_t total_q = coord->queue->count;
            size_t remaining_q = (total_q > (completed_c + active_c)) ? (total_q - completed_c - active_c) : 0;

            /* Line 1: Header */
            printf("\r\033[2K%s──%s %s⚡ down swarm • %zu active (%zu queued, %zu done)%s %s──────────────────%s\n",
                   dim, rst, cyan, active_c, remaining_q, completed_c, rst, dim, rst);
            current_rendered_lines++;

            /* Render each slot */
            for (int i = 0; i < coord->max_concurrent; i++) {
                swarm_slot_t *slot = &coord->slots[i];
                if (atomic_load(&slot->active)) {
                    uint64_t cur = atomic_load(&slot->telem.downloaded_bytes);
                    uint64_t tot = atomic_load(&slot->telem.total_size);
                    int conn = atomic_load(&slot->telem.active_connections);
                    double pct = (tot > 0) ? ((double)cur / (double)tot) * 100.0 : 0.0;
                    if (pct > 100.0) pct = 100.0;

                    uint64_t rem_b = (tot > cur) ? (tot - cur) : 0;
                    uint64_t eta_s = (slot->instant_speed > 512.0) ? (uint64_t)(rem_b / slot->instant_speed) : 0;

                    char cur_s[32], tot_s[32], raw_spd[32], eta_s_str[32];
                    format_bytes(cur, cur_s, sizeof(cur_s));
                    format_bytes(tot, tot_s, sizeof(tot_s));
                    if (slot->instant_speed >= 1024.0 * 1024.0) {
                        snprintf(raw_spd, sizeof(raw_spd), "%.2f MB/s", slot->instant_speed / (1024.0 * 1024.0));
                    } else if (slot->instant_speed >= 1024.0) {
                        snprintf(raw_spd, sizeof(raw_spd), "%.1f KB/s", slot->instant_speed / 1024.0);
                    } else {
                        snprintf(raw_spd, sizeof(raw_spd), "%.0f B/s", slot->instant_speed);
                    }

                    if (slot->instant_speed > 512.0) {
                        if (eta_s >= 3600) {
                            snprintf(eta_s_str, sizeof(eta_s_str), "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64,
                                     eta_s / 3600, (eta_s % 3600) / 60, eta_s % 60);
                        } else {
                            snprintf(eta_s_str, sizeof(eta_s_str), "%02" PRIu64 ":%02" PRIu64,
                                     eta_s / 60, eta_s % 60);
                        }
                    } else {
                        snprintf(eta_s_str, sizeof(eta_s_str), "--:--");
                    }

                    /* Adaptive bar width */
                    int bar_w = (term_cols >= 95) ? 14 : ((term_cols >= 80) ? 8 : 0);
                    char bbuf[64] = {0};
                    char ebuf[64] = {0};

                    if (bar_w > 0 && tot > 0) {
                        double prog = ((double)cur / (double)tot);
                        if (prog < 0.0) prog = 0.0;
                        if (prog > 1.0) prog = 1.0;

                        double tu = prog * (double)bar_w;
                        int fb = (int)tu;
                        int fi = (int)((tu - (double)fb) * 8.0);
                        if (fi > 7) fi = 7;
                        bool hf = (fi > 0);
                        int eb = bar_w - fb - (hf ? 1 : 0);
                        if (eb < 0) eb = 0;

                        size_t bp = 0;
                        for (int k = 0; k < fb && bp + 4 < sizeof(bbuf); k++) {
                            bp += (size_t)snprintf(bbuf + bp, sizeof(bbuf) - bp, "█");
                        }
                        if (hf && bp + 8 < sizeof(bbuf)) {
                            bp += (size_t)snprintf(bbuf + bp, sizeof(bbuf) - bp, "%s", sub_blocks[fi]);
                        }

                        size_t ep = 0;
                        for (int k = 0; k < eb && ep + 4 < sizeof(ebuf); k++) {
                            ep += (size_t)snprintf(ebuf + ep, sizeof(ebuf) - ep, "░");
                        }
                    }

                    char fn_pad[20];
                    snprintf(fn_pad, sizeof(fn_pad), "%-18.18s", slot->display_name);

                    if (term_cols >= 95 && bar_w > 0) {
                        printf("\r\033[2K  %s[%zu/%zu]%s %s%s%s %s%5.1f%%%s %s▕\033[38;5;39m%s\033[38;5;237m%s%s▏%s %s%s%s %s/%s %s%s%s %s•%s %s%s%s %s•%s %sETA %s%s%s %s(%d conn)%s\n",
                               lbl, slot->file_index, total_q, rst,
                               val, fn_pad, rst,
                               cyan, pct, rst,
                               dim, bbuf, ebuf, dim, rst,
                               val, cur_s, rst, dim, rst, lbl, tot_s, rst,
                               dim, rst, green, raw_spd, rst,
                               dim, rst, lbl, yel, eta_s_str, rst,
                               pur, conn, rst);
                    } else if (term_cols >= 75) {
                        printf("\r\033[2K  %s[%zu/%zu]%s %s%s%s %s%5.1f%%%s %s%s%s %s•%s %s%s%s\n",
                               lbl, slot->file_index, total_q, rst,
                               val, fn_pad, rst,
                               cyan, pct, rst,
                               val, cur_s, rst,
                               dim, rst, green, raw_spd, rst);
                    } else {
                        printf("\r\033[2K  %s[%zu]%s %s%-12.12s%s %s%5.1f%%%s %s%s%s\n",
                               lbl, slot->file_index, rst,
                               val, slot->display_name, rst,
                               cyan, pct, rst,
                               green, raw_spd, rst);
                    }
                    current_rendered_lines++;
                } else if (remaining_q > 0) {
                    printf("\r\033[2K  %s[--] (waiting for next queue slot...)%s\n", dim, rst);
                    current_rendered_lines++;
                }
            }

            /* Line 3: Aggregate status line */
            uint64_t agg_bytes = atomic_load(&coord->finished_bytes) + active_downloaded;
            uint64_t agg_tot = atomic_load(&coord->finished_total_size) + active_total;
            double agg_pct = (agg_tot > 0) ? ((double)agg_bytes / (double)agg_tot) * 100.0 : 0.0;
            if (agg_pct > 100.0) agg_pct = 100.0;

            uint64_t agg_rem_b = (agg_tot > agg_bytes) ? (agg_tot - agg_bytes) : 0;
            uint64_t agg_eta_s = (agg_speed > 512.0) ? (uint64_t)(agg_rem_b / agg_speed) : 0;

            char agg_cur_s[32], agg_tot_s[32], agg_spd_s[32], agg_eta_str[32];
            format_bytes(agg_bytes, agg_cur_s, sizeof(agg_cur_s));
            format_bytes(agg_tot, agg_tot_s, sizeof(agg_tot_s));
            if (agg_speed >= 1024.0 * 1024.0) {
                snprintf(agg_spd_s, sizeof(agg_spd_s), "%.2f MB/s", agg_speed / (1024.0 * 1024.0));
            } else if (agg_speed >= 1024.0) {
                snprintf(agg_spd_s, sizeof(agg_spd_s), "%.1f KB/s", agg_speed / 1024.0);
            } else {
                snprintf(agg_spd_s, sizeof(agg_spd_s), "%.0f B/s", agg_speed);
            }

            if (agg_speed > 512.0) {
                if (agg_eta_s >= 3600) {
                    snprintf(agg_eta_str, sizeof(agg_eta_str), "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64,
                             agg_eta_s / 3600, (agg_eta_s % 3600) / 60, agg_eta_s % 60);
                } else {
                    snprintf(agg_eta_str, sizeof(agg_eta_str), "%02" PRIu64 ":%02" PRIu64,
                             agg_eta_s / 60, agg_eta_s % 60);
                }
            } else {
                snprintf(agg_eta_str, sizeof(agg_eta_str), "--:--");
            }

            printf("\r\033[2K  %s⚡%s %sAggregate:%s %s%s%s %s•%s %s%s%s %s/%s %s%s%s (%s%.1f%%%s) %s•%s %sETA %s%s%s\n",
                   cyan, rst, lbl, rst, green, agg_spd_s, rst,
                   dim, rst, val, agg_cur_s, rst, dim, rst, lbl, agg_tot_s, rst,
                   cyan, agg_pct, rst,
                   dim, rst, lbl, yel, agg_eta_str, rst);
            current_rendered_lines++;

            /* Line 4: Footer */
            printf("\r\033[2K%s─────────────────────────────────────────────────────────────────%s\n", dim, rst);
            current_rendered_lines++;

            last_rendered_lines = current_rendered_lines;
            fflush(stdout);
        } else {
            /* Non-interactive stream log every 1s */
            if (now - last_log_time >= 1000000ULL) {
                uint64_t agg_bytes = atomic_load(&coord->finished_bytes) + active_downloaded;
                char agg_cur_s[32], agg_spd_s[32];
                format_bytes(agg_bytes, agg_cur_s, sizeof(agg_cur_s));
                if (agg_speed >= 1024.0 * 1024.0) {
                    snprintf(agg_spd_s, sizeof(agg_spd_s), "%.2f MB/s", agg_speed / (1024.0 * 1024.0));
                } else {
                    snprintf(agg_spd_s, sizeof(agg_spd_s), "%.1f KB/s", agg_speed / 1024.0);
                }
                printf("[down-swarm] %zu/%zu completed | %zu active | Speed: %s | Total: %s\n",
                       atomic_load(&coord->completed_count), coord->queue->count,
                       atomic_load(&coord->active_count), agg_spd_s, agg_cur_s);
                fflush(stdout);
                last_log_time = now;
            }
        }
    }

    if (is_tty && last_rendered_lines > 0) {
        printf("\r\033[%dA\033[0J", last_rendered_lines);
        fflush(stdout);
    }

    return NULL;
}

int swarm_execute(const down_config_t *base_config, batch_queue_t *queue, int max_concurrent) {
    if (!base_config || !queue || queue->count == 0) return 0;

    if (max_concurrent < 1) max_concurrent = 1;
    if (max_concurrent > MAX_CONCURRENT_DOWNLOADS) max_concurrent = MAX_CONCURRENT_DOWNLOADS;
    if ((size_t)max_concurrent > queue->count) max_concurrent = (int)queue->count;

    swarm_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));
    coord.base_config = base_config;
    coord.queue = queue;
    coord.max_concurrent = max_concurrent;
    coord.start_time_us = current_time_micros();
    pthread_mutex_init(&coord.queue_mutex, NULL);

    coord.slots = (swarm_slot_t *)calloc((size_t)max_concurrent, sizeof(swarm_slot_t));
    if (!coord.slots) {
        pthread_mutex_destroy(&coord.queue_mutex);
        return -1;
    }

    for (int i = 0; i < max_concurrent; i++) {
        coord.slots[i].slot_id = i;
    }

    /* Print Swarm Start Banner */
    bool color = isatty(STDOUT_FILENO) && !getenv("NO_COLOR");
    const char *dim   = color ? "\033[38;5;240m" : "";
    const char *lbl   = color ? "\033[38;5;246m" : "";
    const char *bold  = color ? "\033[1;37m" : "";
    const char *cyan  = color ? "\033[1;38;5;45m" : "";
    const char *pur   = color ? "\033[38;5;141m" : "";
    const char *reset = color ? "\033[0m" : "";

    if (!base_config->quiet) {
        printf("\n%s──%s %s⚡ down swarm v%s%s %s────────────────────────────────────────────%s\n",
               dim, reset, cyan, DOWN_VERSION, reset, dim, reset);
        printf("  %sQueue Size%s   : %s%zu files%s\n", lbl, reset, bold, queue->count, reset);
        printf("  %sConcurrency%s  : %s%d files in parallel%s %s(%d connections per file)%s\n",
               lbl, reset, cyan, max_concurrent, reset, pur, base_config->num_workers, reset);
        printf("  %sDestination%s  : %s%s%s\n", lbl, reset, bold,
               base_config->output_dir[0] ? base_config->output_dir : ".", reset);
        printf("%s─────────────────────────────────────────────────────────────────%s\n\n", dim, reset);
        fflush(stdout);
    }

    /* Start Swarm Telemetry Thread */
    pthread_t telemetry_thread = 0;
    if (!base_config->quiet) {
        pthread_create(&telemetry_thread, NULL, swarm_telemetry_thread_fn, &coord);
    }

    /* Spawn slot worker threads */
    for (int i = 0; i < max_concurrent; i++) {
        swarm_worker_arg_t *warg = (swarm_worker_arg_t *)malloc(sizeof(*warg));
        warg->coord = &coord;
        warg->slot_id = i;
        pthread_create(&coord.slots[i].thread, NULL, swarm_slot_worker_thread, warg);
    }

    /* Wait for all slot workers to complete */
    for (int i = 0; i < max_concurrent; i++) {
        pthread_join(coord.slots[i].thread, NULL);
    }

    /* Stop telemetry thread */
    atomic_store(&coord.stop_requested, true);
    if (telemetry_thread) {
        pthread_join(telemetry_thread, NULL);
    }

    /* Compute final totals */
    uint64_t end_time = current_time_micros();
    double total_sec = (double)(end_time - coord.start_time_us) / 1000000.0;
    if (total_sec < 0.001) total_sec = 0.001;

    uint64_t total_bytes = atomic_load(&coord.finished_bytes);
    double avg_speed = (double)total_bytes / total_sec;

    char size_str[32], speed_str[32], dur_str[32], bytes_str[32];
    format_bytes(total_bytes, size_str, sizeof(size_str));
    format_speed(avg_speed, speed_str, sizeof(speed_str));
    format_duration((uint64_t)total_sec, dur_str, sizeof(dur_str));
    format_number_commas(total_bytes, bytes_str, sizeof(bytes_str));

    size_t succeeded = atomic_load(&coord.completed_count);
    size_t failed = atomic_load(&coord.failed_count);

    if (!base_config->quiet) {
        const char *grn = color ? "\033[1;38;5;48m" : "";
        const char *red = color ? "\033[1;31m" : "";

        if (g_shutdown_requested) {
            const char *yel = color ? "\033[1;38;5;214m" : "";
            printf("\n%s──%s %s⏸  Swarm Paused by User%s %s───────────────────────────────────────%s\n",
                   dim, reset, yel, reset, dim, reset);
            printf("  %sStatus%s          : %s%zu completed, %zu remaining%s (state preserved)\n",
                   lbl, reset, yel, succeeded, (queue->count > succeeded ? queue->count - succeeded : 0), reset);
            printf("  %sResume With%s     : %sdown -c%s (or run interactive 'down')\n",
                   lbl, reset, cyan, reset);
            printf("%s─────────────────────────────────────────────────────────────────%s\n\n", dim, reset);
        } else if (failed == 0) {
            printf("\n%s──%s %s✔ Swarm Complete%s %s─────────────────────────────────────────────%s\n",
                   dim, reset, grn, reset, dim, reset);
            printf("  %sFiles Downloaded%s: %s%zu / %zu%s %s(all succeeded)%s\n",
                   lbl, reset, bold, succeeded, queue->count, reset, grn, reset);
            printf("  %sTotal Volume%s    : %s%s%s %s(%s bytes)%s\n",
                   lbl, reset, bold, size_str, reset, dim, bytes_str, reset);
            printf("  %sTime Elapsed%s    : %s%s%s  %s•%s  %sAverage Speed:%s %s%s%s\n",
                   lbl, reset, bold, dur_str, reset, dim, reset, lbl, reset, grn, speed_str, reset);
            printf("  %sDestination%s     : %s%s%s\n",
                   lbl, reset, bold, base_config->output_dir[0] ? base_config->output_dir : ".", reset);
            printf("%s─────────────────────────────────────────────────────────────────%s\n\n", dim, reset);
        } else {
            printf("\n%s──%s %s⚠ Swarm Finished with Errors%s %s─────────────────────────────────%s\n",
                   dim, reset, red, reset, dim, reset);
            printf("  %sResults%s         : %s%zu succeeded%s, %s%zu failed%s (Total: %zu)\n",
                   lbl, reset, grn, succeeded, reset, red, failed, reset, queue->count);
            printf("  %sTotal Volume%s    : %s%s%s\n", lbl, reset, bold, size_str, reset);
            printf("  %sTime Elapsed%s    : %s%s%s\n", lbl, reset, bold, dur_str, reset);
            printf("%s─────────────────────────────────────────────────────────────────%s\n\n", dim, reset);
        }
        fflush(stdout);
    }

    free(coord.slots);
    pthread_mutex_destroy(&coord.queue_mutex);

    if (g_shutdown_requested) return -2;
    return (failed == 0) ? 0 : 1;
}
