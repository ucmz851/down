#include "interactive.h"
#include "cli.h"
#include "checksum.h"
#include "history.h"
#include <ctype.h>

static void trim_string(char *str) {
    if (!str) return;

    /* Trim leading whitespace and quotes */
    char *start = str;
    while (*start && (isspace((unsigned char)*start) || *start == '"' || *start == '\'')) {
        start++;
    }

    /* Trim trailing whitespace and quotes */
    size_t len = strlen(start);
    char *end = start + len;
    while (end > start && (isspace((unsigned char)*(end - 1)) || *(end - 1) == '"' || *(end - 1) == '\'')) {
        end--;
    }
    *end = '\0';

    if (start != str) {
        memmove(str, start, (size_t)(end - start + 1));
    }
}

static bool prompt_input(const char *prompt_str, char *buf, size_t sz, bool allow_empty) {
    if (!buf || sz == 0) return false;

    while (true) {
        if (g_shutdown_requested) return false;

        printf("%s", prompt_str);
        fflush(stdout);

        if (!fgets(buf, (int)sz, stdin)) {
            /* EOF or read error */
            return false;
        }

        if (g_shutdown_requested) return false;

        trim_string(buf);

        if (buf[0] == '\0') {
            if (allow_empty) return true;
            printf("  [!] Input cannot be empty. Please try again (or press Ctrl+C to abort).\n");
            continue;
        }

        return true;
    }
}

