#include "s3.h"

bool s3_is_s3_url(const char *url) {
    if (!url) return false;
    return (strncmp(url, "s3://", 5) == 0 || strncmp(url, "s3a://", 6) == 0);
}

int s3_parse_url(const char *s3_url, char *bucket_out, size_t bucket_sz,
                 char *key_out, size_t key_sz) {
    if (!s3_url || !bucket_out || !key_out || bucket_sz == 0 || key_sz == 0) {
        return -1;
    }

    const char *p = NULL;
    if (strncmp(s3_url, "s3://", 5) == 0) {
        p = s3_url + 5;
    } else if (strncmp(s3_url, "s3a://", 6) == 0) {
        p = s3_url + 6;
    } else {
        return -1;
    }

    const char *slash = strchr(p, '/');
    if (!slash) {
        /* Entire rest is bucket */
        snprintf(bucket_out, bucket_sz, "%s", p);
        key_out[0] = '\0';
    } else {
        size_t b_len = (size_t)(slash - p);
        if (b_len >= bucket_sz) return -1;
        memcpy(bucket_out, p, b_len);
        bucket_out[b_len] = '\0';

        /* Advance past leading slash(es) for key */
        const char *k = slash + 1;
        while (*k == '/') k++;
        snprintf(key_out, key_sz, "%s", k);
    }

    if (bucket_out[0] == '\0') {
        return -1;
    }

    return 0;
}

int s3_transform_url(down_config_t *config) {
    if (!config) return -1;
    if (!s3_is_s3_url(config->url)) {
        return 0; /* Nothing to transform */
    }

    char bucket[256];
    char key[1500];
    if (s3_parse_url(config->url, bucket, sizeof(bucket), key, sizeof(key)) != 0) {
        fprintf(stderr, "[!] Error: invalid S3 URL format: '%s'\n", config->url);
        return -1;
    }

    config->aws_sigv4_enabled = true;

    /* If endpoint is specified (e.g., Cloudflare R2, MinIO) */
    if (config->s3_endpoint[0] != '\0') {
        char endpoint[1024];
        snprintf(endpoint, sizeof(endpoint), "%s", config->s3_endpoint);
        size_t elen = strlen(endpoint);
        while (elen > 0 && endpoint[elen - 1] == '/') {
            endpoint[--elen] = '\0';
        }

        if (key[0] != '\0') {
            snprintf(config->url, sizeof(config->url), "%s/%s/%s", endpoint, bucket, key);
        } else {
            snprintf(config->url, sizeof(config->url), "%s/%s", endpoint, bucket);
        }
    } else {
        /* Standard AWS S3 endpoint */
        const char *region = config->aws_region[0] ? config->aws_region : "us-east-1";
        if (strcmp(region, "us-east-1") == 0) {
            if (key[0] != '\0') {
                snprintf(config->url, sizeof(config->url), "https://%s.s3.amazonaws.com/%s", bucket, key);
            } else {
                snprintf(config->url, sizeof(config->url), "https://%s.s3.amazonaws.com", bucket);
            }
        } else {
            if (key[0] != '\0') {
                snprintf(config->url, sizeof(config->url), "https://%s.s3.%s.amazonaws.com/%s", bucket, region, key);
            } else {
                snprintf(config->url, sizeof(config->url), "https://%s.s3.%s.amazonaws.com", bucket, region);
            }
        }
    }

    return 0;
}

int s3_init_auth(down_config_t *config) {
    if (!config) return -1;

    /* Check if S3 credentials/endpoint are in environment */
    if (config->aws_access_key[0] == '\0') {
        const char *env_key = getenv("AWS_ACCESS_KEY_ID");
        if (!env_key) env_key = getenv("AWS_ACCESS_KEY");
        if (env_key) snprintf(config->aws_access_key, sizeof(config->aws_access_key), "%s", env_key);
    }

    if (config->aws_secret_key[0] == '\0') {
        const char *env_sec = getenv("AWS_SECRET_ACCESS_KEY");
        if (!env_sec) env_sec = getenv("AWS_SECRET_KEY");
        if (env_sec) snprintf(config->aws_secret_key, sizeof(config->aws_secret_key), "%s", env_sec);
    }

    if (config->aws_region[0] == '\0') {
        const char *env_reg = getenv("AWS_REGION");
        if (!env_reg) env_reg = getenv("AWS_DEFAULT_REGION");
        if (env_reg) {
            snprintf(config->aws_region, sizeof(config->aws_region), "%s", env_reg);
        } else if (strstr(config->url, "r2.cloudflarestorage.com") != NULL ||
                   strstr(config->s3_endpoint, "r2.cloudflarestorage.com") != NULL) {
            snprintf(config->aws_region, sizeof(config->aws_region), "auto");
        } else {
            snprintf(config->aws_region, sizeof(config->aws_region), "us-east-1");
        }
    }

    if (config->aws_token[0] == '\0') {
        const char *env_tok = getenv("AWS_SESSION_TOKEN");
        if (env_tok) snprintf(config->aws_token, sizeof(config->aws_token), "%s", env_tok);
    }

    if (config->s3_endpoint[0] == '\0') {
        const char *env_ep = getenv("AWS_ENDPOINT_URL");
        if (!env_ep) env_ep = getenv("S3_ENDPOINT_URL");
        if (env_ep) snprintf(config->s3_endpoint, sizeof(config->s3_endpoint), "%s", env_ep);
    }

    if (config->aws_service[0] == '\0') {
        snprintf(config->aws_service, sizeof(config->aws_service), "s3");
    }

    /* If SigV4 is enabled, construct provider string if not explicitly given */
    if (config->aws_sigv4_enabled) {
        if (config->aws_sigv4_provider[0] == '\0') {
            snprintf(config->aws_sigv4_provider, sizeof(config->aws_sigv4_provider),
                     "aws:amz:%s:%s", config->aws_region, config->aws_service);
        }

        /* If session token present, append X-Amz-Security-Token header */
        if (config->aws_token[0] != '\0') {
            char tok_hdr[1200];
            snprintf(tok_hdr, sizeof(tok_hdr), "X-Amz-Security-Token: %s", config->aws_token);
            config->custom_headers = curl_slist_append(config->custom_headers, tok_hdr);
        }
    }

    return 0;
}

int s3_apply_curl_opts(CURL *curl, const down_config_t *config) {
    if (!curl || !config) return -1;

    if (config->aws_sigv4_enabled) {
        curl_easy_setopt(curl, CURLOPT_AWS_SIGV4, config->aws_sigv4_provider);

        if (config->aws_access_key[0] != '\0' && config->aws_secret_key[0] != '\0') {
            char userpwd[520];
            snprintf(userpwd, sizeof(userpwd), "%s:%s", config->aws_access_key, config->aws_secret_key);
            curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd);
        }
    }

    return 0;
}
