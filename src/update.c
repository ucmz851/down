#include "update.h"
#include <sys/utsname.h>
#include <libgen.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} memory_buffer_t;

static size_t memory_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    memory_buffer_t *mem = (memory_buffer_t *)userdata;

    if (mem->size + total + 1 > mem->capacity) {
        size_t new_cap = (mem->capacity == 0) ? 16384 : mem->capacity * 2;
        while (new_cap < mem->size + total + 1) new_cap *= 2;
        char *new_data = (char *)realloc(mem->data, new_cap);
        if (!new_data) return 0;
        mem->data = new_data;
        mem->capacity = new_cap;
    }

    memcpy(mem->data + mem->size, ptr, total);
    mem->size += total;
    mem->data[mem->size] = '\0';
    return total;
}

int version_compare(const char *v1, const char *v2) {
    if (!v1 || !v2) return 0;
    while (*v1 == 'v' || *v1 == 'V') v1++;
    while (*v2 == 'v' || *v2 == 'V') v2++;

    int maj1 = 0, min1 = 0, pat1 = 0;
    int maj2 = 0, min2 = 0, pat2 = 0;

    sscanf(v1, "%d.%d.%d", &maj1, &min1, &pat1);
    sscanf(v2, "%d.%d.%d", &maj2, &min2, &pat2);

    if (maj1 != maj2) return maj1 - maj2;
    if (min1 != min2) return min1 - min2;
    return pat1 - pat2;
}

static int extract_json_field(const char *json, const char *field, char *dest, size_t dest_size) {
    dest[0] = '\0';
    char search_pattern[64];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", field);

    const char *pos = strstr(json, search_pattern);
    if (!pos) return -1;

    pos += strlen(search_pattern);
    while (*pos && (*pos == ' ' || *pos == ':' || *pos == '\t')) pos++;

    if (*pos != '"') return -1;
    pos++; /* skip opening quote */

    const char *end = strchr(pos, '"');
    if (!end) return -1;

    size_t len = (size_t)(end - pos);
    if (len >= dest_size) len = dest_size - 1;
    memcpy(dest, pos, len);
    dest[len] = '\0';

    return 0;
}

