#include "interactive.h"
#include "history.h"
#include "meta.h"
#include "cli.h"
#include <assert.h>
#include <unistd.h>

volatile sig_atomic_t g_shutdown_requested = 0;

static void set_simulated_stdin(const char *input) {
    int fds[2];
    if (pipe(fds) != 0) {
        perror("pipe");
        exit(1);
    }
    size_t len = strlen(input);
    if (write(fds[1], input, len) != (ssize_t)len) {
        perror("write");
        exit(1);
    }
    close(fds[1]);
    if (dup2(fds[0], STDIN_FILENO) == -1) {
        perror("dup2");
        exit(1);
    }
    close(fds[0]);
    clearerr(stdin);
}

int main(void) {
    printf("[*] Running test_interactive...\n");

    history_clear();
    unlink("/tmp/test_wizard_resume.bin");
    unlink("/tmp/test_wizard_resume.bin.down");

    /* Subtest 1: Quick Start Mode */
    {
        set_simulated_stdin("https://example.com/ubuntu-24.04.iso\n1\n");
        down_config_t config;
        memset(&config, 0, sizeof(config));
        config.num_workers = 4;
        config.chunk_size = DEFAULT_CHUNK_SIZE;

        int res = interactive_run_wizard(&config);
        assert(res == 0);
        assert(strcmp(config.url, "https://example.com/ubuntu-24.04.iso") == 0);
        assert(strcmp(config.output_dir, ".") == 0);
        assert(config.num_workers == 4);
    }

    /* Subtest 2: Auto-prepend https:// to URLs without scheme */
    {
        set_simulated_stdin("releases.ubuntu.com/noble/ubuntu.iso\n1\n");
        down_config_t config;
        memset(&config, 0, sizeof(config));
        config.num_workers = 4;
        config.chunk_size = DEFAULT_CHUNK_SIZE;

        int res = interactive_run_wizard(&config);
        assert(res == 0);
        assert(strcmp(config.url, "https://releases.ubuntu.com/noble/ubuntu.iso") == 0);
    }

    /* Subtest 3: Advanced Mode Customization */
    {
        const char *adv_input =
            "https://test.io/bigfile.bin\n" /* URL */
            "2\n"                           /* Advanced Mode */
            "/tmp/down_target\n"            /* Dir */
            "my_renamed_file.bin\n"         /* Filename */
            "12\n"                          /* Connections */
            "1M\n"                          /* Chunk size */
            "2M\n"                          /* Rate limit */
            "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n"; /* Checksum */

        set_simulated_stdin(adv_input);
        down_config_t config;
        memset(&config, 0, sizeof(config));
        config.num_workers = 4;
        config.chunk_size = DEFAULT_CHUNK_SIZE;

        int res = interactive_run_wizard(&config);
        assert(res == 0);
        assert(strcmp(config.url, "https://test.io/bigfile.bin") == 0);
        assert(strcmp(config.output_dir, "/tmp/down_target") == 0);
        assert(strcmp(config.output_path, "my_renamed_file.bin") == 0);
        assert(config.num_workers == 12);
        assert(config.chunk_size == 1024 * 1024);
        assert(config.max_speed_limit == 2 * 1024 * 1024);
        assert(strcmp(config.checksum_algo, "sha256") == 0);
        assert(strcmp(config.expected_checksum, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    }

    /* Subtest 4: EOF Abort Handling */
    {
        set_simulated_stdin("");
        down_config_t config;
        memset(&config, 0, sizeof(config));

        int res = interactive_run_wizard(&config);
        assert(res == -1);
    }

    /* Subtest 5: Resume Interrupted Download Detection via Wizard */
    {
        /* Prepare interrupted download in history */
        down_config_t rec;
        memset(&rec, 0, sizeof(rec));
        snprintf(rec.url, sizeof(rec.url), "https://archive.org/file_to_resume.bin");
        snprintf(rec.output_path, sizeof(rec.output_path), "/tmp/test_wizard_resume.bin");
        history_record_start(&rec, 5000000);
        history_record_update(&rec, 2500000, 5000000, DOWN_STATUS_INTERRUPTED);

        /* Create mock .down file */
        char meta_path[1200];
        snprintf(meta_path, sizeof(meta_path), "%s%s", rec.output_path, DOWN_META_EXT);
        FILE *mf = fopen(meta_path, "wb");
        assert(mf != NULL);
        down_meta_hdr_t hdr;
        memset(&hdr, 0, sizeof(hdr));
        memcpy(hdr.magic, DOWN_META_MAGIC, DOWN_META_MAGIC_LEN);
        hdr.file_size = 5000000;
        hdr.chunk_size = 500000;
        hdr.num_chunks = 10;
        hdr.completed_chunks = 5;
        fwrite(&hdr, 1, sizeof(hdr), mf);
        fclose(mf);

        /* Simulate pressing Enter (option 1: Resume) */
        set_simulated_stdin("1\n");
        down_config_t config;
        memset(&config, 0, sizeof(config));

        int res = interactive_run_wizard(&config);
        assert(res == 0);
        assert(strcmp(config.url, "https://archive.org/file_to_resume.bin") == 0);
        assert(strcmp(config.output_path, "/tmp/test_wizard_resume.bin") == 0);
        assert(config.resume_mode == true);

        /* Cleanup */
        history_clear();
        unlink(meta_path);
    }

    printf("[+] test_interactive passed successfully!\n");
    return 0;
}
