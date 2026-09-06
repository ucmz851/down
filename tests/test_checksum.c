#include "checksum.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("[*] Running test_checksum...\n");

    /* 1. Test checksum_parse_spec */
    char algo[64];
    char expected[256];

    assert(checksum_parse_spec("sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                               algo, sizeof(algo), expected, sizeof(expected)) == 0);
    assert(strcmp(algo, "sha256") == 0);
    assert(strcmp(expected, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);

    /* Case-insensitivity */
    assert(checksum_parse_spec("SHA-256:E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
                               algo, sizeof(algo), expected, sizeof(expected)) == 0);
    assert(strcmp(algo, "sha256") == 0);
    assert(strcmp(expected, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);

    /* MD5 */
    assert(checksum_parse_spec("md5:d41d8cd98f00b204e9800998ecf8427e",
                               algo, sizeof(algo), expected, sizeof(expected)) == 0);
    assert(strcmp(algo, "md5") == 0);
    assert(strcmp(expected, "d41d8cd98f00b204e9800998ecf8427e") == 0);

    /* Auto-detect 64-char sha256 */
    assert(checksum_parse_spec("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                               algo, sizeof(algo), expected, sizeof(expected)) == 0);
    assert(strcmp(algo, "sha256") == 0);

    /* Auto-detect 32-char md5 */
    assert(checksum_parse_spec("d41d8cd98f00b204e9800998ecf8427e",
                               algo, sizeof(algo), expected, sizeof(expected)) == 0);
    assert(strcmp(algo, "md5") == 0);

    /* 2. File hashing test: create known file */
    const char *tmp_file = "/tmp/inlay_test_checksum.tmp";
    FILE *f = fopen(tmp_file, "wb");
    assert(f != NULL);
    const char *test_data = "Hello, Inlay Checksum Verification!";
    fwrite(test_data, 1, strlen(test_data), f);
    fclose(f);

    char computed_sha256[256];
    assert(checksum_compute_file(tmp_file, "sha256", computed_sha256, sizeof(computed_sha256)) == 0);

    char actual_hex[256];
    /* Positive verification */
    assert(checksum_verify_file(tmp_file, "sha256", computed_sha256, actual_hex, sizeof(actual_hex)) == 0);
    assert(strcmp(actual_hex, computed_sha256) == 0);

    /* Negative verification (mismatch) */
    assert(checksum_verify_file(tmp_file, "sha256", "0000000000000000000000000000000000000000000000000000000000000000",
                                actual_hex, sizeof(actual_hex)) == 1);

    unlink(tmp_file);
    printf("[+] test_checksum passed successfully!\n");
    return 0;
}