int update_check_and_apply(bool auto_install) {
    printf("[*] Checking GitHub for updates (current: v%s)...\n", DOWN_VERSION);

    char tag_name[64] = {0};
    bool tag_found = false;

    /* 1. Try GitHub Releases API */
    CURL *curl = curl_easy_init();
    if (curl) {
        memory_buffer_t body = { .data = NULL, .size = 0, .capacity = 0 };
        struct curl_slist *headers = NULL;

        const char *token = getenv("GITHUB_TOKEN");
        if (!token) token = getenv("GH_TOKEN");
        if (token && *token) {
            char auth_header[256];
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
            headers = curl_slist_append(headers, auth_header);
        }

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/ucmz851/down/releases/latest");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "down/" DOWN_VERSION);
        if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, memory_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);
        if (headers) curl_slist_free_all(headers);

        if (res == CURLE_OK && http_code == 200 && body.data) {
            if (extract_json_field(body.data, "tag_name", tag_name, sizeof(tag_name)) == 0) {
                tag_found = true;
            }
        }
        if (body.data) free(body.data);
    }

    /* 2. Fallback: Query release redirect URL (bypasses GitHub unauthenticated API rate limits) */
    if (!tag_found) {
        CURL *redir_curl = curl_easy_init();
        if (redir_curl) {
            curl_easy_setopt(redir_curl, CURLOPT_URL, "https://github.com/ucmz851/down/releases/latest");
            curl_easy_setopt(redir_curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(redir_curl, CURLOPT_NOBODY, 1L);
            curl_easy_setopt(redir_curl, CURLOPT_TIMEOUT, 10L);
            curl_easy_setopt(redir_curl, CURLOPT_USERAGENT, "down/" DOWN_VERSION);

            if (curl_easy_perform(redir_curl) == CURLE_OK) {
                char *eff_url = NULL;
                curl_easy_getinfo(redir_curl, CURLINFO_EFFECTIVE_URL, &eff_url);
                if (eff_url) {
                    const char *tag_slash = strrchr(eff_url, '/');
                    if (tag_slash && strlen(tag_slash + 1) > 0 && strcmp(tag_slash + 1, "latest") != 0) {
                        snprintf(tag_name, sizeof(tag_name), "%s", tag_slash + 1);
                        tag_found = true;
                    }
                }
            }
            curl_easy_cleanup(redir_curl);
        }
    }

    if (!tag_found) {
        fprintf(stderr, "[!] Error: could not query release information from GitHub (rate limit or network error)\n");
        return -1;
    }

    const char *clean_tag = tag_name;
    if (*clean_tag == 'v' || *clean_tag == 'V') clean_tag++;

    int cmp = version_compare(clean_tag, DOWN_VERSION);
    if (cmp <= 0) {
        printf("[✓] down is already up to date (v%s)\n", DOWN_VERSION);
        return 0;
    }

    printf("\n[*] A new release of down is available: \033[1;32mv%s\033[0m (installed: v%s)\n",
           clean_tag, DOWN_VERSION);

    if (!auto_install) {
        printf("    Run 'down --update' or rerun the installer script to upgrade:\n");
        printf("    curl -fsSL https://raw.githubusercontent.com/ucmz851/down/main/install.sh | bash\n\n");
        return 1;
    }

    /* Prompt user if running in an interactive terminal */
    if (isatty(STDIN_FILENO)) {
        printf("Would you like to update to v%s now? [Y/n] ", clean_tag);
        fflush(stdout);

        char answer[32];
        if (fgets(answer, sizeof(answer), stdin)) {
            if (answer[0] == 'n' || answer[0] == 'N') {
                printf("[*] Update canceled by user.\n");
                return 0;
            }
        }
    }

    /* 1. Identify current binary path */
    char exe_path[1024];
#if defined(__APPLE__)
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) != 0) {
        fprintf(stderr, "[!] Could not determine path to running binary\n");
        return -1;
    }
    char real_exe_path[1024];
    if (realpath(exe_path, real_exe_path) != NULL) {
        strncpy(exe_path, real_exe_path, sizeof(exe_path) - 1);
        exe_path[sizeof(exe_path) - 1] = '\0';
    }
#else
    ssize_t link_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (link_len <= 0) {
        fprintf(stderr, "[!] Could not determine path to running binary\n");
        return -1;
    }
    exe_path[link_len] = '\0';
#endif

    /* 2. Test directory write permissions */
    char exe_dir_copy[1024];
    snprintf(exe_dir_copy, sizeof(exe_dir_copy), "%s", exe_path);
    char *dir = dirname(exe_dir_copy);

    if (access(dir, W_OK) != 0) {
        fprintf(stderr, "\n[!] Permission denied writing to directory: %s\n", dir);
        fprintf(stderr, "    Please rerun with sudo: sudo down --update\n");
        fprintf(stderr, "    Or use the installer: curl -fsSL https://raw.githubusercontent.com/ucmz851/down/main/install.sh | bash\n\n");
        return -1;
    }

    /* 3. Detect operating system and architecture */
    struct utsname u;
    if (uname(&u) != 0) {
        fprintf(stderr, "[!] Could not detect system architecture\n");
        return -1;
    }

#if defined(__APPLE__)
    const char *os_name = "darwin";
#else
    const char *os_name = "linux";
