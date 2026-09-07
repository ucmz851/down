#include "config_file.h"
#include "cli.h"
#include <ctype.h>
#include <strings.h>

static void expand_tilde(const char *src, char *dest, size_t dest_size) {
    if (!src || !dest || dest_size == 0) return;
    if (src[0] == '~' && (src[1] == '/' || src[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(dest, dest_size, "%s%s", home, src + 1);
            return;
        }
    }
    snprintf(dest, dest_size, "%s", src);
}

static char *trim_whitespace(char *str) {
    if (!str) return NULL;
    while (*str && isspace((unsigned char)*str)) str++;
    if (!*str) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    /* strip surrounding single or double quotes if present */
    if ((*str == '"' && *end == '"' && end > str) ||
        (*str == '\'' && *end == '\'' && end > str)) {
        str++;
        *end = '\0';
    }
    return str;
}

static bool parse_bool(const char *str, bool default_val) {
    if (!str || !*str) return default_val;
    if (strcasecmp(str, "true") == 0 || strcasecmp(str, "yes") == 0 ||
        strcasecmp(str, "1") == 0 || strcasecmp(str, "on") == 0) {
        return true;
    }
    if (strcasecmp(str, "false") == 0 || strcasecmp(str, "no") == 0 ||
        strcasecmp(str, "0") == 0 || strcasecmp(str, "off") == 0) {
        return false;
    }
    return default_val;
}

int config_file_set_option(down_config_t *config, const char *key, const char *val) {
    if (!config || !key || !*key) return -1;

    if (strcasecmp(key, "connections") == 0 || strcasecmp(key, "workers") == 0 ||
        strcasecmp(key, "num-workers") == 0) {
        int n = atoi(val);
        if (n >= 1 && n <= MAX_NUM_WORKERS) {
            config->num_workers = n;
            return 0;
        }
        return -1;
    } else if (strcasecmp(key, "chunk-size") == 0 || strcasecmp(key, "chunk_size") == 0) {
        config->chunk_size = parse_size_string(val);
        return 0;
    } else if (strcasecmp(key, "dir") == 0 || strcasecmp(key, "download-dir") == 0 ||
               strcasecmp(key, "directory") == 0) {
        expand_tilde(val, config->output_dir, sizeof(config->output_dir));
        return 0;
    } else if (strcasecmp(key, "output") == 0 || strcasecmp(key, "output-path") == 0) {
        expand_tilde(val, config->output_path, sizeof(config->output_path));
        return 0;
    } else if (strcasecmp(key, "timeout") == 0) {
        long t = atol(val);
        if (t > 0) config->timeout_sec = t;
        return 0;
    } else if (strcasecmp(key, "rate-limit") == 0 || strcasecmp(key, "speed-limit") == 0) {
        config->max_speed_limit = parse_speed_string(val);
        return 0;
    } else if (strcasecmp(key, "retry") == 0 || strcasecmp(key, "max-retries") == 0) {
        int r = atoi(val);
        if (r >= 0) config->max_retries = r;
        return 0;
    } else if (strcasecmp(key, "retry-delay") == 0) {
        int d = atoi(val);
        if (d >= 0) config->retry_delay_sec = d;
        return 0;
    } else if (strcasecmp(key, "header") == 0) {
        config->custom_headers = curl_slist_append(config->custom_headers, val);
        return 0;
    } else if (strcasecmp(key, "user-agent") == 0) {
        snprintf(config->user_agent, sizeof(config->user_agent), "%s", val);
        return 0;
    } else if (strcasecmp(key, "insecure") == 0) {
        config->insecure = parse_bool(val, true);
        return 0;
    } else if (strcasecmp(key, "ipv4") == 0) {
        if (parse_bool(val, true)) config->ip_version = CURL_IPRESOLVE_V4;
        return 0;
    } else if (strcasecmp(key, "ipv6") == 0) {
        if (parse_bool(val, true)) config->ip_version = CURL_IPRESOLVE_V6;
        return 0;
    } else if (strcasecmp(key, "http3") == 0) {
        if (parse_bool(val, true)) config->http_version = CURL_HTTP_VERSION_3;
        return 0;
    } else if (strcasecmp(key, "http3-only") == 0) {
        if (parse_bool(val, true)) config->http_version = CURL_HTTP_VERSION_3ONLY;
        return 0;
    } else if (strcasecmp(key, "static") == 0) {
        config->use_static = parse_bool(val, true);
        return 0;
    } else if (strcasecmp(key, "no-fallocate") == 0) {
        config->no_fallocate = parse_bool(val, true);
        return 0;
    } else if (strcasecmp(key, "force-single") == 0) {
        config->force_single_stream = parse_bool(val, true);
        return 0;
    } else if (strcasecmp(key, "quiet") == 0) {
        config->quiet = parse_bool(val, true);
        return 0;
    } else if (strcasecmp(key, "verbose") == 0) {
        config->verbose = parse_bool(val, true);
        return 0;
    } else if (strcasecmp(key, "no-color") == 0) {
        config->no_color = parse_bool(val, true);
        return 0;
    } else if (strcasecmp(key, "aws-sigv4") == 0) {
        config->aws_sigv4_enabled = true;
        if (val && *val && strcasecmp(val, "true") != 0 && strcasecmp(val, "1") != 0) {
            snprintf(config->aws_sigv4_provider, sizeof(config->aws_sigv4_provider), "%s", val);
        }
        return 0;
    } else if (strcasecmp(key, "aws-region") == 0) {
        snprintf(config->aws_region, sizeof(config->aws_region), "%s", val);
        return 0;
    } else if (strcasecmp(key, "aws-access-key") == 0) {
        snprintf(config->aws_access_key, sizeof(config->aws_access_key), "%s", val);
        return 0;
    } else if (strcasecmp(key, "aws-secret-key") == 0) {
        snprintf(config->aws_secret_key, sizeof(config->aws_secret_key), "%s", val);
        return 0;
    } else if (strcasecmp(key, "aws-token") == 0) {
        snprintf(config->aws_token, sizeof(config->aws_token), "%s", val);
        return 0;
    } else if (strcasecmp(key, "s3-endpoint") == 0 || strcasecmp(key, "endpoint-url") == 0) {
        snprintf(config->s3_endpoint, sizeof(config->s3_endpoint), "%s", val);
        return 0;
    }

    return -1;
}

int config_file_find_default(char *dest, size_t dest_size) {
    if (!dest || dest_size == 0) return 0;

    /* 1. $XDG_CONFIG_HOME/down/config (or inlay) */
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        snprintf(dest, dest_size, "%s/down/config", xdg);
        if (access(dest, R_OK) == 0) return 1;
        snprintf(dest, dest_size, "%s/inlay/config", xdg);
        if (access(dest, R_OK) == 0) return 1;
    }

    /* 2. ~/.config/down/config */
    const char *home = getenv("HOME");
    if (home && *home) {
#if defined(__APPLE__)
        /* macOS standard Application Support location */
        snprintf(dest, dest_size, "%s/Library/Application Support/down/config", home);
        if (access(dest, R_OK) == 0) return 1;
        snprintf(dest, dest_size, "%s/Library/Application Support/inlay/config", home);
        if (access(dest, R_OK) == 0) return 1;
#endif

        snprintf(dest, dest_size, "%s/.config/down/config", home);
        if (access(dest, R_OK) == 0) return 1;
        snprintf(dest, dest_size, "%s/.config/inlay/config", home);
        if (access(dest, R_OK) == 0) return 1;

        /* 3. ~/.downrc */
        snprintf(dest, dest_size, "%s/.downrc", home);
        if (access(dest, R_OK) == 0) return 1;
        snprintf(dest, dest_size, "%s/.inlayrc", home);
        if (access(dest, R_OK) == 0) return 1;
    }

    /* 4. /etc/down/config */
    if (access("/etc/down/config", R_OK) == 0) {
        snprintf(dest, dest_size, "/etc/down/config");
        return 1;
    }
    if (access("/etc/inlay/config", R_OK) == 0) {
        snprintf(dest, dest_size, "/etc/inlay/config");
        return 1;
    }

    return 0;
}

