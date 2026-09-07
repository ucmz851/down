#ifndef DOWN_HISTORY_H
#define DOWN_HISTORY_H

#include "down.h"

#define MAX_HISTORY_ENTRIES 100

typedef enum {
    DOWN_STATUS_IN_PROGRESS = 0,
    DOWN_STATUS_INTERRUPTED,
    DOWN_STATUS_COMPLETED,
    DOWN_STATUS_FAILED
} down_status_t;

typedef struct {
    char timestamp[32];      /* "YYYY-MM-DD HH:MM:SS" */
    down_status_t status;    /* COMPLETED, INTERRUPTED, IN_PROGRESS, FAILED */
    uint64_t downloaded_bytes;
    uint64_t total_bytes;
    char output_path[1024];  /* Absolute or relative destination path */
    char url[2048];          /* Resource URL */
    bool has_meta_file;      /* True if .down control file exists on disk */
    double percent;          /* Completion percentage (0.0 - 100.0) */
} down_history_entry_t;

/* Get full path to the history database file (~/.local/state/down/history.tsv) */
int history_get_path(char *buf, size_t buf_sz);

/* Record or update download start in history */
int history_record_start(const down_config_t *config, uint64_t total_bytes);

/* Record download progress or pause/interruption */
int history_record_update(const down_config_t *config, uint64_t downloaded, uint64_t total, down_status_t status);

/* Record successful download completion */
int history_record_complete(const down_config_t *config, uint64_t total_bytes);

/* Load all history entries into array, sorted newest first. Returns count. */
int history_load_all(down_history_entry_t *entries, int max_entries);

/* Load only resumable downloads (those where .down control file exists). Returns count. */
int history_load_resumable(down_history_entry_t *entries, int max_entries);

/* Remove or discard an entry from history and delete its .down file if requested */
int history_discard_resumable(const char *output_path);

/* Print a formatted terminal table of past downloads */
void history_print_table(void);

/* Clear all history records */
int history_clear(void);

#endif /* DOWN_HISTORY_H */
