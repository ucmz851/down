#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include "meta.h"

#define TEST_META_TARGET "test_meta_file.bin"
#define FILE_SIZE (10 * 1024 * 1024) /* 10 MB */
#define CHUNK_SIZE (512 * 1024)      /* 512 KB -> 20 chunks */
#define NUM_CHUNKS 20

typedef struct {
    down_meta_t *meta;
    int thread_id;
} meta_thread_arg_t;

static void *thread_meta_writer(void *arg) {
    meta_thread_arg_t *m = (meta_thread_arg_t *)arg;
    /* Each of the 4 threads marks 5 chunks */
    for (int i = 0; i < 5; i++) {
        uint32_t chunk_idx = m->thread_id * 5 + i;
        bool set = meta_mark_chunk_done(m->meta, chunk_idx);
        assert(set == true);
    }
    return NULL;
}

int main(void) {
    printf("[*] Running test_meta...\n");

    const char *url = "http://example.com/test_archive.iso";
    down_meta_t meta;

    /* Clean any leftover */
    char meta_path[1024];
    snprintf(meta_path, sizeof(meta_path), "%s%s", TEST_META_TARGET, DOWN_META_EXT);
    unlink(meta_path);

    int ret = meta_open(&meta, TEST_META_TARGET, url, FILE_SIZE, CHUNK_SIZE, false);
    assert(ret == 0);
    assert(meta.hdr != NULL);
    assert(meta.hdr->num_chunks == NUM_CHUNKS);
    assert(atomic_load(&meta.hdr->completed_chunks) == 0);

    for (uint32_t i = 0; i < NUM_CHUNKS; i++) {
        assert(!meta_is_chunk_done(&meta, i));
    }

    /* Concurrently mark chunks across 4 threads */
    pthread_t threads[4];
    meta_thread_arg_t args[4];
    for (int t = 0; t < 4; t++) {
        args[t].meta = &meta;
        args[t].thread_id = t;
        pthread_create(&threads[t], NULL, thread_meta_writer, &args[t]);
    }
    for (int t = 0; t < 4; t++) {
        pthread_join(threads[t], NULL);
    }

    assert(atomic_load(&meta.hdr->completed_chunks) == NUM_CHUNKS);
    for (uint32_t i = 0; i < NUM_CHUNKS; i++) {
        assert(meta_is_chunk_done(&meta, i));
    }

    /* Close and persist to disk */
    meta_close(&meta);

    /* Re-open in resume mode and verify instant crash recovery without re-download */
    down_meta_t resumed_meta;
    ret = meta_open(&resumed_meta, TEST_META_TARGET, url, FILE_SIZE, CHUNK_SIZE, true);
    assert(ret == 0);
    assert(resumed_meta.is_resumed == true);
    assert(atomic_load(&resumed_meta.hdr->completed_chunks) == NUM_CHUNKS);

    for (uint32_t i = 0; i < NUM_CHUNKS; i++) {
        assert(meta_is_chunk_done(&resumed_meta, i));
    }

    /* Cleanup */
    meta_remove(&resumed_meta);
    struct stat st;
    assert(stat(meta_path, &st) != 0);

    printf("[+] test_meta passed successfully: mmap crash recovery & bitfield verified!\n");
    return 0;
}
