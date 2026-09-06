#include "cli.h"
#include "checksum.h"
#include "s3.h"
#include "update.h"
#include <getopt.h>
#include <ctype.h>

int make_directory_recursive(const char *dir_path) {
    if (!dir_path || !*dir_path) return 0;
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", dir_path);
    size_t len = strlen(tmp);
    if (len == 0) return 0;

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

void config_cleanup(inlay_config_t *config) {
    if (!config) return;
    if (config->custom_headers) {
        curl_slist_free_all(config->custom_headers);
        config->custom_headers = NULL;
    }
}

uint32_t parse_size_string(const char *str) {
    if (!str || !*str) return DEFAULT_CHUNK_SIZE;
    char *endptr = NULL;
    double val = strtod(str, &endptr);
    if (val <= 0) return DEFAULT_CHUNK_SIZE;

    while (endptr && isspace((unsigned char)*endptr)) endptr++;

    uint64_t multiplier = 1;
    if (endptr && *endptr) {
        char u = (char)toupper((unsigned char)*endptr);
        if (u == 'K') multiplier = 1024ULL;
        else if (u == 'M') multiplier = 1024ULL * 1024ULL;
        else if (u == 'G') multiplier = 1024ULL * 1024ULL * 1024ULL;
    }

    uint64_t total = (uint64_t)(val * (double)multiplier);
    if (total < MIN_CHUNK_SIZE) total = MIN_CHUNK_SIZE;
    if (total > MAX_CHUNK_SIZE) total = MAX_CHUNK_SIZE;
    return (uint32_t)total;
}

uint64_t parse_speed_string(const char *str) {
    if (!str || !*str) return 0;
    char *endptr = NULL;
    double val = strtod(str, &endptr);
    if (val <= 0) return 0;

    while (endptr && isspace((unsigned char)*endptr)) endptr++;

    uint64_t multiplier = 1;
    if (endptr && *endptr) {
        char u = (char)toupper((unsigned char)*endptr);
        if (u == 'K') multiplier = 1024ULL;
        else if (u == 'M') multiplier = 1024ULL * 1024ULL;
        else if (u == 'G') multiplier = 1024ULL * 1024ULL * 1024ULL;
    }

    return (uint64_t)(val * (double)multiplier);
}

void cli_print_version(void) {
    printf("inlay %s (High-Performance Segmented Download Engine)\n", INLAY_VERSION);
    printf("Features: fallocate, lockless pwrite, dynamic work-stealing, mmap crash recovery\n");
    printf("Network : libcurl %s (HTTP/3 QUIC supported)\n", curl_version());
    printf("Crypto  : OpenSSL EVP (SHA-256, SHA-512, MD5, SHA-1, BLAKE2)\n");
    printf("Storage : AWS S3 / Cloudflare R2 SigV4 direct signing\n");
}

void cli_print_usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS] [<URL>]\n\n", prog_name);
    printf("High-performance segmented download engine with zero-assembly positional I/O.\n\n");
    printf("Arguments:\n");
    printf("  <URL>                      HTTP, HTTPS, or S3 (s3://) resource URL to download\n\n");
    printf("Target & Batch:\n");
    printf("  -o, --output <PATH>        Destination file name or path (default: auto-detected)\n");
    printf("  -d, --dir <DIRECTORY>      Destination folder (auto-created if nonexistent)\n");
    printf("  -i, --input-file <FILE>    Batch download: read list of URLs from file\n");
    printf("  -c, --continue             Resume interrupted download from .inlay control file\n");
    printf("  -C, --checksum <SPEC>      Verify hash after download (<algo>:<hex> or raw hex)\n");
    printf("      --no-fallocate         Disable upfront contiguous disk block pre-allocation\n\n");
    printf("Concurrency & Performance:\n");
    printf("  -n, --connections <N>      Number of parallel connections (1-%d, default: %d)\n",
           MAX_NUM_WORKERS, DEFAULT_NUM_WORKERS);
    printf("  -s, --chunk-size <SIZE>    Uniform chunk size (e.g. 256K, 512K, 1M, 4M, default: 512K)\n");
    printf("      --static               Use static range partitioning instead of work-stealing\n");
    printf("      --force-single         Force single-stream sequential download\n\n");
    printf("Protocols & Network:\n");
    printf("      --http3                Enable HTTP/3 (QUIC) with automatic protocol fallback\n");
    printf("      --http3-only           Force HTTP/3 (QUIC) without protocol fallback\n");
    printf("  -t, --timeout <SECS>       Connection/transfer timeout in seconds (default: %d)\n",
           DEFAULT_TIMEOUT_SECS);
    printf("  -r, --rate-limit <SPEED>   Maximum bandwidth rate limit (e.g. 500K, 10M, 1G)\n");
    printf("      --retry <N>            Maximum connection retries upon failure (default: 3)\n");
    printf("      --retry-delay <SECS>   Seconds to wait between retries (default: 2)\n");
    printf("  -H, --header <HEADER>      Custom HTTP header (repeatable: -H \"Authorization: ...\")\n");
    printf("  -U, --user-agent <STRING>  Custom HTTP User-Agent string\n");
    printf("  -k, --insecure             Allow insecure HTTPS connections (skip TLS certificate check)\n");
    printf("  -4, --ipv4                 Resolve IPv4 addresses only\n");
    printf("  -6, --ipv6                 Resolve IPv6 addresses only\n\n");
    printf("Cloud Storage & AWS SigV4:\n");
    printf("      --aws-sigv4 [PARAM]    Enable AWS SigV4 request signing (default: aws:amz:<region>:s3)\n");
    printf("      --aws-access-key <KEY> AWS/S3 access key ID (or env AWS_ACCESS_KEY_ID)\n");
    printf("      --aws-secret-key <KEY> AWS/S3 secret access key (or env AWS_SECRET_ACCESS_KEY)\n");
    printf("      --aws-region <REGION>  AWS/S3 region (default: us-east-1, auto for Cloudflare R2)\n");
    printf("      --aws-token <TOKEN>    AWS temporary session token (or env AWS_SESSION_TOKEN)\n");
    printf("      --s3-endpoint <URL>    Custom S3/R2 endpoint (e.g. Cloudflare R2, MinIO, Ceph)\n\n");
    printf("Display & Logging:\n");
    printf("  -q, --quiet                Suppress live ANSI progress bar and interactive output\n");
    printf("  -v, --verbose              Enable detailed diagnostic and curl debug logs\n");
    printf("      --no-color             Disable ANSI color codes in output\n");
    printf("  -V, --version              Print version information and exit\n");
    printf("  -h, --help                 Print this help screen and exit\n\n");
    printf("Updates & Maintenance:\n");
    printf("      --update               Check for and install latest release from GitHub\n");
    printf("      --check-update         Check if a newer version is available without installing\n\n");
    printf("Examples:\n");
    printf("  # Download with 8 parallel connections and SHA-256 validation:\n");
    printf("  %s -n 8 -s 1M -C sha256:abcd... https://releases.ubuntu.com/noble/ubuntu-24.04.iso\n\n", prog_name);
    printf("  # Download via HTTP/3 (QUIC):\n");
    printf("  %s --http3 https://cloudflare-quic.com/test.iso\n\n", prog_name);
    printf("  # Download from AWS S3 or Cloudflare R2:\n");
    printf("  %s s3://my-bucket/dataset.tar.gz\n\n", prog_name);
    printf("  # Batch download from URL list file:\n");
    printf("  %s -i urls.txt -d ~/Downloads -n 8\n", prog_name);
}

