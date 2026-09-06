#ifndef INLAY_CONFIG_FILE_H
#define INLAY_CONFIG_FILE_H

#include "inlay.h"

/*
 * Attempts to load configuration from file.
 * If explicit_path is non-NULL, loads from that specific file (errors if file cannot be opened).
 * If explicit_path is NULL, searches standard XDG / home paths:
 *   1. $XDG_CONFIG_HOME/inlay/config
 *   2. ~/.config/inlay/config
 *   3. ~/.inlayrc
 *   4. /etc/inlay/config
 * Returns 0 on success or if no default config file is found.
 * Returns -1 on fatal error (e.g. explicit_path not found or unreadable).
 */
int config_file_load(inlay_config_t *config, const char *explicit_path);

/*
 * Sets a single key=value pair into inlay_config_t.
 * Returns 0 on success, -1 on unknown key or invalid value.
 */
int config_file_set_option(inlay_config_t *config, const char *key, const char *val);

/*
 * Finds the default configuration file path, if one exists.
 * Returns 1 if found (path written to dest), 0 if not found, -1 on buffer overflow.
 */
int config_file_find_default(char *dest, size_t dest_size);

#endif /* INLAY_CONFIG_FILE_H */
