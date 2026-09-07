#ifndef DOWN_STORAGE_H
#define DOWN_STORAGE_H

#include "down.h"

typedef struct {
    int fd;
    char filepath[1024];
    uint64_t total_size;
    bool fallocate_used;
} down_storage_t;

typedef down_storage_t inlay_storage_t;

/* Initialize or open target file with upfront disk space allocation */
int storage_init(down_storage_t *storage, const char *filepath, uint64_t total_size, bool no_fallocate, bool resume);

/* Thread-safe positional write directly into file offset without file locks */
int storage_pwrite_all(int fd, const void *buf, size_t count, off_t offset);

/* Thread-safe positional read from file offset */
int storage_pread_all(int fd, void *buf, size_t count, off_t offset);

/* Flush operating system caches to disk */
int storage_sync(down_storage_t *storage);

/* Close storage file descriptor */
void storage_close(down_storage_t *storage);

#endif /* DOWN_STORAGE_H */
