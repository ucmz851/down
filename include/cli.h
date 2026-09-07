#ifndef DOWN_CLI_H
#define DOWN_CLI_H

#include "down.h"

/* Parse command-line arguments into configuration struct */
int cli_parse_args(int argc, char **argv, down_config_t *config);

/* Print help text and usage banner */
void cli_print_usage(const char *prog_name);

/* Print version information */
void cli_print_version(void);

/* Parse byte size string with units like 512K, 1M, 2M, 1.5MB */
uint32_t parse_size_string(const char *str);

/* Parse speed limit string like 500K, 10M, 1G */
uint64_t parse_speed_string(const char *str);

#endif /* DOWN_CLI_H */
