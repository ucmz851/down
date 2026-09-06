#ifndef INLAY_META_H
#define INLAY_META_H

#include "inlay.h"

#define INLAY_META_MAGIC "INLAY01"
#define INLAY_META_MAGIC_LEN 8

typedef struct {
    char magic[INLAY_META_MAGIC_LEN]; /* "INLAY01\0" */
    uint64_t file_size;               /* Total file size */
    uint32_t chunk_size;              /* Chunk size in bytes */
    uint32_t num_chunks;              /* Total chunk count */
    _Atomic uint32_t completed_chunks;/* Number of finished chunks */
    uint32_t reserved;                /* Reserved / padding */
    uint64_t url_hash;                /* FNV-1a hash of URL */
} inlay_meta_hdr_t;

typedef struct {
    int fd;
    char meta_filepath[1200];
    size_t mapped_len;
    inlay_meta_hdr_t *hdr;
    _Atomic uint8_t *bitfield;
    bool is_resumed;
} inlay_meta_t;

/* Initialize or load existing memory-mapped .inlay control file */
int meta_open(inlay_meta_t *meta, const char *target_filepath, const char *url,
              uint64_t file_size, uint32_t chunk_size, bool force_resume);

/* Query whether a chunk has already been downloaded */
bool meta_is_chunk_done(const inlay_meta_t *meta, uint32_t chunk_idx);

/* Atomically mark a chunk as completed and trigger asynchronous msync */
bool meta_mark_chunk_done(inlay_meta_t *meta, uint32_t chunk_idx);

/* Synchronize mmap changes synchronously or asynchronously */
int meta_sync(inlay_meta_t *meta, bool synchronous);

/* Close meta without removing the file (for pause/resume later) */
void meta_close(inlay_meta_t *meta);

/* Complete download: sync storage, unmap, and remove .inlay control file */
void meta_remove(inlay_meta_t *meta);

/* Helper to compute FNV-1a 64-bit hash */
uint64_t hash_url(const char *url);

#endif /* INLAY_META_H */
