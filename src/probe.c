#include "probe.h"
#include "s3.h"
#include <ctype.h>
#include <strings.h>

typedef struct {
    inlay_probe_t *res;
    bool seen_content_range;
} header_parser_state_t;

/* Trim leading/trailing whitespace */
static char *trim_whitespace(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

/* Parse Content-Disposition to extract filename */
static void parse_content_disposition(const char *val, char *dest, size_t dest_size) {
    const char *fn = strstr(val, "filename=");
    if (!fn) {
        fn = strstr(val, "filename*=");
        if (fn) {
            /* Handle filename*=UTF-8''filename.ext format */
            const char *quote = strstr(fn, "''");
            if (quote) {
                fn = quote + 2;
                size_t len = 0;
                while (fn[len] && fn[len] != ';' && fn[len] != '\r' && fn[len] != '\n' && len < dest_size - 1) {
                    dest[len] = fn[len];
                    len++;
                }
                dest[len] = '\0';
                return;
            }
        }
        return;
    }

    fn += 9; /* skip "filename=" */
    while (*fn == ' ' || *fn == '\t') fn++;

    if (*fn == '"') {
        fn++;
        size_t len = 0;
        while (*fn && *fn != '"' && len < dest_size - 1) {
            dest[len++] = *fn++;
        }
        dest[len] = '\0';
    } else {
        size_t len = 0;
        while (*fn && !isspace((unsigned char)*fn) && *fn != ';' && len < dest_size - 1) {
            dest[len++] = *fn++;
        }
        dest[len] = '\0';
    }
}

static size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
    size_t total = size * nitems;
    header_parser_state_t *state = (header_parser_state_t *)userdata;
    inlay_probe_t *res = state->res;

    char line[1024];
    size_t copy_len = total < sizeof(line) - 1 ? total : sizeof(line) - 1;
    memcpy(line, buffer, copy_len);
    line[copy_len] = '\0';

    char *colon = strchr(line, ':');
    if (!colon) return total;

    *colon = '\0';
    char *key = trim_whitespace(line);
    char *val = trim_whitespace(colon + 1);

    if (strcasecmp(key, "Accept-Ranges") == 0) {
        if (strcasestr(val, "bytes") != NULL) {
            res->supports_range = true;
        }
    } else if (strcasecmp(key, "Content-Length") == 0) {
        char *endptr = NULL;
        unsigned long long len = strtoull(val, &endptr, 10);
        if (endptr != val && len > 0) {
            res->content_length = (uint64_t)len;
            res->length_known = true;
        }
    } else if (strcasecmp(key, "Content-Range") == 0) {
        /* Format: bytes 0-0/1234567 or bytes 0-0/asterisk */
        char *slash = strchr(val, '/');
        if (slash) {
            slash++;
            if (*slash != '*') {
                unsigned long long len = strtoull(slash, NULL, 10);
                if (len > 0) {
                    res->content_length = (uint64_t)len;
                    res->length_known = true;
                    res->supports_range = true;
                    state->seen_content_range = true;
                }
            }
        }
    } else if (strcasecmp(key, "Content-Disposition") == 0) {
        if (res->suggested_filename[0] == '\0') {
            parse_content_disposition(val, res->suggested_filename, sizeof(res->suggested_filename));
        }
    }

    return total;
}

void probe_filename_from_url(const char *url, char *dest, size_t dest_size) {
    dest[0] = '\0';
    if (!url || !*url) return;

    /* Strip query string and fragments */
    const char *q = strchr(url, '?');
    const char *h = strchr(url, '#');
    const char *end = url + strlen(url);
    if (q && q < end) end = q;
    if (h && h < end) end = h;

    /* Find last '/' */
    const char *slash = NULL;
    for (const char *p = url; p < end; p++) {
        if (*p == '/') slash = p;
    }

    if (slash && slash + 1 < end) {
        size_t len = (size_t)(end - (slash + 1));
        if (len >= dest_size) len = dest_size - 1;
        memcpy(dest, slash + 1, len);
        dest[len] = '\0';
    }

    /* Fallback if URL ended with a slash or empty */
    if (dest[0] == '\0') {
        snprintf(dest, dest_size, "download.out");
    }
}

int probe_url(const inlay_config_t *config, inlay_probe_t *probe_res) {
    if (!config || !probe_res) return -1;
    memset(probe_res, 0, sizeof(*probe_res));

    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[!] Error: failed to initialize curl easy handle\n");
        return -1;
    }

    header_parser_state_t state = { .res = probe_res, .seen_content_range = false };

    /* Configure HEAD request first */
    curl_easy_setopt(curl, CURLOPT_URL, config->url);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &state);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config->timeout_sec > 0 ? config->timeout_sec : DEFAULT_TIMEOUT_SECS);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, config->user_agent[0] ? config->user_agent : ("inlay/" INLAY_VERSION));
    if (config->custom_headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, config->custom_headers);
    }
    if (config->insecure) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    if (config->ip_version != CURL_IPRESOLVE_WHATEVER) {
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, config->ip_version);
    }
    if (config->http_version != 0) {
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, config->http_version);
    }
    if (config->aws_sigv4_enabled) {
        s3_apply_curl_opts(curl, config);
    }
    if (config->verbose) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    CURLcode res = curl_easy_perform(curl);
    long http_status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    probe_res->http_status = http_status;

    char *eff_url = NULL;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff_url);
    if (eff_url) {
        snprintf(probe_res->effective_url, sizeof(probe_res->effective_url), "%s", eff_url);
    } else {
        snprintf(probe_res->effective_url, sizeof(probe_res->effective_url), "%s", config->url);
    }

    /* If HEAD failed (e.g. 405 Method Not Allowed, or length unknown), try GET with Range: bytes=0-0 */
    if (res != CURLE_OK || http_status >= 400 || !probe_res->length_known) {
        if (config->verbose) {
            fprintf(stderr, "[*] HEAD probe returned HTTP %ld (%s), falling back to ranged GET probe...\n",
                    http_status, curl_easy_strerror(res));
        }
        curl_easy_reset(curl);
        curl_easy_setopt(curl, CURLOPT_URL, probe_res->effective_url);
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_RANGE, "0-0");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &state);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, config->timeout_sec > 0 ? config->timeout_sec : DEFAULT_TIMEOUT_SECS);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, config->user_agent[0] ? config->user_agent : ("inlay/" INLAY_VERSION));
        if (config->custom_headers) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, config->custom_headers);
        }
        if (config->insecure) {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        }
        if (config->ip_version != CURL_IPRESOLVE_WHATEVER) {
            curl_easy_setopt(curl, CURLOPT_IPRESOLVE, config->ip_version);
        }
        if (config->http_version != 0) {
            curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, config->http_version);
        }
        if (config->aws_sigv4_enabled) {
            s3_apply_curl_opts(curl, config);
        }
        /* Discard body bytes of single-byte probe */
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
        if (config->verbose) {
            curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        }

        res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
        probe_res->http_status = http_status;

        if (http_status == 206) {
            probe_res->supports_range = true;
        }

        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff_url);
        if (eff_url) {
            snprintf(probe_res->effective_url, sizeof(probe_res->effective_url), "%s", eff_url);
        }
    }

    curl_easy_cleanup(curl);

    if (http_status < 200 || http_status >= 400) {
        fprintf(stderr, "[!] Probe failed with HTTP %ld\n", http_status);
        return -1;
    }

    if (probe_res->suggested_filename[0] == '\0') {
        probe_filename_from_url(probe_res->effective_url, probe_res->suggested_filename, sizeof(probe_res->suggested_filename));
    }

    return 0;
}
