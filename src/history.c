#include "history.h"
#include "meta.h"
#include <time.h>
#include <sys/file.h>
#include <pthread.h>
#include <limits.h>

static pthread_mutex_t g_history_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool is_same_target_file(const char *path1, const char *path2) {
    if (!path1 || !path2) return false;
    if (strcmp(path1, path2) == 0) return true;

    /* Normalize leading "./" */
    const char *p1 = path1;
    while (p1[0] == '.' && p1[1] == '/') p1 += 2;
    const char *p2 = path2;
    while (p2[0] == '.' && p2[1] == '/') p2 += 2;
    if (strcmp(p1, p2) == 0) return true;

    /* Realpath match if both exist */
    char r1[PATH_MAX], r2[PATH_MAX];
    if (realpath(path1, r1) && realpath(path2, r2)) {
        if (strcmp(r1, r2) == 0) return true;
    }

    /* Check file device and inode */
    struct stat s1, s2;
    if (stat(path1, &s1) == 0 && stat(path2, &s2) == 0) {
        if (s1.st_dev == s2.st_dev && s1.st_ino == s2.st_ino) return true;
    }

    /* Check .down control file device and inode */
    char m1[1200], m2[1200];
    snprintf(m1, sizeof(m1), "%s%s", path1, DOWN_META_EXT);
    snprintf(m2, sizeof(m2), "%s%s", path2, DOWN_META_EXT);
    if (stat(m1, &s1) == 0 && stat(m2, &s2) == 0) {
        if (s1.st_dev == s2.st_dev && s1.st_ino == s2.st_ino) return true;
    }

    return false;
}

static void get_current_timestamp(char *buf, size_t sz) {
    if (!buf || sz == 0) return;
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    strftime(buf, sz, "%Y-%m-%d %H:%M:%S", &tm_buf);
}

static const char *status_to_str(down_status_t s) {
    switch (s) {
        case DOWN_STATUS_IN_PROGRESS: return "IN_PROGRESS";
        case DOWN_STATUS_INTERRUPTED: return "INTERRUPTED";
        case DOWN_STATUS_COMPLETED:   return "COMPLETED";
        case DOWN_STATUS_FAILED:      return "FAILED";
        default:                      return "UNKNOWN";
    }
}

static down_status_t str_to_status(const char *s) {
    if (!s) return DOWN_STATUS_INTERRUPTED;
    if (strcmp(s, "COMPLETED") == 0) return DOWN_STATUS_COMPLETED;
    if (strcmp(s, "IN_PROGRESS") == 0) return DOWN_STATUS_IN_PROGRESS;
    if (strcmp(s, "FAILED") == 0) return DOWN_STATUS_FAILED;
    return DOWN_STATUS_INTERRUPTED;
}

int history_get_path(char *buf, size_t buf_sz) {
    if (!buf || buf_sz == 0) return -1;

    const char *state_home = getenv("XDG_STATE_HOME");
    char dir[900];

    if (state_home && *state_home) {
        snprintf(dir, sizeof(dir), "%s/down", state_home);
    } else {
        const char *home = getenv("HOME");
        if (home && *home) {
#if defined(__APPLE__)
            snprintf(dir, sizeof(dir), "%s/Library/Application Support/down", home);
#else
            snprintf(dir, sizeof(dir), "%s/.local/state/down", home);
#endif
        } else {
            snprintf(dir, sizeof(dir), ".");
        }
    }

    if (strcmp(dir, ".") != 0) {
        make_directory_recursive(dir);
    }

    snprintf(buf, buf_sz, "%s/history.tsv", dir);
    return 0;
}

static int save_entries(const down_history_entry_t *entries, int count) {
    char path[1024];
    if (history_get_path(path, sizeof(path)) != 0) return -1;

    char tmp_path[1200];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", path, (int)getpid());

    FILE *f = fopen(tmp_path, "w");
    if (!f) return -1;

    for (int i = 0; i < count && i < MAX_HISTORY_ENTRIES; i++) {
        const down_history_entry_t *e = &entries[i];
        if (e->output_path[0] == '\0') continue;

        fprintf(f, "%s\t%s\t%" PRIu64 "\t%" PRIu64 "\t%s\t%s\n",
                e->timestamp[0] ? e->timestamp : "unknown",
                status_to_str(e->status),
                e->downloaded_bytes,
                e->total_bytes,
                e->output_path,
                e->url);
    }

    fflush(f);
    int fd = fileno(f);
    if (fd >= 0) fsync(fd);
    fclose(f);

    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        return -1;
    }

    return 0;
}