int config_file_load(down_config_t *config, const char *explicit_path) {
    if (!config) return -1;

    char resolved_path[1024] = {0};
    if (explicit_path && *explicit_path) {
        expand_tilde(explicit_path, resolved_path, sizeof(resolved_path));
        if (access(resolved_path, R_OK) != 0) {
            fprintf(stderr, "[!] Error: configuration file not found or unreadable: '%s'\n", resolved_path);
            return -1;
        }
    } else {
        if (!config_file_find_default(resolved_path, sizeof(resolved_path))) {
            return 0; /* No config file found, nothing to load */
        }
    }

    FILE *fp = fopen(resolved_path, "r");
    if (!fp) {
        if (explicit_path) {
            fprintf(stderr, "[!] Error: cannot open configuration file '%s': %s\n",
                    resolved_path, strerror(errno));
            return -1;
        }
        return 0;
    }

    char line[2048];
    int line_num = 0;
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        char *p = trim_whitespace(line);
        if (!p || *p == '\0' || *p == '#' || *p == ';') {
            continue; /* blank or comment */
        }

        /* Split on '=' or ':' */
        char *sep = strchr(p, '=');
        if (!sep) sep = strchr(p, ':');
        if (!sep) {
            /* Boolean flag without value, e.g. "http3" or "verbose" */
            config_file_set_option(config, p, "true");
            continue;
        }

        *sep = '\0';
        char *key = trim_whitespace(p);
        char *val = trim_whitespace(sep + 1);

        if (config_file_set_option(config, key, val) != 0) {
            if (config->verbose) {
                fprintf(stderr, "[*] Note: ignoring unrecognized config option '%s' at %s:%d\n",
                        key, resolved_path, line_num);
            }
        }
    }

    fclose(fp);
    return 0;
}