#endif

    const char *arch = "amd64";
    if (strcmp(u.machine, "x86_64") == 0) {
        arch = "amd64";
    } else if (strcmp(u.machine, "aarch64") == 0 || strcmp(u.machine, "arm64") == 0) {
        arch = "arm64";
    }

    /* 4. Construct release tarball URL */
    char download_url[512];
    snprintf(download_url, sizeof(download_url),
             "https://github.com/ucmz851/down/releases/download/v%s/down-v%s-%s-%s.tar.gz",
             clean_tag, clean_tag, os_name, arch);

    char tmp_tar[512];
    snprintf(tmp_tar, sizeof(tmp_tar), "/tmp/down_update_%d.tar.gz", (int)getpid());

    char tmp_extract_dir[512];
    snprintf(tmp_extract_dir, sizeof(tmp_extract_dir), "/tmp/down_update_%d_dir", (int)getpid());

    mkdir(tmp_extract_dir, 0755);

    printf("[*] Downloading v%s from %s...\n", clean_tag, download_url);

    FILE *fp = fopen(tmp_tar, "wb");
    if (!fp) {
        fprintf(stderr, "[!] Failed to create temporary file '%s': %s\n", tmp_tar, strerror(errno));
        return -1;
    }

    CURL *dl_curl = curl_easy_init();
    if (!dl_curl) {
        fclose(fp);
        unlink(tmp_tar);
        return -1;
    }

    curl_easy_setopt(dl_curl, CURLOPT_URL, download_url);
    curl_easy_setopt(dl_curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(dl_curl, CURLOPT_USERAGENT, "down/" DOWN_VERSION);
    curl_easy_setopt(dl_curl, CURLOPT_WRITEDATA, fp);

    CURLcode dl_res = curl_easy_perform(dl_curl);
    long dl_http_code = 0;
    curl_easy_getinfo(dl_curl, CURLINFO_RESPONSE_CODE, &dl_http_code);
    curl_easy_cleanup(dl_curl);
    fclose(fp);

    if (dl_res != CURLE_OK || dl_http_code != 200) {
        fprintf(stderr, "[!] Failed to download release asset (HTTP %ld: %s)\n",
                dl_http_code, curl_easy_strerror(dl_res));
        unlink(tmp_tar);
        rmdir(tmp_extract_dir);
        return -1;
    }

    /* 5. Extract tarball using tar command */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "tar -xzf '%s' -C '%s'", tmp_tar, tmp_extract_dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "[!] Failed to extract downloaded update archive\n");
        unlink(tmp_tar);
        return -1;
    }

    unlink(tmp_tar);

    /* 6. Verify extracted binary and replace running executable */
    char new_bin[1024];
    snprintf(new_bin, sizeof(new_bin), "%s/down", tmp_extract_dir);

    if (access(new_bin, X_OK) != 0) {
        /* Fallback check for inlay binary name in old releases */
        snprintf(new_bin, sizeof(new_bin), "%s/inlay", tmp_extract_dir);
        if (access(new_bin, X_OK) != 0) {
            fprintf(stderr, "[!] Downloaded package did not contain executable 'down'\n");
            return -1;
        }
    }

    char staged_path[1200];
    snprintf(staged_path, sizeof(staged_path), "%s.new.%d", exe_path, (int)getpid());

    /* Copy to staged path */
    int src_fd = open(new_bin, O_RDONLY);
    int dst_fd = open(staged_path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (src_fd < 0 || dst_fd < 0) {
        fprintf(stderr, "[!] Failed to stage new binary: %s\n", strerror(errno));
        if (src_fd >= 0) close(src_fd);
        if (dst_fd >= 0) close(dst_fd);
        return -1;
    }

    char buffer[65536];
    ssize_t bytes;
    while ((bytes = read(src_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t written = 0;
        while (written < bytes) {
            ssize_t w = write(dst_fd, buffer + written, (size_t)(bytes - written));
            if (w < 0) break;
            written += w;
        }
    }
    close(src_fd);
    close(dst_fd);

    /* Atomic rename replaces running binary on Linux */
    if (rename(staged_path, exe_path) != 0) {
        fprintf(stderr, "[!] Failed to overwrite '%s': %s\n", exe_path, strerror(errno));
        unlink(staged_path);
        return -1;
    }

    unlink(new_bin);
    rmdir(tmp_extract_dir);

    printf("\n\033[1;32m[✓] Successfully updated down to v%s!\033[0m\n", clean_tag);
    printf("    Installed executable: %s\n\n", exe_path);

    return 0;
}