int history_load_all(down_history_entry_t *entries, int max_entries) {
    if (!entries || max_entries <= 0) return 0;

    char path[1024];
    if (history_get_path(path, sizeof(path)) != 0) return 0;

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[4096];
    int count = 0;

    while (fgets(line, sizeof(line), f) && count < max_entries) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        /* Parse tab-separated columns:
         * timestamp \t status \t downloaded \t total \t output_path \t url */
        char *p = line;
        char *col_time = p;

        char *t1 = strchr(p, '\t');
        if (!t1) continue;
        *t1 = '\0';
        p = t1 + 1;
        char *col_status = p;

        char *t2 = strchr(p, '\t');
        if (!t2) continue;
        *t2 = '\0';
        p = t2 + 1;
        char *col_downloaded = p;

        char *t3 = strchr(p, '\t');
        if (!t3) continue;
        *t3 = '\0';
        p = t3 + 1;
        char *col_total = p;

        char *t4 = strchr(p, '\t');
        if (!t4) continue;
        *t4 = '\0';
        p = t4 + 1;
        char *col_path = p;
        while (col_path[0] == '.' && col_path[1] == '/') col_path += 2;

        char *t5 = strchr(p, '\t');
        char *col_url = "";
        if (t5) {
            *t5 = '\0';
            col_url = t5 + 1;
        }

        /* Skip duplicate entries for the same target file (keep newer entry) */
        bool is_dup = false;
        for (int k = 0; k < count; k++) {
            if (is_same_target_file(entries[k].output_path, col_path)) {
                is_dup = true;
                break;
            }
        }
        if (is_dup) continue;

        down_history_entry_t *e = &entries[count];
        memset(e, 0, sizeof(*e));

        snprintf(e->timestamp, sizeof(e->timestamp), "%s", col_time);
        e->status = str_to_status(col_status);
        e->downloaded_bytes = (uint64_t)strtoull(col_downloaded, NULL, 10);
        e->total_bytes = (uint64_t)strtoull(col_total, NULL, 10);
        snprintf(e->output_path, sizeof(e->output_path), "%s", col_path);
        snprintf(e->url, sizeof(e->url), "%s", col_url);

        /* Inspect on-disk state: check if .down or .inlay exists */
        char meta_path[1200];
        snprintf(meta_path, sizeof(meta_path), "%s%s", e->output_path, DOWN_META_EXT);
        struct stat st;
        if (stat(meta_path, &st) != 0) {
            snprintf(meta_path, sizeof(meta_path), "%s%s", e->output_path, INLAY_META_EXT);
        }

        if (stat(meta_path, &st) == 0) {
            e->has_meta_file = true;
            int fd = open(meta_path, O_RDONLY);
            if (fd >= 0) {
                down_meta_hdr_t hdr;
                if (read(fd, &hdr, sizeof(hdr)) == sizeof(hdr)) {
                    if (memcmp(hdr.magic, DOWN_META_MAGIC, 7) == 0 ||
                        memcmp(hdr.magic, INLAY_META_MAGIC, 7) == 0) {
                        uint64_t bytes = (uint64_t)hdr.completed_chunks * hdr.chunk_size;
                        if (bytes > hdr.file_size) bytes = hdr.file_size;
                        e->downloaded_bytes = bytes;
                        e->total_bytes = hdr.file_size;
                        if (e->status == DOWN_STATUS_IN_PROGRESS) {
                            e->status = DOWN_STATUS_INTERRUPTED;
                        }
                    }
                }
                close(fd);
            }
        } else {
            e->has_meta_file = false;
        }

        e->percent = (e->total_bytes > 0) ? ((double)e->downloaded_bytes / (double)e->total_bytes) * 100.0 : 0.0;
        if (e->percent > 100.0) e->percent = 100.0;

        count++;
    }

    fclose(f);
    return count;
}

int history_load_resumable(down_history_entry_t *entries, int max_entries) {
    if (!entries || max_entries <= 0) return 0;

    down_history_entry_t all[MAX_HISTORY_ENTRIES];
    int all_count = history_load_all(all, MAX_HISTORY_ENTRIES);

    int resumable_count = 0;
    for (int i = 0; i < all_count && resumable_count < max_entries; i++) {
        if (all[i].has_meta_file && all[i].status != DOWN_STATUS_COMPLETED) {
            bool duplicate = false;
            for (int k = 0; k < resumable_count; k++) {
                if (is_same_target_file(entries[k].output_path, all[i].output_path)) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                entries[resumable_count++] = all[i];
            }
        }
    }

    return resumable_count;
}