static void expand_path_tilde(const char *path, char *out, size_t out_sz) {
    if (!path || !out || out_sz == 0) return;

    if (path[0] == '~' && (path[1] == '/' || path[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(out, out_sz, "%s%s", home, path + 1);
            return;
        }
    }
    snprintf(out, out_sz, "%s", path);
}

int interactive_run_wizard(down_config_t *config) {
    if (!config) return -1;

    bool color = isatty(STDOUT_FILENO) && !getenv("NO_COLOR");
    const char *cyan = color ? "\033[1;36m" : "";
    const char *green = color ? "\033[1;32m" : "";
    const char *yellow = color ? "\033[1;33m" : "";
    const char *dim = color ? "\033[38;5;244m" : "";
    const char *bold = color ? "\033[1m" : "";
    const char *reset = color ? "\033[0m" : "";

    printf("\n");
    printf("%s  down — Interactive Setup Wizard%s\n", cyan, reset);
    printf("%s  Download Over Wide Networks — High-Performance Accelerator%s\n", dim, reset);
    printf("%s─────────────────────────────────────────────────────────────────%s\n\n", dim, reset);

    int forced_mode = 0; /* 0: ask, 1: quick start, 2: advanced */

    /* Check for interrupted / resumable downloads if no URL was given yet */
    if (config->url[0] == '\0') {
check_resumable: ;
        down_history_entry_t resumable[10];
        int res_count = history_load_resumable(resumable, 10);

        if (res_count > 0) {
            if (res_count == 1) {
                char dl_str[32], tot_str[32];
                format_bytes(resumable[0].downloaded_bytes, dl_str, sizeof(dl_str));
                format_bytes(resumable[0].total_bytes, tot_str, sizeof(tot_str));

                const char *base = strrchr(resumable[0].output_path, '/');
                base = base ? (base + 1) : resumable[0].output_path;

                printf("%s  Found an interrupted / resumable download:%s\n", yellow, reset);
                printf("    %sFile%s        : %s%s%s\n", bold, reset, cyan, base, reset);
                printf("    %sProgress%s    : %s%.1f%%%s (%s / %s completed)\n",
                       bold, reset, green, resumable[0].percent, reset, dl_str, tot_str);
                printf("    %sSource URL%s  : %s\n", bold, reset, resumable[0].url);
                printf("    %sSaved Path%s  : %s\n\n", bold, reset, resumable[0].output_path);

                printf("%sWhat would you like to do?%s\n", bold, reset);
                printf("  %s[1]%s %sResume '%s'%s %s(Recommended)%s\n", green, reset, bold, base, reset, dim, reset);
                printf("  %s[2]%s Start a new download (Quick Start)\n", bold, reset);
                printf("  %s[3]%s Advanced setup for new download\n", bold, reset);
                printf("  %s[4]%s View all download history\n", dim, reset);
                printf("  %s[5]%s Discard this resume state\n\n", dim, reset);

                char res_buf[32];
                char res_prompt[128];
                snprintf(res_prompt, sizeof(res_prompt), "%s?%s Select option [1-5] %s(default: 1)%s: ",
                         cyan, reset, dim, reset);
                if (!prompt_input(res_prompt, res_buf, sizeof(res_buf), true)) {
                    printf("\n%s[!] Interactive setup aborted.%s\n", yellow, reset);
                    return -1;
                }

                int rchoice = res_buf[0] ? atoi(res_buf) : 1;
                if (rchoice == 1) {
                    snprintf(config->url, sizeof(config->url), "%s", resumable[0].url);
                    snprintf(config->output_path, sizeof(config->output_path), "%s", resumable[0].output_path);
                    config->resume_mode = true;
                    if (config->num_workers <= 0) config->num_workers = DEFAULT_NUM_WORKERS;

                    printf("\n%s── Resuming Download ──────────────────────────────────────────%s\n", dim, reset);
                    printf("  %sFile%s        : %s\n", bold, reset, config->output_path);
                    printf("  %sSource%s      : %s\n", bold, reset, config->url);
                    printf("  %sProgress%s    : %.1f%% (%s / %s)\n", bold, reset, resumable[0].percent, dl_str, tot_str);
                    printf("  %sConnections%s : %d\n", bold, reset, config->num_workers);
                    printf("%s─────────────────────────────────────────────────────────────────%s\n\n", dim, reset);
                    return 0;
                } else if (rchoice == 2) {
                    forced_mode = 1;
                } else if (rchoice == 3) {
                    forced_mode = 2;
                } else if (rchoice == 4) {
                    history_print_table();
                    goto check_resumable;
                } else if (rchoice == 5) {
                    history_discard_resumable(resumable[0].output_path);
                    printf("%s[+] Discarded resume state for '%s'%s\n\n", green, base, reset);
                    goto check_resumable;
                }
            } else {
                printf("%s  Found %d interrupted / resumable downloads:%s\n", yellow, res_count, reset);
                for (int i = 0; i < res_count; i++) {
                    const char *base = strrchr(resumable[i].output_path, '/');
                    base = base ? (base + 1) : resumable[i].output_path;
                    char dl_str[32], tot_str[32];
                    format_bytes(resumable[i].downloaded_bytes, dl_str, sizeof(dl_str));
                    format_bytes(resumable[i].total_bytes, tot_str, sizeof(tot_str));
                    printf("    • %s%s%s — %.1f%% (%s / %s completed)\n",
                           bold, base, reset, resumable[i].percent, dl_str, tot_str);
                }
                int opt_new = res_count + 2;
                int opt_adv = res_count + 3;
                int opt_hist = res_count + 4;
                int opt_discard = res_count + 5;

                printf("\n%sWhat would you like to do?%s\n", bold, reset);
                printf("  %s[1]%s %sResume ALL %d downloads in parallel%s %s(Recommended)%s\n",
                       green, reset, bold, res_count, reset, dim, reset);
                for (int i = 0; i < res_count; i++) {
                    const char *base = strrchr(resumable[i].output_path, '/');
                    base = base ? (base + 1) : resumable[i].output_path;
                    printf("  %s[%d]%s Resume '%s' only\n", bold, i + 2, reset, base);
                }
                printf("  %s[%d]%s Start a new download (Quick Start)\n", bold, opt_new, reset);
                printf("  %s[%d]%s Advanced setup for new download\n", bold, opt_adv, reset);
                printf("  %s[%d]%s View all download history\n", dim, opt_hist, reset);
                printf("  %s[%d]%s Discard all resume states\n\n", dim, opt_discard, reset);

                char res_buf[32];
                char res_prompt[128];
                snprintf(res_prompt, sizeof(res_prompt), "%s?%s Select option [1-%d] %s(default: 1)%s: ",
                         cyan, reset, opt_discard, dim, reset);
                if (!prompt_input(res_prompt, res_buf, sizeof(res_buf), true)) {
                    printf("\n%s[!] Interactive setup aborted.%s\n", yellow, reset);
                    return -1;
                }

                int sel = res_buf[0] ? atoi(res_buf) : 1;
                if (sel == 1) {
                    /* Resume all interrupted downloads concurrently */
                    batch_queue_free(&config->queue);
                    batch_queue_init(&config->queue);
                    for (int i = 0; i < res_count; i++) {
                        batch_queue_add(&config->queue, resumable[i].url, resumable[i].output_path, NULL);
                    }
                    snprintf(config->url, sizeof(config->url), "%s", resumable[0].url);
                    config->resume_mode = true;
                    if (config->num_workers <= 0) config->num_workers = DEFAULT_NUM_WORKERS;
                    config->max_concurrent_downloads = (res_count > MAX_CONCURRENT_DOWNLOADS) ? MAX_CONCURRENT_DOWNLOADS : res_count;

                    printf("\n%s── Resuming %d Downloads (Swarm Parallel Mode) ─────────%s\n", dim, res_count, reset);
                    printf("  %sQueue%s       : %d files to resume\n", bold, reset, res_count);
                    printf("  %sConcurrency%s : %d parallel downloads\n", bold, reset, config->max_concurrent_downloads);
                    printf("  %sConnections%s : %d per file\n", bold, reset, config->num_workers);
                    printf("%s─────────────────────────────────────────────────────────────────%s\n\n", dim, reset);
                    return 0;
                } else if (sel >= 2 && sel <= res_count + 1) {
                    int pick = sel - 2;
                    snprintf(config->url, sizeof(config->url), "%s", resumable[pick].url);
                    snprintf(config->output_path, sizeof(config->output_path), "%s", resumable[pick].output_path);
                    config->resume_mode = true;
                    if (config->num_workers <= 0) config->num_workers = DEFAULT_NUM_WORKERS;
                    config->max_concurrent_downloads = 1;

                    char dl_str[32], tot_str[32];
                    format_bytes(resumable[pick].downloaded_bytes, dl_str, sizeof(dl_str));
                    format_bytes(resumable[pick].total_bytes, tot_str, sizeof(tot_str));

                    printf("\n%s── Resuming Download ──────────────────────────────────────────%s\n", dim, reset);
                    printf("  %sFile%s        : %s\n", bold, reset, config->output_path);
                    printf("  %sSource%s      : %s\n", bold, reset, config->url);
                    printf("  %sProgress%s    : %.1f%% (%s / %s)\n", bold, reset, resumable[pick].percent, dl_str, tot_str);
                    printf("  %sConnections%s : %d\n", bold, reset, config->num_workers);
                    printf("%s─────────────────────────────────────────────────────────────────%s\n\n", dim, reset);
                    return 0;
                } else if (sel == opt_new) {
                    forced_mode = 1;
                } else if (sel == opt_adv) {
                    forced_mode = 2;
                } else if (sel == opt_hist) {
                    history_print_table();
                    goto check_resumable;
                } else if (sel == opt_discard) {
                    for (int i = 0; i < res_count; i++) {
                        history_discard_resumable(resumable[i].output_path);
                    }
                    printf("%s[+] Discarded %d resume states%s\n\n", green, res_count, reset);
                    goto check_resumable;
                }
            }
        }
    }

    /* Step 1: URL input (if not already specified) */
    if (config->url[0] == '\0') {
        char url_buf[1900];
        char prompt[128];
        snprintf(prompt, sizeof(prompt), "%s?%s %sEnter download URL(s):%s ", cyan, reset, bold, reset);

        if (!prompt_input(prompt, url_buf, sizeof(url_buf), false)) {
            printf("\n%s[!] Interactive setup aborted.%s\n", yellow, reset);
            return -1;
        }

        /* Tokenize space- or comma-separated URLs */
        char *saveptr = NULL;
        char *token = strtok_r(url_buf, " ,\t\r\n", &saveptr);
        while (token) {
            trim_string(token);
            if (token[0] != '\0') {
                char clean_url[2048];
                if (strncmp(token, "http://", 7) != 0 &&
                    strncmp(token, "https://", 8) != 0 &&
                    strncmp(token, "s3://", 5) != 0 &&
                    strncmp(token, "s3a://", 6) != 0) {
                    snprintf(clean_url, sizeof(clean_url), "https://%s", token);
                } else {
                    snprintf(clean_url, sizeof(clean_url), "%s", token);
                }
                batch_queue_add(&config->queue, clean_url, NULL, NULL);
            }
            token = strtok_r(NULL, " ,\t\r\n", &saveptr);
        }

        if (config->queue.count > 0) {
            snprintf(config->url, sizeof(config->url), "%s", config->queue.entries[0].url);
            if (config->queue.count > 1) {
                config->max_concurrent_downloads = DEFAULT_CONCURRENT_DOWNLOADS;
            }
        }
    } else {
        if (config->queue.count == 0) {
            batch_queue_add(&config->queue, config->url, config->output_path[0] ? config->output_path : NULL,
                            config->checksum_spec[0] ? config->checksum_spec : NULL);
        }
        if (config->queue.count > 1) {
            printf("%s?%s %sTarget:%s %s%zu URLs queued%s\n", cyan, reset, bold, reset, cyan, config->queue.count, reset);
        } else {
            printf("%s?%s %sTarget URL:%s %s%s%s\n", cyan, reset, bold, reset, cyan, config->url, reset);
        }
    }

    /* Step 2: Mode selection (Quick vs Advanced vs History) */
    int choice = forced_mode;
    if (choice == 0) {
prompt_mode: ;
        printf("\n%sConfiguration Mode:%s\n", bold, reset);
        printf("  %s[1]%s %sQuick Start (Recommended)%s\n", green, reset, bold, reset);
        printf("      %s→ Download immediately with optimized defaults to current directory%s\n", dim, reset);
        printf("  %s[2]%s %sAdvanced Setup%s\n", yellow, reset, bold, reset);
        printf("      %s→ Customize destination, connection count, chunk size, speed limit, checksum%s\n", dim, reset);
        printf("  %s[3]%s %sView Download History%s\n\n", dim, reset, bold, reset);

        char mode_buf[32];
        char mode_prompt[128];
        snprintf(mode_prompt, sizeof(mode_prompt), "%s?%s Select mode [1/2/3] %s(default: 1)%s: ", cyan, reset, dim, reset);

        if (!prompt_input(mode_prompt, mode_buf, sizeof(mode_buf), true)) {
            printf("\n%s[!] Interactive setup aborted.%s\n", yellow, reset);
            return -1;
        }

        if (mode_buf[0] == '3') {
            history_print_table();
            goto prompt_mode;
        }

        choice = (mode_buf[0] == '2') ? 2 : 1;
    }

    if (choice == 1) {
        /* Quick Start: Ensure sensible defaults */
        if (config->output_dir[0] == '\0' && config->output_path[0] == '\0') {
            snprintf(config->output_dir, sizeof(config->output_dir), ".");
        }
        if (config->num_workers <= 0) {
            config->num_workers = DEFAULT_NUM_WORKERS;
        }
    } else {
        /* Advanced Setup */
        printf("\n%s── Advanced Settings ──────────────────────────────────────────%s\n", dim, reset);

        /* Destination directory */
        char dir_buf[512];
        char dir_prompt[128];
        snprintf(dir_prompt, sizeof(dir_prompt), "  %sDestination directory%s %s[default: .]%s: ",
                 bold, reset, dim, reset);
        if (!prompt_input(dir_prompt, dir_buf, sizeof(dir_buf), true)) {
            printf("\n%s[!] Interactive setup aborted.%s\n", yellow, reset);
            return -1;
        }
        if (dir_buf[0] != '\0') {
            expand_path_tilde(dir_buf, config->output_dir, sizeof(config->output_dir));
        } else if (config->output_dir[0] == '\0') {
            snprintf(config->output_dir, sizeof(config->output_dir), ".");
        }

        if (config->queue.count <= 1) {
            /* Custom filename */
            char name_buf[512];
            char name_prompt[128];
            snprintf(name_prompt, sizeof(name_prompt), "  %sCustom filename%s %s[leave blank for auto-detect]%s: ",
                     bold, reset, dim, reset);
            if (!prompt_input(name_prompt, name_buf, sizeof(name_buf), true)) {
                printf("\n%s[!] Interactive setup aborted.%s\n", yellow, reset);
                return -1;
            }
            if (name_buf[0] != '\0') {
                snprintf(config->output_path, sizeof(config->output_path), "%s", name_buf);
            }
        } else {
            /* Concurrent download slots for multi-file swarm */
            char conc_buf[32];
            char conc_prompt[128];
            int def_conc = config->max_concurrent_downloads > 0 ? config->max_concurrent_downloads : DEFAULT_CONCURRENT_DOWNLOADS;
            snprintf(conc_prompt, sizeof(conc_prompt), "  %sConcurrent downloads (1-16)%s %s[default: %d]%s: ",
                     bold, reset, dim, def_conc, reset);
            if (!prompt_input(conc_prompt, conc_buf, sizeof(conc_buf), true)) {
                printf("\n%s[!] Interactive setup aborted.%s\n", yellow, reset);
                return -1;
            }
            if (conc_buf[0] != '\0') {
                int c = atoi(conc_buf);
                if (c >= 1 && c <= MAX_CONCURRENT_DOWNLOADS) {
                    config->max_concurrent_downloads = c;
                } else {
                    config->max_concurrent_downloads = def_conc;
                }
            } else {
                config->max_concurrent_downloads = def_conc;
            }
        }

        /* Number of parallel connections */
        char conn_buf[32];
        char conn_prompt[128];
        int def_conn = config->num_workers > 0 ? config->num_workers : DEFAULT_NUM_WORKERS;
        snprintf(conn_prompt, sizeof(conn_prompt), "  %sParallel connections (1-64)%s %s[default: %d]%s: ",
                 bold, reset, dim, def_conn, reset);
        if (!prompt_input(conn_prompt, conn_buf, sizeof(conn_buf), true)) {
            printf("\n%s[!] Interactive setup aborted.%s\n", yellow, reset);
            return -1;
        }
        if (conn_buf[0] != '\0') {
            int n = atoi(conn_buf);
            if (n >= 1 && n <= MAX_NUM_WORKERS) {
                config->num_workers = n;
            } else {
                printf("  %s[!] Value must be 1-%d. Using default %d.%s\n", yellow, MAX_NUM_WORKERS, def_conn, reset);
                config->num_workers = def_conn;
            }
        } else {
            config->num_workers = def_conn;
        }

        /* Chunk size */
        char chunk_buf[32];
        char chunk_prompt[128];
        snprintf(chunk_prompt, sizeof(chunk_prompt), "  %sChunk size (e.g. 256K, 512K, 1M)%s %s[default: 512K]%s: ",
                 bold, reset, dim, reset);
        if (!prompt_input(chunk_prompt, chunk_buf, sizeof(chunk_buf), true)) {
            printf("\n%s[!] Interactive setup aborted.%s\n", yellow, reset);
            return -1;
        }
        if (chunk_buf[0] != '\0') {
            uint32_t sz = parse_size_string(chunk_buf);
            if (sz >= MIN_CHUNK_SIZE && sz <= MAX_CHUNK_SIZE) {
                config->chunk_size = sz;
            } else {
                printf("  %s[!] Chunk size must be between 64K and 32M. Using 512K.%s\n", yellow, reset);
                config->chunk_size = DEFAULT_CHUNK_SIZE;
            }
        }

        /* Bandwidth rate limit */
        char speed_buf[32];
        char speed_prompt[128];
        snprintf(speed_prompt, sizeof(speed_prompt), "  %sSpeed limit (e.g. 500K, 2M, 10M)%s %s[default: unlimited]%s: ",
                 bold, reset, dim, reset);
        if (!prompt_input(speed_prompt, speed_buf, sizeof(speed_buf), true)) {
            printf("\n%s[!] Interactive setup aborted.%s\n", yellow, reset);
            return -1;
        }
        if (speed_buf[0] != '\0') {
            config->max_speed_limit = parse_speed_string(speed_buf);
        }

        /* Checksum verification */
        char csum_buf[256];
        char csum_prompt[128];
        snprintf(csum_prompt, sizeof(csum_prompt), "  %sChecksum verification (<algo>:<hex>)%s %s[default: none]%s: ",
                 bold, reset, dim, reset);
        if (!prompt_input(csum_prompt, csum_buf, sizeof(csum_buf), true)) {
            printf("\n%s[!] Interactive setup aborted.%s\n", yellow, reset);
            return -1;
        }
        if (csum_buf[0] != '\0') {
            if (checksum_parse_spec(csum_buf, config->checksum_algo, sizeof(config->checksum_algo),
                                    config->expected_checksum, sizeof(config->expected_checksum)) == 0) {
                snprintf(config->checksum_spec, sizeof(config->checksum_spec), "%s", csum_buf);
            } else {
                printf("  %s[!] Invalid checksum specification format. Checksum verification skipped.%s\n", yellow, reset);
            }
        }
    }

    /* Print confirmation summary */
    printf("\n%s── Ready to Download ──────────────────────────────────────────%s\n", dim, reset);
    if (config->queue.count > 1) {
        printf("  %sQueue%s       : %s%zu files%s\n", bold, reset, cyan, config->queue.count, reset);
        printf("  %sConcurrency%s : %s%d files in parallel%s\n", bold, reset, cyan, config->max_concurrent_downloads, reset);
    } else {
        printf("  %sURL%s         : %s\n", bold, reset, config->url);
        if (config->output_path[0] != '\0') {
            printf("  %sFile%s        : %s\n", bold, reset, config->output_path);
        }
    }
    printf("  %sDirectory%s   : %s\n", bold, reset, config->output_dir[0] ? config->output_dir : ".");
    printf("  %sConnections%s : %d\n", bold, reset, config->num_workers);
    if (config->max_speed_limit > 0) {
        char spd[32];
        format_bytes(config->max_speed_limit, spd, sizeof(spd));
        printf("  %sRate Limit%s  : %s/s\n", bold, reset, spd);
    }
    if (config->checksum_spec[0] != '\0') {
        printf("  %sChecksum%s    : %s\n", bold, reset, config->checksum_spec);
    }
    printf("%s─────────────────────────────────────────────────────────────────%s\n\n", dim, reset);

    return 0;
}
