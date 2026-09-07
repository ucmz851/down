#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include "storage.h"

#define TEST_FILE "test_storage_out.bin"
#define FILE_SIZE (4 * 1024 * 1024) /* 4 MB */
#define NUM_THREADS 8
#define BLOCK_SIZE (FILE_SIZE / NUM_THREADS)

typedef struct {
    int fd;
    int thread_id;
    off_t offset;
    size_t size;
} thread_arg_t;

static void *thread_writer(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;
    uint8_t *buf = malloc(targ->size);
    assert(buf != NULL);

    /* Fill buffer with a pattern unique to this thread */
    memset(buf, (uint8_t)(targ->thread_id + 1), targ->size);

    /* Direct positional write without file locks */
    int ret = storage_pwrite_all(targ->fd, buf, targ->size, targ->offset);
    assert(ret == 0);

    free(buf);
    return NULL;
}

int main(void) {
    printf("[*] Running test_storage...\n");

    down_storage_t storage;
    int ret = storage_init(&storage, TEST_FILE, FILE_SIZE, false, false);
    assert(ret == 0);
    assert(storage.fd >= 0);

    /* Launch concurrent threads writing to disjoint offsets simultaneously without locks */
    pthread_t threads[NUM_THREADS];
    thread_arg_t args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].fd = storage.fd;
        args[i].thread_id = i;
        args[i].offset = (off_t)i * BLOCK_SIZE;
        args[i].size = BLOCK_SIZE;
        pthread_create(&threads[i], NULL, thread_writer, &args[i]);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    storage_sync(&storage);

    /* Verify each block has the exact expected byte pattern */
    uint8_t check_buf[4096];
    for (int i = 0; i < NUM_THREADS; i++) {
        off_t start = (off_t)i * BLOCK_SIZE;
        for (size_t off = 0; off < BLOCK_SIZE; off += sizeof(check_buf)) {
            size_t to_read = sizeof(check_buf);
            if (off + to_read > BLOCK_SIZE) to_read = BLOCK_SIZE - off;

            ret = storage_pread_all(storage.fd, check_buf, to_read, start + off);
            assert(ret == 0);

            uint8_t expected = (uint8_t)(i + 1);
            for (size_t b = 0; b < to_read; b++) {
                if (check_buf[b] != expected) {
                    fprintf(stderr, "Data corruption at offset %ld! Expected %u, got %u\n",
                            start + off + b, expected, check_buf[b]);
                    assert(false);
                }
            }
        }
    }

    storage_close(&storage);
    unlink(TEST_FILE);

    printf("[+] test_storage passed successfully: lockless parallel positional I/O verified!\n");
    return 0;
}
