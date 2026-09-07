#include "config_file.h"
#include "cli.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

volatile sig_atomic_t g_shutdown_requested = 0;

void test_config_set_option(void) {
    down_config_t config;
    memset(&config, 0, sizeof(config));

    assert(config_file_set_option(&config, "connections", "16") == 0);
    assert(config.num_workers == 16);

    assert(config_file_set_option(&config, "chunk-size", "2M") == 0);
    assert(config.chunk_size == 2 * 1024 * 1024);

    assert(config_file_set_option(&config, "http3", "true") == 0);
    assert(config.http_version == CURL_HTTP_VERSION_3);

    assert(config_file_set_option(&config, "rate-limit", "10M") == 0);
    assert(config.max_speed_limit == 10 * 1024 * 1024);

    assert(config_file_set_option(&config, "timeout", "45") == 0);
    assert(config.timeout_sec == 45);

    assert(config_file_set_option(&config, "aws-region", "eu-west-1") == 0);
    assert(strcmp(config.aws_region, "eu-west-1") == 0);

    assert(config_file_set_option(&config, "s3-endpoint", "https://r2.cloudflarestorage.com") == 0);
    assert(strcmp(config.s3_endpoint, "https://r2.cloudflarestorage.com") == 0);

    assert(config_file_set_option(&config, "no-color", "yes") == 0);
    assert(config.no_color == true);

    /* Test invalid / unknown option */
    assert(config_file_set_option(&config, "nonexistent-option", "value") == -1);

    config_cleanup(&config);
}

void test_config_file_load_and_override(void) {
    const char *tmp_path = "/tmp/down_test_config.conf";
    FILE *fp = fopen(tmp_path, "w");
    assert(fp != NULL);

    fprintf(fp, "# Test Down Configuration\n");
    fprintf(fp, "; Semicolon comment\n");
    fprintf(fp, "connections = 12\n");
    fprintf(fp, "chunk-size = 1M\n");
    fprintf(fp, "http3 = true\n");
    fprintf(fp, "timeout = 50\n");
    fprintf(fp, "rate-limit = 20M\n");
    fprintf(fp, "aws-region = 'ap-southeast-1'\n");
    fprintf(fp, "user-agent = \"down-custom/1.0\"\n");
    fclose(fp);

    down_config_t config;
    memset(&config, 0, sizeof(config));
    assert(config_file_load(&config, tmp_path) == 0);

    assert(config.num_workers == 12);
    assert(config.chunk_size == 1024 * 1024);
    assert(config.http_version == CURL_HTTP_VERSION_3);
    assert(config.timeout_sec == 50);
    assert(config.max_speed_limit == 20 * 1024 * 1024);
    assert(strcmp(config.aws_region, "ap-southeast-1") == 0);
    assert(strcmp(config.user_agent, "down-custom/1.0") == 0);

    config_cleanup(&config);

    /* Test CLI override: CLI -n 6 should override config file's 12 */
    char *argv[] = {
        "down",
        "--config", (char *)tmp_path,
        "-n", "6",
        "https://example.com/test.bin"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    down_config_t cli_cfg;
    assert(cli_parse_args(argc, argv, &cli_cfg) == 0);

    /* num_workers should be overridden by -n 6 */
    assert(cli_cfg.num_workers == 6);
    /* chunk_size should be inherited from config file (1M) */
    assert(cli_cfg.chunk_size == 1024 * 1024);
    /* http3 should be inherited from config file */
    assert(cli_cfg.http_version == CURL_HTTP_VERSION_3);
    /* timeout should be inherited from config file (50) */
    assert(cli_cfg.timeout_sec == 50);
    /* URL should be parsed */
    assert(strcmp(cli_cfg.url, "https://example.com/test.bin") == 0);

    config_cleanup(&cli_cfg);

    /* Test --no-config ignores file */
    char *argv_no_cfg[] = {
        "down",
        "--no-config",
        "--config", (char *)tmp_path,
        "https://example.com/test.bin"
    };
    int argc_no_cfg = sizeof(argv_no_cfg) / sizeof(argv_no_cfg[0]);
    down_config_t default_cfg;
    assert(cli_parse_args(argc_no_cfg, argv_no_cfg, &default_cfg) == 0);
    /* Should retain default 4 workers instead of 12 */
    assert(default_cfg.num_workers == DEFAULT_NUM_WORKERS);

    config_cleanup(&default_cfg);
    unlink(tmp_path);
}

int main(void) {
    printf("[*] Running test_config_file...\n");
    test_config_set_option();
    test_config_file_load_and_override();
    printf("[+] test_config_file passed successfully!\n");
    return 0;
}