int history_record_start(const down_config_t *config, uint64_t total_bytes) {
    if (!config || config->output_path[0] == '\0') return -1;

    pthread_mutex_lock(&g_history_mutex);
    down_history_entry_t entries[MAX_HISTORY_ENTRIES];
    int count = history_load_all(entries, MAX_HISTORY_ENTRIES - 1);

    char now[32];
    get_current_timestamp(now, sizeof(now));

    /* Check if this output_path already has an entry */
    int existing_idx = -1;
    for (int i = 0; i < count; i++) {
        if (is_same_target_file(entries[i].output_path, config->output_path)) {
            existing_idx = i;
            break;
        }
    }

    if (existing_idx >= 0) {
        snprintf(entries[existing_idx].timestamp, sizeof(entries[existing_idx].timestamp), "%s", now);
        entries[existing_idx].status = DOWN_STATUS_IN_PROGRESS;
        if (total_bytes > 0) entries[existing_idx].total_bytes = total_bytes;
        snprintf(entries[existing_idx].url, sizeof(entries[existing_idx].url), "%s", config->url);
    } else {
        /* Prepend to front */
        memmove(&entries[1], &entries[0], (size_t)count * sizeof(down_history_entry_t));
        memset(&entries[0], 0, sizeof(entries[0]));
        snprintf(entries[0].timestamp, sizeof(entries[0].timestamp), "%s", now);
        entries[0].status = DOWN_STATUS_IN_PROGRESS;
        entries[0].downloaded_bytes = 0;
        entries[0].total_bytes = total_bytes;
        snprintf(entries[0].output_path, sizeof(entries[0].output_path), "%s", config->output_path);
        snprintf(entries[0].url, sizeof(entries[0].url), "%s", config->url);
        count++;
    }

    int ret = save_entries(entries, count);
    pthread_mutex_unlock(&g_history_mutex);
    return ret;
}

int history_record_update(const down_config_t *config, uint64_t downloaded, uint64_t total, down_status_t status) {
    if (!config || config->output_path[0] == '\0') return -1;

    pthread_mutex_lock(&g_history_mutex);
    down_history_entry_t entries[MAX_HISTORY_ENTRIES];
    int count = history_load_all(entries, MAX_HISTORY_ENTRIES);

    char now[32];
    get_current_timestamp(now, sizeof(now));

    int idx = -1;
    for (int i = 0; i < count; i++) {
        if (is_same_target_file(entries[i].output_path, config->output_path)) {
            idx = i;
            break;
        }
    }

    if (idx >= 0) {
        snprintf(entries[idx].timestamp, sizeof(entries[idx].timestamp), "%s", now);
        entries[idx].status = status;
        entries[idx].downloaded_bytes = downloaded;
        if (total > 0) entries[idx].total_bytes = total;
    } else {
        /* Prepend if wasn't recorded */
        memmove(&entries[1], &entries[0], (size_t)count * sizeof(down_history_entry_t));
        memset(&entries[0], 0, sizeof(entries[0]));
        snprintf(entries[0].timestamp, sizeof(entries[0].timestamp), "%s", now);
        entries[0].status = status;
        entries[0].downloaded_bytes = downloaded;
        entries[0].total_bytes = total;
        snprintf(entries[0].output_path, sizeof(entries[0].output_path), "%s", config->output_path);
        snprintf(entries[0].url, sizeof(entries[0].url), "%s", config->url);
        count++;
    }

    int ret = save_entries(entries, count);
    pthread_mutex_unlock(&g_history_mutex);
    return ret;
}

int history_record_complete(const down_config_t *config, uint64_t total_bytes) {
    return history_record_update(config, total_bytes, total_bytes, DOWN_STATUS_COMPLETED);
}

int history_discard_resumable(const char *output_path) {
    if (!output_path || !*output_path) return -1;

    char meta_path[1200];
    snprintf(meta_path, sizeof(meta_path), "%s%s", output_path, DOWN_META_EXT);
    unlink(meta_path);
    snprintf(meta_path, sizeof(meta_path), "%s%s", output_path, INLAY_META_EXT);
    unlink(meta_path);

    const char *np = output_path;
    while (np[0] == '.' && np[1] == '/') np += 2;
    snprintf(meta_path, sizeof(meta_path), "%s%s", np, DOWN_META_EXT);
    unlink(meta_path);
    snprintf(meta_path, sizeof(meta_path), "%s%s", np, INLAY_META_EXT);
    unlink(meta_path);

    pthread_mutex_lock(&g_history_mutex);
    /* Update history entry status */
    down_history_entry_t entries[MAX_HISTORY_ENTRIES];
    int count = history_load_all(entries, MAX_HISTORY_ENTRIES);

    for (int i = 0; i < count; i++) {
        if (is_same_target_file(entries[i].output_path, output_path)) {
            entries[i].status = DOWN_STATUS_FAILED;
            entries[i].has_meta_file = false;
        }
    }

    int ret = save_entries(entries, count);
    pthread_mutex_unlock(&g_history_mutex);
    return ret;
}

