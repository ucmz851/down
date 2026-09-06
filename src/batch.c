#include "batch.h"
#include <ctype.h>

void batch_queue_init(batch_queue_t *queue) {
    if (!queue) return;
    queue->entries = NULL;
    queue->count = 0;
    queue->capacity = 0;
}

int batch_queue_add(batch_queue_t *queue, const char *url,
                    const char *output_name, const char *checksum_spec) {
    if (!queue || !url || !*url) return -1;

    if (queue->count >= queue->capacity) {
        size_t new_cap = (queue->capacity == 0) ? 16 : queue->capacity * 2;
        batch_entry_t *new_arr = (batch_entry_t *)realloc(queue->entries, new_cap * sizeof(batch_entry_t));
        if (!new_arr) return -1;
        queue->entries = new_arr;
        queue->capacity = new_cap;
    }

    batch_entry_t *entry = &queue->entries[queue->count++];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->url, sizeof(entry->url), "%s", url);

    if (output_name && *output_name) {
        snprintf(entry->output_name, sizeof(entry->output_name), "%s", output_name);
    }
    if (checksum_spec && *checksum_spec) {
        snprintf(entry->checksum_spec, sizeof(entry->checksum_spec), "%s", checksum_spec);
    }

    return 0;
}

static char *trim_string(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

int batch_load_file(const char *filepath, batch_queue_t *queue) {
    if (!filepath || !queue) return -1;

    FILE *f = fopen(filepath, "r");
    if (!f) {
        fprintf(stderr, "[!] Error opening input file '%s': %s\n", filepath, strerror(errno));
        return -1;
    }

    char line[4096];
    batch_entry_t *current_entry = NULL;

    while (fgets(line, sizeof(line), f)) {
        /* Check if line begins with whitespace (aria2-style sub-option) */
        bool is_indented = (line[0] == ' ' || line[0] == '\t');
        char *trimmed = trim_string(line);

        if (*trimmed == '\0' || *trimmed == '#' || *trimmed == ';') {
            continue;
        }

        if (is_indented && current_entry != NULL) {
            if (strncmp(trimmed, "out=", 4) == 0) {
                snprintf(current_entry->output_name, sizeof(current_entry->output_name), "%s", trimmed + 4);
                continue;
            } else if (strncmp(trimmed, "checksum=", 9) == 0) {
                snprintf(current_entry->checksum_spec, sizeof(current_entry->checksum_spec), "%s", trimmed + 9);
                continue;
            }
        }

        /* Top-level line: New URL entry */
        /* Check if line has space-separated URL and output name: e.g. "https://... myfile.iso" */
        char *space = strpbrk(trimmed, " \t");
        char url_buf[2048] = {0};
        char out_buf[512] = {0};

        if (space) {
            *space = '\0';
            snprintf(url_buf, sizeof(url_buf), "%s", trimmed);
            char *second_arg = trim_string(space + 1);
            if (*second_arg) {
                snprintf(out_buf, sizeof(out_buf), "%s", second_arg);
            }
        } else {
            snprintf(url_buf, sizeof(url_buf), "%s", trimmed);
        }

        if (batch_queue_add(queue, url_buf, out_buf[0] ? out_buf : NULL, NULL) == 0) {
            current_entry = &queue->entries[queue->count - 1];
        }
    }

    fclose(f);
    return 0;
}

void batch_queue_free(batch_queue_t *queue) {
    if (!queue) return;
    if (queue->entries) {
        free(queue->entries);
        queue->entries = NULL;
    }
    queue->count = 0;
    queue->capacity = 0;
}
