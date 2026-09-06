#include "checksum.h"
#include <openssl/evp.h>
#include <ctype.h>
#include <strings.h>

#define READ_CHUNK_SIZE (1024 * 1024) /* 1 MB sequential buffer */

static const char *normalize_algo_name(const char *raw) {
    if (!raw) return NULL;
    if (strcasecmp(raw, "sha256") == 0 || strcasecmp(raw, "sha-256") == 0) {
        return "sha256";
    }
    if (strcasecmp(raw, "sha512") == 0 || strcasecmp(raw, "sha-512") == 0) {
        return "sha512";
    }
    if (strcasecmp(raw, "sha1") == 0 || strcasecmp(raw, "sha-1") == 0) {
        return "sha1";
    }
    if (strcasecmp(raw, "md5") == 0) {
        return "md5";
    }
    if (strcasecmp(raw, "blake2b") == 0 || strcasecmp(raw, "blake2b512") == 0) {
        return "blake2b512";
    }
    if (strcasecmp(raw, "blake2s") == 0 || strcasecmp(raw, "blake2s256") == 0) {
        return "blake2s256";
    }
    return raw;
}

static bool is_valid_hex_string(const char *s) {
    if (!s || !*s) return false;
    for (const char *p = s; *p; p++) {
        if (!isxdigit((unsigned char)*p)) {
            return false;
        }
    }
    return true;
}

int checksum_parse_spec(const char *spec, char *algo_out, size_t algo_sz,
                        char *expected_hex_out, size_t hex_sz) {
    if (!spec || !algo_out || !expected_hex_out || algo_sz == 0 || hex_sz == 0) {
        return -1;
    }

    const char *colon = strchr(spec, ':');
    if (colon) {
        size_t a_len = (size_t)(colon - spec);
        if (a_len == 0 || a_len >= algo_sz) return -1;
        char temp_algo[64];
        if (a_len >= sizeof(temp_algo)) return -1;
        memcpy(temp_algo, spec, a_len);
        temp_algo[a_len] = '\0';

        const char *norm = normalize_algo_name(temp_algo);
        snprintf(algo_out, algo_sz, "%s", norm);

        const char *hex_part = colon + 1;
        while (isspace((unsigned char)*hex_part)) hex_part++;
        if (!is_valid_hex_string(hex_part)) return -1;

        size_t h_len = strlen(hex_part);
        if (h_len >= hex_sz) return -1;

        for (size_t i = 0; i < h_len; i++) {
            expected_hex_out[i] = (char)tolower((unsigned char)hex_part[i]);
        }
        expected_hex_out[h_len] = '\0';
        return 0;
    }

    /* Auto-detect by string length if no colon prefix */
    const char *hex_part = spec;
    while (isspace((unsigned char)*hex_part)) hex_part++;
    if (!is_valid_hex_string(hex_part)) return -1;

    size_t len = strlen(hex_part);
    if (len >= hex_sz) return -1;

    const char *detected = NULL;
    if (len == 32) detected = "md5";
    else if (len == 40) detected = "sha1";
    else if (len == 64) detected = "sha256";
    else if (len == 128) detected = "sha512";
    else return -1;

    snprintf(algo_out, algo_sz, "%s", detected);
    for (size_t i = 0; i < len; i++) {
        expected_hex_out[i] = (char)tolower((unsigned char)hex_part[i]);
    }
    expected_hex_out[len] = '\0';
    return 0;
}

int checksum_compute_file(const char *file_path, const char *algo,
                          char *hex_out, size_t hex_sz) {
    if (!file_path || !algo || !hex_out || hex_sz == 0) {
        return -1;
    }

    const char *norm_algo = normalize_algo_name(algo);
    const EVP_MD *md = EVP_get_digestbyname(norm_algo);
    if (!md) {
        fprintf(stderr, "[!] Error: unsupported digest algorithm '%s'\n", algo);
        return -1;
    }

    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "[!] Error opening file for checksum calculation '%s': %s\n",
                file_path, strerror(errno));
        return -1;
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        close(fd);
        return -1;
    }

    if (EVP_DigestInit_ex(ctx, md, NULL) != 1) {
        EVP_MD_CTX_free(ctx);
        close(fd);
        return -1;
    }

    uint8_t *buffer = (uint8_t *)malloc(READ_CHUNK_SIZE);
    if (!buffer) {
        EVP_MD_CTX_free(ctx);
        close(fd);
        return -1;
    }

    ssize_t bytes_read = 0;
    while ((bytes_read = read(fd, buffer, READ_CHUNK_SIZE)) > 0) {
        if (EVP_DigestUpdate(ctx, buffer, (size_t)bytes_read) != 1) {
            free(buffer);
            EVP_MD_CTX_free(ctx);
            close(fd);
            return -1;
        }
    }

    free(buffer);
    close(fd);

    if (bytes_read < 0) {
        EVP_MD_CTX_free(ctx);
        return -1;
    }

    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;
    if (EVP_DigestFinal_ex(ctx, md_value, &md_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return -1;
    }
    EVP_MD_CTX_free(ctx);

    if (hex_sz < (size_t)(md_len * 2 + 1)) {
        return -1;
    }

    for (unsigned int i = 0; i < md_len; i++) {
        snprintf(hex_out + (i * 2), 3, "%02x", md_value[i]);
    }
    hex_out[md_len * 2] = '\0';

    return 0;
}

int checksum_verify_file(const char *file_path, const char *algo,
                         const char *expected_hex,
                         char *actual_hex_out, size_t actual_hex_sz) {
    char computed_hex[256];
    if (checksum_compute_file(file_path, algo, computed_hex, sizeof(computed_hex)) != 0) {
        return -1;
    }

    if (actual_hex_out && actual_hex_sz > 0) {
        snprintf(actual_hex_out, actual_hex_sz, "%s", computed_hex);
    }

    if (strcasecmp(computed_hex, expected_hex) == 0) {
        return 0; /* Verified match */
    }

    return 1; /* Mismatch */
}
