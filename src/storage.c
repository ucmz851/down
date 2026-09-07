#include "storage.h"

int storage_init(down_storage_t *storage, const char *filepath, uint64_t total_size, bool no_fallocate, bool resume) {
    if (!storage || !filepath) return -1;

    memset(storage, 0, sizeof(*storage));
    strncpy(storage->filepath, filepath, sizeof(storage->filepath) - 1);
    storage->total_size = total_size;
    storage->fallocate_used = false;

    int flags = O_RDWR | O_CREAT;
    mode_t mode = 0644;

    storage->fd = open(filepath, flags, mode);
    if (storage->fd < 0) {
        fprintf(stderr, "[!] Error opening/creating file '%s': %s\n", filepath, strerror(errno));
        return -1;
    }

    if (total_size > 0) {
        struct stat st;
        if (fstat(storage->fd, &st) == 0) {
            /* If resuming and file already has target size, skip reallocating */
            if (resume && (uint64_t)st.st_size == total_size) {
                return 0;
            }
        }

        if (!no_fallocate) {
#if defined(__APPLE__)
            /* macOS / Darwin (APFS & HFS+): upfront disk space allocation via fcntl F_PREALLOCATE */
            fstore_t store = {
                .fst_flags = F_ALLOCATECONTIG,
                .fst_posmode = F_PEOFPOSMODE,
                .fst_offset = 0,
                .fst_length = (off_t)total_size,
                .fst_bytesalloc = 0
            };
            if (fcntl(storage->fd, F_PREALLOCATE, &store) == -1) {
                /* Contiguous allocation failed (e.g. fragmented disk); attempt non-contiguous */
                store.fst_flags = F_ALLOCATEALL;
                if (fcntl(storage->fd, F_PREALLOCATE, &store) == -1) {
                    if (errno == ENOSPC) {
                        fprintf(stderr, "[!] Insufficient disk space to allocate %" PRIu64 " bytes for '%s'\n",
                                total_size, filepath);
                        close(storage->fd);
                        storage->fd = -1;
                        return -1;
                    }
                    /* Filesystem does not support pre-allocation (e.g. NFS/FAT) -> fallback to ftruncate */
                    if (ftruncate(storage->fd, (off_t)total_size) < 0) {
                        fprintf(stderr, "[!] Warning: ftruncate failed: %s\n", strerror(errno));
                    }
                } else {
                    storage->fallocate_used = true;
                    if (ftruncate(storage->fd, (off_t)total_size) < 0) {
                        fprintf(stderr, "[!] Warning: ftruncate failed: %s\n", strerror(errno));
                    }
                }
            } else {
                storage->fallocate_used = true;
                if (ftruncate(storage->fd, (off_t)total_size) < 0) {
                    fprintf(stderr, "[!] Warning: ftruncate failed: %s\n", strerror(errno));
                }
            }
#else
            int ret = posix_fallocate(storage->fd, 0, (off_t)total_size);
            if (ret == 0) {
                storage->fallocate_used = true;
            } else if (ret == ENOSPC) {
                fprintf(stderr, "[!] Insufficient disk space to allocate %" PRIu64 " bytes for '%s'\n",
                        total_size, filepath);
                close(storage->fd);
                storage->fd = -1;
                return -1;
            } else {
                /* posix_fallocate not supported (e.g. some tmpfs/NFS mounts) -> fallback to ftruncate */
                if (ftruncate(storage->fd, (off_t)total_size) < 0) {
                    fprintf(stderr, "[!] Warning: ftruncate failed: %s\n", strerror(errno));
                }
            }
#endif
        } else {
            if (ftruncate(storage->fd, (off_t)total_size) < 0) {
                fprintf(stderr, "[!] Warning: ftruncate failed: %s\n", strerror(errno));
            }
        }
    }

    return 0;
}

int storage_pwrite_all(int fd, const void *buf, size_t count, off_t offset) {
    const uint8_t *ptr = (const uint8_t *)buf;
    size_t written = 0;

    while (written < count) {
        ssize_t n = pwrite(fd, ptr + written, count - written, offset + (off_t)written);
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[!] pwrite error at offset %" PRId64 " (length %zu): %s\n",
                    (int64_t)(offset + written), count - written, strerror(errno));
            return -1;
        }
        if (n == 0) {
            fprintf(stderr, "[!] pwrite returned 0 at offset %" PRId64 "\n",
                    (int64_t)(offset + written));
            return -1;
        }
        written += (size_t)n;
    }

    return 0;
}

int storage_pread_all(int fd, void *buf, size_t count, off_t offset) {
    uint8_t *ptr = (uint8_t *)buf;
    size_t read_bytes = 0;

    while (read_bytes < count) {
        ssize_t n = pread(fd, ptr + read_bytes, count - read_bytes, offset + (off_t)read_bytes);
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[!] pread error at offset %" PRId64 ": %s\n",
                    (int64_t)(offset + read_bytes), strerror(errno));
            return -1;
        }
        if (n == 0) {
            /* EOF reached before count bytes */
            break;
        }
        read_bytes += (size_t)n;
    }

    return (read_bytes == count) ? 0 : -1;
}

int storage_sync(down_storage_t *storage) {
    if (!storage || storage->fd < 0) return -1;
#if defined(__APPLE__)
    return (fcntl(storage->fd, F_FULLFSYNC) != -1) ? 0 : fsync(storage->fd);
#elif defined(_POSIX_SYNCHRONIZED_IO) && (_POSIX_SYNCHRONIZED_IO > 0)
    return fdatasync(storage->fd);
#else
    return fsync(storage->fd);
#endif
}

void storage_close(down_storage_t *storage) {
    if (storage && storage->fd >= 0) {
        storage_sync(storage);
        close(storage->fd);
        storage->fd = -1;
    }
}
