#ifndef DOWN_H
#define DOWN_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdatomic.h>
#include <pthread.h>
#include <signal.h>
#include <curl/curl.h>
#include "batch.h"

#define DOWN_VERSION "0.0.2"
#define DEFAULT_NUM_WORKERS 4
#define MAX_NUM_WORKERS 64
#define DEFAULT_CONCURRENT_DOWNLOADS 2
#define MAX_CONCURRENT_DOWNLOADS 16
#define DEFAULT_CHUNK_SIZE (512 * 1024)   /* 512 KB */
#define MIN_CHUNK_SIZE     (64 * 1024)    /* 64 KB */
#define MAX_CHUNK_SIZE     (32 * 1024 * 1024) /* 32 MB */
#define DEFAULT_TIMEOUT_SECS 30
#define DOWN_META_EXT ".down"

/* Global signal flag for graceful shutdown */
extern volatile sig_atomic_t g_shutdown_requested;

/* Down runtime configuration */
typedef struct {
    char url[2048];
    char output_path[1024];
    char output_dir[512];
    char user_agent[256];
    struct curl_slist *custom_headers;
    int num_workers;
    uint32_t chunk_size;
    long timeout_sec;
    int max_retries;
    int retry_delay_sec;
    uint64_t max_speed_limit; /* bytes per second */
    bool resume_mode;
    bool verbose;
    bool quiet;
    bool force_single_stream;
    bool no_fallocate;
    bool use_static;
    bool insecure;
    bool no_color;
    bool interactive_mode;
    long ip_version; /* CURL_IPRESOLVE_WHATEVER / V4 / V6 */
    long http_version; /* 0, CURL_HTTP_VERSION_3, CURL_HTTP_VERSION_3ONLY */

    /* Checksum verification */
    char checksum_spec[256];
    char checksum_algo[64];
    char expected_checksum[256];

    /* Batch URL queue / input file / swarm */
    char input_file[1024];
    int max_concurrent_downloads; /* -j, --concurrent <N> (1-16) */
    batch_queue_t queue;

    /* AWS S3 / Cloudflare R2 SigV4 */
    bool aws_sigv4_enabled;
    char aws_sigv4_provider[256];
    char aws_access_key[256];
    char aws_secret_key[256];
    char aws_region[64];
    char aws_service[64];
    char aws_token[1024];
    char s3_endpoint[1024];
} down_config_t;

/* Backward compatibility aliases */
typedef down_config_t inlay_config_t;
#define INLAY_VERSION DOWN_VERSION
#define INLAY_META_EXT DOWN_META_EXT

/* Human-readable byte formatting helper */
void format_bytes(uint64_t bytes, char *buf, size_t buf_size);

/* Microsecond timestamp helper */
uint64_t current_time_micros(void);

/* Recursive directory creation (like mkdir -p) */
int make_directory_recursive(const char *dir_path);

/* Free heap resources in config */
void config_cleanup(down_config_t *config);

/* Deep-clone a curl_slist structure */
struct curl_slist *clone_slist(const struct curl_slist *src);

/* Forward declare down_telemetry_t */
struct down_telemetry_t;
typedef struct down_telemetry_t down_telemetry_t;

/* Execute download of a single file (used directly or as worker in swarm).
 * If is_swarm is true, skips standalone banners and runs telemetry quietly. */
int down_execute_single(down_config_t *config, down_telemetry_t *external_telem, bool is_swarm);

#endif /* DOWN_H */
