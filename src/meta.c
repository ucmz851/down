#include "meta.h"
#include <sys/mman.h>

uint64_t hash_url(const char *url) {
    uint64_t hash = 14695981039346656037ULL;
    if (!url) return hash;
    while (*url) {
        hash ^= (uint64_t)(unsigned char)(*url++);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint32_t count_set_bits(const _Atomic uint8_t *bitfield, uint32_t num_chunks) {
    uint32_t count = 0;
    uint32_t num_bytes = (num_chunks + 7) / 8;
    for (uint32_t i = 0; i < num_bytes; i++) {
        uint8_t byte = atomic_load_explicit(&bitfield[i], memory_order_relaxed);
        while (byte) {
            count += (byte & 1u);
            byte >>= 1;
        }
    }
    if (count > num_chunks) count = num_chunks;
    return count;
}

int meta_open(down_meta_t *meta, const char *target_filepath, const char *url,
              uint64_t file_size, uint32_t chunk_size, bool force_resume) {
    if (!meta || !target_filepath || !url || chunk_size == 0) return -1;
    (void)force_resume;
    memset(meta, 0, sizeof(*meta));

    snprintf(meta->meta_filepath, sizeof(meta->meta_filepath), "%s%s", target_filepath, DOWN_META_EXT);

    uint32_t num_chunks = (uint32_t)((file_size + chunk_size - 1) / chunk_size);
    if (num_chunks == 0) num_chunks = 1;
    size_t bitfield_bytes = (num_chunks + 7) / 8;
    size_t total_meta_size = sizeof(down_meta_hdr_t) + bitfield_bytes;
    meta->mapped_len = total_meta_size;

    uint64_t target_url_hash = hash_url(url);
    bool should_resume = false;

    /* Check if control file exists (.down first, then fallback to legacy .inlay) */
    struct stat st;
    if (stat(meta->meta_filepath, &st) != 0) {
        char legacy_path[1200];
        snprintf(legacy_path, sizeof(legacy_path), "%s%s", target_filepath, INLAY_META_EXT);
        if (stat(legacy_path, &st) == 0) {
            strncpy(meta->meta_filepath, legacy_path, sizeof(meta->meta_filepath) - 1);
            meta->meta_filepath[sizeof(meta->meta_filepath) - 1] = '\0';
        }
    }

    if (stat(meta->meta_filepath, &st) == 0) {
        if (st.st_size == (off_t)total_meta_size) {
            int existing_fd = open(meta->meta_filepath, O_RDWR);
            if (existing_fd >= 0) {
                void *map = mmap(NULL, total_meta_size, PROT_READ | PROT_WRITE, MAP_SHARED, existing_fd, 0);
                if (map != MAP_FAILED) {
                    down_meta_hdr_t *hdr = (down_meta_hdr_t *)map;
                    if ((memcmp(hdr->magic, DOWN_META_MAGIC, DOWN_META_MAGIC_LEN) == 0 ||
                         memcmp(hdr->magic, INLAY_META_MAGIC, INLAY_META_MAGIC_LEN) == 0) &&
                        hdr->file_size == file_size &&
                        hdr->chunk_size == chunk_size &&
                        hdr->num_chunks == num_chunks &&
                        hdr->url_hash == target_url_hash) {
                        should_resume = true;
                        meta->fd = existing_fd;
                        meta->hdr = hdr;
                        meta->bitfield = (_Atomic uint8_t *)((uint8_t *)map + sizeof(down_meta_hdr_t));
                        meta->is_resumed = true;
                        uint32_t verified_completed = count_set_bits(meta->bitfield, num_chunks);
                        atomic_store_explicit(&meta->hdr->completed_chunks, verified_completed, memory_order_relaxed);
                    } else {
                        munmap(map, total_meta_size);
                        close(existing_fd);
                    }
                } else {
                    close(existing_fd);
                }
            }
        }
    }

    if (should_resume) {
        return 0;
    }

    /* Ensure we create standard .down file */
    snprintf(meta->meta_filepath, sizeof(meta->meta_filepath), "%s%s", target_filepath, DOWN_META_EXT);

    /* Create fresh meta file */
    meta->fd = open(meta->meta_filepath, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (meta->fd < 0) {
        fprintf(stderr, "[!] Failed to create meta control file '%s': %s\n",
                meta->meta_filepath, strerror(errno));
        return -1;
    }

    if (ftruncate(meta->fd, (off_t)total_meta_size) < 0) {
        fprintf(stderr, "[!] Failed to truncate meta control file: %s\n", strerror(errno));
        close(meta->fd);
        meta->fd = -1;
        return -1;
    }

    void *map = mmap(NULL, total_meta_size, PROT_READ | PROT_WRITE, MAP_SHARED, meta->fd, 0);
    if (map == MAP_FAILED) {
        fprintf(stderr, "[!] mmap failed for meta control file: %s\n", strerror(errno));
        close(meta->fd);
        meta->fd = -1;
        return -1;
    }

    meta->hdr = (down_meta_hdr_t *)map;
    meta->bitfield = (_Atomic uint8_t *)((uint8_t *)map + sizeof(down_meta_hdr_t));
    meta->is_resumed = false;

    memset(meta->hdr, 0, sizeof(down_meta_hdr_t));
    memcpy(meta->hdr->magic, DOWN_META_MAGIC, DOWN_META_MAGIC_LEN);
    meta->hdr->file_size = file_size;
    meta->hdr->chunk_size = chunk_size;
    meta->hdr->num_chunks = num_chunks;
    atomic_store_explicit(&meta->hdr->completed_chunks, 0, memory_order_relaxed);
    meta->hdr->url_hash = target_url_hash;

    memset((void *)meta->bitfield, 0, bitfield_bytes);

    /* Initial sync */
    msync(map, total_meta_size, MS_SYNC);
    return 0;
}

bool meta_is_chunk_done(const down_meta_t *meta, uint32_t chunk_idx) {
    if (!meta || !meta->hdr || chunk_idx >= meta->hdr->num_chunks) return false;
    uint32_t byte_idx = chunk_idx / 8;
    uint8_t bit_mask = (uint8_t)(1u << (chunk_idx % 8));
    uint8_t byte_val = atomic_load_explicit(&meta->bitfield[byte_idx], memory_order_relaxed);
    return (byte_val & bit_mask) != 0;
}

bool meta_mark_chunk_done(down_meta_t *meta, uint32_t chunk_idx) {
    if (!meta || !meta->hdr || chunk_idx >= meta->hdr->num_chunks) return false;
    uint32_t byte_idx = chunk_idx / 8;
    uint8_t bit_mask = (uint8_t)(1u << (chunk_idx % 8));

    uint8_t prev = atomic_fetch_or_explicit(&meta->bitfield[byte_idx], bit_mask, memory_order_acq_rel);
    if ((prev & bit_mask) == 0) {
        atomic_fetch_add_explicit(&meta->hdr->completed_chunks, 1, memory_order_relaxed);
        /* Trigger asynchronous writeback of the modified page */
        msync(&meta->bitfield[byte_idx], 1, MS_ASYNC);
        return true;
    }
    return false;
}

int meta_sync(down_meta_t *meta, bool synchronous) {
    if (!meta || !meta->hdr || meta->mapped_len == 0) return -1;
    return msync(meta->hdr, meta->mapped_len, synchronous ? MS_SYNC : MS_ASYNC);
}

void meta_close(down_meta_t *meta) {
    if (!meta) return;
    if (meta->hdr && meta->mapped_len > 0) {
        meta_sync(meta, true);
        munmap(meta->hdr, meta->mapped_len);
        meta->hdr = NULL;
        meta->bitfield = NULL;
        meta->mapped_len = 0;
    }
    if (meta->fd >= 0) {
        close(meta->fd);
        meta->fd = -1;
    }
}

void meta_remove(down_meta_t *meta) {
    if (!meta) return;
    char path[1024];
    strncpy(path, meta->meta_filepath, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';

    meta_close(meta);
    if (path[0]) {
        unlink(path);
    }
}
