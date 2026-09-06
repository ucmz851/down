#ifndef INLAY_STORAGE_H
#define INLAY_STORAGE_H

#include "inlay.h"

typedef struct {
    int fd;
    char filepath[1024];
    uint64_t total_size;
    bool fallocate_used;
} inlay_storage_t;

/* Initialize or open target file with upfront disk space allocation */
int storage_init(inlay_storage_t *storage, const char *filepath, uint64_t total_size, bool no_fallocate, bool resume);

/* Thread-safe positional write directly into file offset without file locks */
int storage_pwrite_all(int fd, const void *buf, size_t count, off_t offset);

/* Thread-safe positional read from file offset */
int storage_pread_all(int fd, void *buf, size_t count, off_t offset);

/* Flush operating system caches to disk */
int storage_sync(inlay_storage_t *storage);

/* Close storage file descriptor */
void storage_close(inlay_storage_t *storage);

#endif /* INLAY_STORAGE_H */
