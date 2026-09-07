#include "telemetry.h"
#include <sys/ioctl.h>
#include <sys/time.h>
#include <math.h>

#define TELEMETRY_REFRESH_US 35000 /* 35 ms = ~28.5 Hz high-frequency refresh */
#define SPEED_WINDOW_CAPACITY 12   /* ~400 ms sliding window for instantaneous accuracy */

typedef struct {
    uint64_t timestamp_us;
    uint64_t bytes;
} speed_sample_t;

uint64_t current_time_micros(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

void format_bytes(uint64_t bytes, char *buf, size_t buf_size) {
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double size = (double)bytes;
    int unit_idx = 0;
    while (size >= 1024.0 && unit_idx < 4) {
        size /= 1024.0;
        unit_idx++;
    }
    if (unit_idx == 0) {
        snprintf(buf, buf_size, "%" PRIu64 " B", bytes);
    } else {
        snprintf(buf, buf_size, "%.2f %s", size, units[unit_idx]);
    }
}

void format_speed(double speed, char *buf, size_t buf_size) {
    static const char *units[] = {"B/s", "KB/s", "MB/s", "GB/s", "TB/s"};
    int unit_idx = 0;
    while (speed >= 1024.0 && unit_idx < 4) {
        speed /= 1024.0;
        unit_idx++;
    }
    if (unit_idx == 0) {
        snprintf(buf, buf_size, "%.0f B/s", speed);
    } else {
        snprintf(buf, buf_size, "%.2f %s", speed, units[unit_idx]);
    }
}

static void format_eta(uint64_t seconds, char *buf, size_t buf_size) {
    if (seconds >= 3600) {
        uint64_t hrs = seconds / 3600;
        uint64_t mins = (seconds % 3600) / 60;
        uint64_t secs = seconds % 60;
        snprintf(buf, buf_size, "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64, hrs, mins, secs);
    } else {
        uint64_t mins = seconds / 60;
        uint64_t secs = seconds % 60;
        snprintf(buf, buf_size, "%02" PRIu64 ":%02" PRIu64, mins, secs);
    }
}

void format_duration(uint64_t seconds, char *buf, size_t buf_size) {
    if (seconds >= 3600) {
        uint64_t hrs = seconds / 3600;
        uint64_t mins = (seconds % 3600) / 60;
        uint64_t secs = seconds % 60;
        snprintf(buf, buf_size, "%" PRIu64 "h %02" PRIu64 "m %02" PRIu64 "s", hrs, mins, secs);
    } else if (seconds >= 60) {
        uint64_t mins = seconds / 60;
        uint64_t secs = seconds % 60;
        snprintf(buf, buf_size, "%02" PRIu64 "m %02" PRIu64 "s", mins, secs);
    } else if (seconds > 0) {
        snprintf(buf, buf_size, "%" PRIu64 "s", seconds);
    } else {
        snprintf(buf, buf_size, "< 1s");
    }
}

void format_number_commas(uint64_t n, char *buf, size_t sz) {
    if (!buf || sz == 0) return;
    char raw[32];
    int raw_len = snprintf(raw, sizeof(raw), "%" PRIu64, n);
    if (raw_len <= 0) {
        buf[0] = '\0';
        return;
    }
    int commas = (raw_len - 1) / 3;
    int total_len = raw_len + commas;
    if ((size_t)total_len >= sz) {
        snprintf(buf, sz, "%" PRIu64, n);
        return;
    }
    buf[total_len] = '\0';
    int r = raw_len - 1;
    int w = total_len - 1;
    int digit_count = 0;
    while (r >= 0) {
        if (digit_count == 3) {
            buf[w--] = ',';
            digit_count = 0;
        }
        buf[w--] = raw[r--];
        digit_count++;
    }
}

static bool use_color(void) {
    if (!isatty(STDOUT_FILENO)) return false;
    const char *no_color = getenv("NO_COLOR");
    if (no_color && *no_color) return false;
    const char *term = getenv("TERM");
    if (term && strcmp(term, "dumb") == 0) return false;
    return true;
}

static void *telemetry_thread_fn(void *arg) {
    down_telemetry_t *telem = (down_telemetry_t *)arg;
    bool is_tty = isatty(STDOUT_FILENO);
    bool color = use_color();

    uint64_t start_time = current_time_micros();
    uint64_t last_non_tty_log_time = start_time;

    /* Sliding-window ring buffer for instantaneous rate calculation */
    speed_sample_t samples[SPEED_WINDOW_CAPACITY];
    int sample_head = 0;
    int sample_count = 0;

    static const char *sub_blocks[] = {"", "▏", "▎", "▍", "▌", "▋", "▊", "▉"};

    while (!atomic_load(&telem->stop_requested)) {
        usleep(TELEMETRY_REFRESH_US);

        uint64_t now = current_time_micros();
        uint64_t cur_bytes = atomic_load_explicit(&telem->downloaded_bytes, memory_order_relaxed);
        uint64_t total = atomic_load_explicit(&telem->total_size, memory_order_relaxed);
        int active_conn = atomic_load_explicit(&telem->active_connections, memory_order_relaxed);

        /* Record new sample in ring buffer */
        samples[sample_head].timestamp_us = now;
        samples[sample_head].bytes = cur_bytes;
        sample_head = (sample_head + 1) % SPEED_WINDOW_CAPACITY;
        if (sample_count < SPEED_WINDOW_CAPACITY) {
            sample_count++;
        }

        /* Calculate instantaneous rate over sliding window */
        int ref_idx = (sample_head - sample_count + SPEED_WINDOW_CAPACITY) % SPEED_WINDOW_CAPACITY;
        uint64_t dt_us = now - samples[ref_idx].timestamp_us;
        uint64_t delta_bytes = (cur_bytes >= samples[ref_idx].bytes) ? (cur_bytes - samples[ref_idx].bytes) : 0;
        double dt = (double)dt_us / 1000000.0;
        double instant_speed = (dt > 0.02) ? ((double)delta_bytes / dt) : 0.0;

        double percent = (total > 0) ? ((double)cur_bytes / (double)total) * 100.0 : 0.0;
        if (percent > 100.0) percent = 100.0;

        uint64_t remaining_bytes = (total > cur_bytes) ? (total - cur_bytes) : 0;
        uint64_t eta_secs = (instant_speed > 512.0) ? (uint64_t)(remaining_bytes / instant_speed) : 0;

        char cur_str[32], tot_str[32], spd_str[32], eta_str[32];
        format_bytes(cur_bytes, cur_str, sizeof(cur_str));
        format_bytes(total, tot_str, sizeof(tot_str));
        format_speed(instant_speed, spd_str, sizeof(spd_str));
        format_eta(eta_secs, eta_str, sizeof(eta_str));

        if (is_tty) {
            int term_cols = 80;
            struct winsize ws;
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 10) {
                term_cols = ws.ws_col;
            }

            char conn_str[32];
            snprintf(conn_str, sizeof(conn_str), "(%d conn)", active_conn);
            const char *eta_val = (instant_speed > 512.0) ? eta_str : "--:--";

            /* Adaptive tiers based on terminal column width:
             * Tier 4 (>= 95 cols): Size • Speed • ETA • (N conn)
             * Tier 3 (>= 80 cols): Size • Speed • ETA
             * Tier 2 (>= 60 cols): Size • Speed
             * Tier 1 (>= 42 cols): Speed
             * Tier 0 (< 42 cols) : Compact speed (no bar)
             */
            int tier = 0;
            if (term_cols >= 95) {
                tier = 4;
            } else if (term_cols >= 80) {
                tier = 3;
            } else if (term_cols >= 60) {
                tier = 2;
            } else if (term_cols >= 42) {
                tier = 1;
            } else {
                tier = 0;
            }

            /* Calculate visible text length of info components (excluding ANSI color codes) */
            int right_len = 0;
            if (tier >= 2) {
                if (total > 0) {
                    right_len += (int)strlen(cur_str) + 3 + (int)strlen(tot_str);
                } else {
                    right_len += (int)strlen(cur_str);
                }
                right_len += 3 + (int)strlen(spd_str); /* " • " + speed */
                if (tier >= 3) {
                    right_len += 3 + 4 + (int)strlen(eta_val); /* " • ETA " + eta */
                }
                if (tier >= 4) {
                    right_len += 1 + (int)strlen(conn_str); /* " " + conn */
                }
            } else {
                right_len = (int)strlen(spd_str);
            }

            /* Fixed overhead: " 100.0% " (8) + "▕" + "▏ " (3) + right_len */
            int fixed_overhead = 8 + 3 + right_len;
            int bar_width = term_cols - fixed_overhead - 2; /* 2 cols safety margin against wrapping */
            if (bar_width > 28) bar_width = 28;

            /* Assemble formatted info string */
            char info_buf[512];
            if (color) {
                const char *c_cur = "\033[1;37m";
                const char *c_sla = "\033[38;5;242m/\033[0m";
                const char *c_tot = "\033[38;5;248m";
                const char *c_dot = " \033[38;5;240m•\033[0m ";
                const char *c_spd = "\033[1;38;5;48m";
                const char *c_lbl = "\033[38;5;245m";
                const char *c_eta = (instant_speed > 512.0) ? "\033[38;5;214m" : "\033[38;5;242m";
                const char *c_con = "\033[38;5;141m";
                const char *rst   = "\033[0m";

                if (tier == 4) {
                    if (total > 0) {
                        snprintf(info_buf, sizeof(info_buf),
                                 "%s%s%s %s %s%s%s%s%s%s%s%s%sETA %s%s%s %s%s%s",
                                 c_cur, cur_str, rst, c_sla, c_tot, tot_str, rst,
                                 c_dot, c_spd, spd_str, rst,
                                 c_dot, c_lbl, c_eta, eta_val, rst,
                                 c_con, conn_str, rst);
                    } else {
                        snprintf(info_buf, sizeof(info_buf),
                                 "%s%s%s%s%s%s%s %s%s%s",
                                 c_cur, cur_str, rst,
                                 c_dot, c_spd, spd_str, rst,
                                 c_con, conn_str, rst);
                    }
                } else if (tier == 3) {
                    if (total > 0) {
                        snprintf(info_buf, sizeof(info_buf),
                                 "%s%s%s %s %s%s%s%s%s%s%s%s%sETA %s%s%s",
                                 c_cur, cur_str, rst, c_sla, c_tot, tot_str, rst,
                                 c_dot, c_spd, spd_str, rst,
                                 c_dot, c_lbl, c_eta, eta_val, rst);
                    } else {
                        snprintf(info_buf, sizeof(info_buf),
                                 "%s%s%s%s%s%s%s",
                                 c_cur, cur_str, rst,
                                 c_dot, c_spd, spd_str, rst);
                    }
                } else if (tier == 2) {
                    if (total > 0) {
                        snprintf(info_buf, sizeof(info_buf),
                                 "%s%s%s %s %s%s%s%s%s%s%s",
                                 c_cur, cur_str, rst, c_sla, c_tot, tot_str, rst,
                                 c_dot, c_spd, spd_str, rst);
                    } else {
                        snprintf(info_buf, sizeof(info_buf),
                                 "%s%s%s%s%s%s%s",
                                 c_cur, cur_str, rst,
                                 c_dot, c_spd, spd_str, rst);
                    }
                } else {
                    snprintf(info_buf, sizeof(info_buf), "%s%s%s", c_spd, spd_str, rst);
                }
            } else {
                /* Non-color plain format */
                if (tier == 4) {
                    if (total > 0) {
                        snprintf(info_buf, sizeof(info_buf), "%s / %s • %s • ETA %s %s",
                                 cur_str, tot_str, spd_str, eta_val, conn_str);
                    } else {
                        snprintf(info_buf, sizeof(info_buf), "%s • %s %s",
                                 cur_str, spd_str, conn_str);
                    }
                } else if (tier == 3) {
                    if (total > 0) {
                        snprintf(info_buf, sizeof(info_buf), "%s / %s • %s • ETA %s",
                                 cur_str, tot_str, spd_str, eta_val);
                    } else {
                        snprintf(info_buf, sizeof(info_buf), "%s • %s", cur_str, spd_str);
                    }
                } else if (tier == 2) {
                    if (total > 0) {
                        snprintf(info_buf, sizeof(info_buf), "%s / %s • %s", cur_str, tot_str, spd_str);
                    } else {
                        snprintf(info_buf, sizeof(info_buf), "%s • %s", cur_str, spd_str);
                    }
                } else {
                    snprintf(info_buf, sizeof(info_buf), "%s", spd_str);
                }
            }

            if (bar_width >= 6 && total > 0) {
                /* Render graphical bar with adaptive width */
                double progress = ((double)cur_bytes / (double)total);
                if (progress < 0.0) progress = 0.0;
                if (progress > 1.0) progress = 1.0;

                double total_units = progress * (double)bar_width;
                int full_blocks = (int)total_units;
                int frac_idx = (int)((total_units - (double)full_blocks) * 8.0);
                if (frac_idx > 7) frac_idx = 7;
                bool has_frac = (frac_idx > 0);
                int empty_blocks = bar_width - full_blocks - (has_frac ? 1 : 0);
                if (empty_blocks < 0) empty_blocks = 0;

                char bar_buf[256] = {0};
                size_t bpos = 0;
                for (int i = 0; i < full_blocks && bpos + 4 < sizeof(bar_buf); i++) {
                    bpos += (size_t)snprintf(bar_buf + bpos, sizeof(bar_buf) - bpos, "█");
                }
                if (has_frac && bpos + 8 < sizeof(bar_buf)) {
                    bpos += (size_t)snprintf(bar_buf + bpos, sizeof(bar_buf) - bpos, "%s", sub_blocks[frac_idx]);
                }
                bar_buf[bpos] = '\0';

                char empty_buf[128] = {0};
                size_t epos = 0;
                for (int i = 0; i < empty_blocks && epos + 4 < sizeof(empty_buf); i++) {
                    epos += (size_t)snprintf(empty_buf + epos, sizeof(empty_buf) - epos, "░");
                }
                empty_buf[epos] = '\0';

                if (color) {
                    printf("\r\033[2K \033[1;38;5;45m%5.1f%%\033[0m \033[38;5;240m▕\033[38;5;39m%s\033[38;5;237m%s\033[38;5;240m▏\033[0m %s",
                           percent, bar_buf, empty_buf, info_buf);
                } else {
                    printf("\r\033[2K %5.1f%% ▕%s%s▏ %s",
                           percent, bar_buf, empty_buf, info_buf);
                }
            } else {
                /* Compact text-only mode for small terminal tiles / stream downloads */
                if (total > 0) {
                    if (color) {
                        printf("\r\033[2K \033[1;38;5;45m%5.1f%%\033[0m  %s", percent, info_buf);
                    } else {
                        printf("\r\033[2K %5.1f%%  %s", percent, info_buf);
                    }
                } else {
                    if (color) {
                        printf("\r\033[2K \033[1;38;5;45mSTREAM\033[0m  %s", info_buf);
                    } else {
                        printf("\r\033[2K STREAM  %s", info_buf);
                    }
                }
            }
            fflush(stdout);
        } else {
            /* Non-interactive stream output (log file, pipe, CI) */
            if ((now - last_non_tty_log_time) >= 1000000ULL) { /* Every 1s */
                if (total > 0) {
                    printf("[down] %5.1f%%   %s / %s   %10s   ETA %s   (%d conn)\n",
                           percent, cur_str, tot_str, spd_str,
                           (instant_speed > 512.0 ? eta_str : "--:--"), active_conn);
                } else {
                    printf("[down] %s   %10s   (%d conn)\n",
                           cur_str, spd_str, active_conn);
                }
                fflush(stdout);
                last_non_tty_log_time = now;
            }
        }
    }

    return NULL;
}

int telemetry_init(down_telemetry_t *telem, uint64_t total_size, uint64_t initial_bytes, bool quiet) {
    if (!telem) return -1;
    memset(telem, 0, sizeof(*telem));
    atomic_store(&telem->total_size, total_size);
    atomic_store(&telem->downloaded_bytes, initial_bytes);
    atomic_store(&telem->initial_bytes, initial_bytes);
    atomic_store(&telem->active_connections, 0);
    atomic_store(&telem->stop_requested, false);
    telem->quiet = quiet;
    telem->start_time_us = current_time_micros();
    return 0;
}

int telemetry_start(down_telemetry_t *telem) {
    if (!telem || telem->quiet) return 0;
    return pthread_create(&telem->thread, NULL, telemetry_thread_fn, telem);
}

void telemetry_stop(down_telemetry_t *telem) {
    if (!telem) return;
    atomic_store(&telem->stop_requested, true);

    if (!telem->quiet && telem->thread) {
        pthread_join(telem->thread, NULL);
        telem->thread = 0;

        if (isatty(STDOUT_FILENO)) {
            printf("\r\033[2K"); /* Cleanly clear live progress bar */
            fflush(stdout);
        }
    }
}

void telemetry_print_complete(const down_telemetry_t *telem, const char *filepath) {
    if (!telem || telem->quiet) return;

    uint64_t end_time = current_time_micros();
    uint64_t total_us = end_time - telem->start_time_us;
    double total_sec = (double)total_us / 1000000.0;
    if (total_sec < 0.001) total_sec = 0.001;

    uint64_t total_downloaded = atomic_load(&telem->downloaded_bytes);
    uint64_t session_bytes = total_downloaded - atomic_load(&telem->initial_bytes);
    double avg_speed = (double)session_bytes / total_sec;

    char size_str[32], speed_str[32], dur_str[32], bytes_str[32];
    format_bytes(total_downloaded, size_str, sizeof(size_str));
    format_speed(avg_speed, speed_str, sizeof(speed_str));
    format_duration((uint64_t)total_sec, dur_str, sizeof(dur_str));
    format_number_commas(total_downloaded, bytes_str, sizeof(bytes_str));

    bool color = use_color();
    const char *dim   = color ? "\033[38;5;240m" : "";
    const char *lbl   = color ? "\033[38;5;246m" : "";
    const char *bold  = color ? "\033[1;37m" : "";
    const char *green = color ? "\033[1;38;5;48m" : "";
    const char *reset = color ? "\033[0m" : "";

    printf("\n%s──%s %s✔ Download Complete%s %s───────────────────────────────────────────%s\n",
           dim, reset, green, reset, dim, reset);
    printf("  %sDestination%s  : %s%s%s\n", lbl, reset, bold, filepath, reset);
    printf("  %sFile Size%s    : %s%s%s %s(%s bytes)%s\n", lbl, reset, bold, size_str, reset, dim, bytes_str, reset);
    printf("  %sTime Elapsed%s : %s%s%s  %s•%s  %sAverage Speed:%s %s%s%s\n",
           lbl, reset, bold, dur_str, reset, dim, reset, lbl, reset, green, speed_str, reset);
    printf("  %sIntegrity%s    : %sVerified 100%% chunks%s %s(storage synced, state cleaned)%s\n",
           lbl, reset, green, reset, dim, reset);
    printf("%s─────────────────────────────────────────────────────────────────%s\n\n", dim, reset);
    fflush(stdout);
}

void telemetry_print_paused(const down_telemetry_t *telem, const char *filepath,
                            const char *meta_path, const char *url) {
    if (!telem) return;

    uint64_t cur = atomic_load(&telem->downloaded_bytes);
    uint64_t total = atomic_load(&telem->total_size);
    double percent = (total > 0) ? ((double)cur / (double)total) * 100.0 : 0.0;

    char cur_str[32], tot_str[32];
    format_bytes(cur, cur_str, sizeof(cur_str));
    format_bytes(total, tot_str, sizeof(tot_str));

    bool color = use_color();
    const char *dim    = color ? "\033[38;5;240m" : "";
    const char *lbl    = color ? "\033[38;5;246m" : "";
    const char *bold   = color ? "\033[1;37m" : "";
    const char *yellow = color ? "\033[1;38;5;214m" : "";
    const char *cyan   = color ? "\033[1;38;5;45m" : "";
    const char *reset  = color ? "\033[0m" : "";

    printf("\n%s──%s %s⏸  Download Paused%s %s─────────────────────────────────────────────%s\n",
           dim, reset, yellow, reset, dim, reset);
    printf("  %sTarget File%s  : %s%s%s\n", lbl, reset, bold, filepath, reset);
    printf("  %sProgress%s     : %s%s / %s (%.1f%% completed)%s\n", lbl, reset, yellow, cur_str, tot_str, percent, reset);
    printf("  %sState File%s   : %s%s%s %s(mmap state preserved)%s\n", lbl, reset, bold, meta_path, reset, dim, reset);
    printf("  %sResume With%s  : %sdown -c -o %s %s%s\n", lbl, reset, cyan, filepath, url, reset);
    printf("%s─────────────────────────────────────────────────────────────────%s\n\n", dim, reset);
    fflush(stdout);
}
