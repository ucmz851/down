#ifndef DOWN_CHECKSUM_H
#define DOWN_CHECKSUM_H

#include "down.h"

/* Parse a checksum specification such as "sha256:<hex>" or raw hex.
 * Auto-detects algorithm by length if prefix is omitted:
 *   32 hex chars -> md5
 *   40 hex chars -> sha1
 *   64 hex chars -> sha256
 *  128 hex chars -> sha512
 * Returns 0 on success, -1 on failure. */
int checksum_parse_spec(const char *spec, char *algo_out, size_t algo_sz,
                        char *expected_hex_out, size_t hex_sz);

/* Compute cryptographic hash of a file on disk using OpenSSL EVP.
 * Returns 0 on success, -1 on error. */
int checksum_compute_file(const char *file_path, const char *algo,
                          char *hex_out, size_t hex_sz);

/* Verify file on disk against expected checksum string (case-insensitive).
 * Returns:
 *   0: Match
 *   1: Mismatch
 *  -1: Read or algorithm error
 * Populates actual_hex_out if non-NULL. */
int checksum_verify_file(const char *file_path, const char *algo,
                         const char *expected_hex,
                         char *actual_hex_out, size_t actual_hex_sz);

#endif /* DOWN_CHECKSUM_H */