void history_print_table(void) {
    down_history_entry_t entries[MAX_HISTORY_ENTRIES];
    int count = history_load_all(entries, MAX_HISTORY_ENTRIES);

    bool color = isatty(STDOUT_FILENO) && !getenv("NO_COLOR");
    const char *cyan = color ? "\033[1;36m" : "";
    const char *green = color ? "\033[1;32m" : "";
    const char *yellow = color ? "\033[1;33m" : "";
    const char *red = color ? "\033[1;31m" : "";
    const char *dim = color ? "\033[38;5;244m" : "";
    const char *bold = color ? "\033[1m" : "";
    const char *reset = color ? "\033[0m" : "";

    printf("\n%s  down — Download History & Sessions%s\n", cyan, reset);
    printf("%s──────────────────────────────────────────────────────────────────────────────────────────%s\n", dim, reset);
    printf("  %-19s  %-12s  %-16s  %s\n", "Date/Time", "Status", "Progress", "File / Source");
    printf("%s──────────────────────────────────────────────────────────────────────────────────────────%s\n", dim, reset);

    if (count == 0) {
        printf("  %sNo downloads recorded yet.%s\n", dim, reset);
    } else {
        for (int i = 0; i < count; i++) {
            const down_history_entry_t *e = &entries[i];

            const char *status_color = reset;
            const char *status_name = "UNKNOWN";
            if (e->status == DOWN_STATUS_COMPLETED) {
                status_color = green;
                status_name = "COMPLETED";
            } else if (e->status == DOWN_STATUS_INTERRUPTED) {
                status_color = yellow;
                status_name = e->has_meta_file ? "RESUMABLE" : "PAUSED";
            } else if (e->status == DOWN_STATUS_IN_PROGRESS) {
                status_color = cyan;
                status_name = "IN_PROGRESS";
            } else if (e->status == DOWN_STATUS_FAILED) {
                status_color = red;
                status_name = "FAILED";
            }

            char prog_str[64];
            if (e->total_bytes > 0) {
                char dl_str[32], tot_str[32];
                format_bytes(e->downloaded_bytes, dl_str, sizeof(dl_str));
                format_bytes(e->total_bytes, tot_str, sizeof(tot_str));
                if (e->status == DOWN_STATUS_COMPLETED) {
                    snprintf(prog_str, sizeof(prog_str), "%s (100%%)", tot_str);
                } else {
                    snprintf(prog_str, sizeof(prog_str), "%s (%.1f%%)", dl_str, e->percent);
                }
            } else {
                snprintf(prog_str, sizeof(prog_str), "unknown size");
            }

            /* Extract base filename from path for compact display */
            const char *base = strrchr(e->output_path, '/');
            base = base ? (base + 1) : e->output_path;

            printf("  %-19s  %s%-12s%s  %-16s  %s%s%s",
                   e->timestamp, status_color, status_name, reset, prog_str,
                   bold, base, reset);

            if (e->has_meta_file) {
                printf(" %s(.down state)%s", yellow, reset);
            }
            printf("\n");
            printf("  %s%*s  ↳ %s%s\n", dim, 33, "", e->url, reset);
        }
    }

    printf("%s──────────────────────────────────────────────────────────────────────────────────────────%s\n\n", dim, reset);
}

int history_clear(void) {
    pthread_mutex_lock(&g_history_mutex);
    char path[1024];
    if (history_get_path(path, sizeof(path)) != 0) {
        pthread_mutex_unlock(&g_history_mutex);
        return -1;
    }
    if (unlink(path) == 0) {
        printf("[+] Download history cleared successfully.\n");
        pthread_mutex_unlock(&g_history_mutex);
        return 0;
    }
    if (errno == ENOENT) {
        printf("[*] Download history was already empty.\n");
        pthread_mutex_unlock(&g_history_mutex);
        return 0;
    }
    fprintf(stderr, "[!] Error clearing history: %s\n", strerror(errno));
    pthread_mutex_unlock(&g_history_mutex);
    return -1;
}
