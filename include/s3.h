#ifndef DOWN_S3_H
#define DOWN_S3_H

#include "down.h"

/* Returns true if URL uses s3:// or s3a:// scheme */
bool s3_is_s3_url(const char *url);

/* Parse s3://bucket/key into separate bucket and key strings */
int s3_parse_url(const char *s3_url, char *bucket_out, size_t bucket_sz,
                 char *key_out, size_t key_sz);

/* Transform s3:// URLs into HTTPS endpoints and initialize SigV4 params */
int s3_transform_url(down_config_t *config);

/* Resolve AWS credentials, region, endpoint, and session tokens from flags or env */
int s3_init_auth(down_config_t *config);

/* Configure libcurl easy handle with AWS SigV4 options and credentials */
int s3_apply_curl_opts(CURL *curl, const down_config_t *config);

#endif /* DOWN_S3_H */
