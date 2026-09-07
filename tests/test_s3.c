#include "s3.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    printf("[*] Running test_s3...\n");

    /* 1. Scheme detection */
    assert(s3_is_s3_url("s3://bucket/key"));
    assert(s3_is_s3_url("s3a://bucket/key"));
    assert(!s3_is_s3_url("https://bucket.s3.amazonaws.com/key"));
    assert(!s3_is_s3_url("ftp://example.com/file"));

    /* 2. URL parsing */
    char bucket[256];
    char key[1024];

    assert(s3_parse_url("s3://deep-data/models/weights.bin", bucket, sizeof(bucket), key, sizeof(key)) == 0);
    assert(strcmp(bucket, "deep-data") == 0);
    assert(strcmp(key, "models/weights.bin") == 0);

    assert(s3_parse_url("s3a://archive/2026/backup.tar.gz", bucket, sizeof(bucket), key, sizeof(key)) == 0);
    assert(strcmp(bucket, "archive") == 0);
    assert(strcmp(key, "2026/backup.tar.gz") == 0);

    /* 3. URL transformation: Standard S3 */
    down_config_t config;
    memset(&config, 0, sizeof(config));
    snprintf(config.url, sizeof(config.url), "s3://prod-backup/database.dump");
    snprintf(config.aws_region, sizeof(config.aws_region), "us-east-1");

    assert(s3_transform_url(&config) == 0);
    assert(config.aws_sigv4_enabled == true);
    assert(strcmp(config.url, "https://prod-backup.s3.amazonaws.com/database.dump") == 0);

    /* URL transformation: Regional S3 */
    memset(&config, 0, sizeof(config));
    snprintf(config.url, sizeof(config.url), "s3://eu-assets/images/logo.png");
    snprintf(config.aws_region, sizeof(config.aws_region), "eu-central-1");

    assert(s3_transform_url(&config) == 0);
    assert(config.aws_sigv4_enabled == true);
    assert(strcmp(config.url, "https://eu-assets.s3.eu-central-1.amazonaws.com/images/logo.png") == 0);

    /* URL transformation: Cloudflare R2 / MinIO custom endpoint */
    memset(&config, 0, sizeof(config));
    snprintf(config.url, sizeof(config.url), "s3://my-bucket/dataset.zip");
    snprintf(config.s3_endpoint, sizeof(config.s3_endpoint), "https://12345678.r2.cloudflarestorage.com/");

    assert(s3_transform_url(&config) == 0);
    assert(config.aws_sigv4_enabled == true);
    assert(strcmp(config.url, "https://12345678.r2.cloudflarestorage.com/my-bucket/dataset.zip") == 0);

    /* 4. Auth & Provider init */
    assert(s3_init_auth(&config) == 0);
    assert(strcmp(config.aws_region, "auto") == 0); /* Cloudflare R2 auto-region */
    assert(strcmp(config.aws_sigv4_provider, "aws:amz:auto:s3") == 0);

    printf("[+] test_s3 passed successfully!\n");
    return 0;
}