int cli_parse_args(int argc, char **argv, inlay_config_t *config) {
    if (!config) return -1;
    memset(config, 0, sizeof(*config));
    config->num_workers = DEFAULT_NUM_WORKERS;
    config->chunk_size = DEFAULT_CHUNK_SIZE;
    config->timeout_sec = DEFAULT_TIMEOUT_SECS;
    config->max_retries = 3;
    config->retry_delay_sec = 2;
    config->max_speed_limit = 0;
    config->resume_mode = false;
    config->verbose = false;
    config->quiet = false;
    config->force_single_stream = false;
    config->no_fallocate = false;
    config->use_static = false;
    config->insecure = false;
    config->no_color = false;
    config->ip_version = CURL_IPRESOLVE_WHATEVER;
    config->http_version = 0;
    config->aws_sigv4_enabled = false;

    static struct option long_options[] = {
        {"output",        required_argument, 0, 'o'},
        {"dir",           required_argument, 0, 'd'},
        {"connections",   required_argument, 0, 'n'},
        {"chunk-size",    required_argument, 0, 's'},
        {"continue",      no_argument,       0, 'c'},
        {"timeout",       required_argument, 0, 't'},
        {"rate-limit",    required_argument, 0, 'r'},
        {"header",        required_argument, 0, 'H'},
        {"user-agent",    required_argument, 0, 'U'},
        {"insecure",      no_argument,       0, 'k'},
        {"ipv4",          no_argument,       0, '4'},
        {"ipv6",          no_argument,       0, '6'},
        {"input-file",    required_argument, 0, 'i'},
        {"checksum",      required_argument, 0, 'C'},
        {"http3",         no_argument,       0, 1010},
        {"http3-only",    no_argument,       0, 1011},
        {"aws-sigv4",     optional_argument, 0, 1020},
        {"aws-access-key",required_argument, 0, 1021},
        {"aws-secret-key",required_argument, 0, 1022},
        {"aws-region",    required_argument, 0, 1023},
        {"aws-token",     required_argument, 0, 1024},
        {"s3-endpoint",   required_argument, 0, 1025},
        {"endpoint-url",  required_argument, 0, 1025},
        {"retry",         required_argument, 0, 1003},
        {"retry-delay",   required_argument, 0, 1004},
        {"static",        no_argument,       0, 1001},
        {"no-fallocate",  no_argument,       0, 1002},
        {"force-single",  no_argument,       0, 1005},
        {"no-color",      no_argument,       0, 1006},
        {"quiet",         no_argument,       0, 'q'},
        {"verbose",       no_argument,       0, 'v'},
        {"update",        no_argument,       0, 1030},
        {"check-update",  no_argument,       0, 1031},
        {"version",       no_argument,       0, 'V'},
        {"help",          no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "o:d:n:s:ct:r:H:U:k46i:C:qvVh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'o':
                snprintf(config->output_path, sizeof(config->output_path), "%s", optarg);
                break;
            case 'd':
                snprintf(config->output_dir, sizeof(config->output_dir), "%s", optarg);
                break;
            case 'n': {
                int n = atoi(optarg);
                if (n < 1 || n > MAX_NUM_WORKERS) {
                    fprintf(stderr, "[!] Error: --connections must be between 1 and %d\n", MAX_NUM_WORKERS);
                    return -1;
                }
                config->num_workers = n;
                break;
            }
            case 's':
                config->chunk_size = parse_size_string(optarg);
                break;
            case 'c':
                config->resume_mode = true;
                break;
            case 't': {
                long t = atol(optarg);
                if (t <= 0) {
                    fprintf(stderr, "[!] Error: --timeout must be greater than 0 seconds\n");
                    return -1;
                }
                config->timeout_sec = t;
                break;
            }
            case 'r':
                config->max_speed_limit = parse_speed_string(optarg);
                break;
            case 'H':
                config->custom_headers = curl_slist_append(config->custom_headers, optarg);
                break;
            case 'U':
                snprintf(config->user_agent, sizeof(config->user_agent), "%s", optarg);
                break;
            case 'k':
                config->insecure = true;
                break;
            case '4':
                config->ip_version = CURL_IPRESOLVE_V4;
                break;
            case '6':
                config->ip_version = CURL_IPRESOLVE_V6;
                break;
            case 'i':
                snprintf(config->input_file, sizeof(config->input_file), "%s", optarg);
                break;
            case 'C':
                snprintf(config->checksum_spec, sizeof(config->checksum_spec), "%s", optarg);
                if (checksum_parse_spec(optarg, config->checksum_algo, sizeof(config->checksum_algo),
                                        config->expected_checksum, sizeof(config->expected_checksum)) != 0) {
                    fprintf(stderr, "[!] Error: invalid checksum specification '%s'. Expected format: <algo>:<hex> (e.g. sha256:...) or valid hex digest.\n", optarg);
                    return -1;
                }
                break;
            case 1010: /* --http3 */ {
                curl_version_info_data *vinfo = curl_version_info(CURLVERSION_NOW);
                if (!(vinfo->features & CURL_VERSION_HTTP3)) {
                    fprintf(stderr, "[!] Warning: this libcurl build does not have HTTP/3 (QUIC) support\n");
                }
                config->http_version = CURL_HTTP_VERSION_3;
                break;
            }
            case 1011: /* --http3-only */ {
                curl_version_info_data *vinfo = curl_version_info(CURLVERSION_NOW);
                if (!(vinfo->features & CURL_VERSION_HTTP3)) {
                    fprintf(stderr, "[!] Warning: this libcurl build does not have HTTP/3 (QUIC) support\n");
                }
                config->http_version = CURL_HTTP_VERSION_3ONLY;
                break;
            }
            case 1020: /* --aws-sigv4 */
                config->aws_sigv4_enabled = true;
                if (optarg && *optarg) {
                    snprintf(config->aws_sigv4_provider, sizeof(config->aws_sigv4_provider), "%s", optarg);
                } else if (optind < argc && argv[optind][0] != '-' &&
                           strncmp(argv[optind], "http://", 7) != 0 &&
                           strncmp(argv[optind], "https://", 8) != 0 &&
                           strncmp(argv[optind], "s3://", 5) != 0 &&
                           strncmp(argv[optind], "s3a://", 6) != 0) {
                    snprintf(config->aws_sigv4_provider, sizeof(config->aws_sigv4_provider), "%s", argv[optind]);
                    optind++;
                }
                break;
            case 1021: /* --aws-access-key */
                snprintf(config->aws_access_key, sizeof(config->aws_access_key), "%s", optarg);
                config->aws_sigv4_enabled = true;
                break;
            case 1022: /* --aws-secret-key */
                snprintf(config->aws_secret_key, sizeof(config->aws_secret_key), "%s", optarg);
                config->aws_sigv4_enabled = true;
                break;
            case 1023: /* --aws-region */
                snprintf(config->aws_region, sizeof(config->aws_region), "%s", optarg);
                break;
            case 1024: /* --aws-token */
                snprintf(config->aws_token, sizeof(config->aws_token), "%s", optarg);
                break;
            case 1025: /* --s3-endpoint / --endpoint-url */
                snprintf(config->s3_endpoint, sizeof(config->s3_endpoint), "%s", optarg);
                config->aws_sigv4_enabled = true;
                break;
            case 1001: /* --static */
                config->use_static = true;
                break;
            case 1002: /* --no-fallocate */
                config->no_fallocate = true;
                break;
            case 1003: /* --retry */
                config->max_retries = atoi(optarg);
                if (config->max_retries < 0) config->max_retries = 0;
                break;
            case 1004: /* --retry-delay */
                config->retry_delay_sec = atoi(optarg);
                if (config->retry_delay_sec < 0) config->retry_delay_sec = 0;
                break;
            case 1005: /* --force-single */
                config->force_single_stream = true;
                break;
            case 1006: /* --no-color */
                config->no_color = true;
                break;
            case 'q':
                config->quiet = true;
                break;
            case 'v':
                config->verbose = true;
                break;
            case 1030: /* --update */ {
                int res = update_check_and_apply(true);
                exit(res == 0 ? 0 : 1);
            }
            case 1031: /* --check-update */ {
                int res = update_check_and_apply(false);
                exit(res == 0 || res == 1 ? 0 : 1);
            }
            case 'V':
                cli_print_version();
                exit(0);
            case 'h':
                cli_print_usage(argv[0]);
                exit(0);
            default:
                cli_print_usage(argv[0]);
                return -1;
        }
    }

    if (optind < argc) {
        snprintf(config->url, sizeof(config->url), "%s", argv[optind]);
    }

    if (config->url[0] == '\0' && config->input_file[0] == '\0') {
        fprintf(stderr, "[!] Error: URL argument or -i/--input-file is required\n\n");
        cli_print_usage(argv[0]);
        return -1;
    }

    if (config->url[0] != '\0') {
        if (s3_is_s3_url(config->url)) {
            if (s3_transform_url(config) != 0) {
                return -1;
            }
        }
    }
    s3_init_auth(config);

    if (config->no_color) {
        setenv("NO_COLOR", "1", 1);
    }

    return 0;
}
