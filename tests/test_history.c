#include "history.h"
#include "meta.h"
#include <assert.h>
#include <unistd.h>

volatile sig_atomic_t g_shutdown_requested = 0;

int main(void) {
    printf("[*] Running test_history...\n");

    /* Clear any existing history */
    history_clear();

    down_config_t config;
    memset(&config, 0, sizeof(config));
    snprintf(config.url, sizeof(config.url), "https://example.com/ubuntu.iso");
    snprintf(config.output_path, sizeof(config.output_path), "/tmp/test_history_ubuntu.iso");

    /* Subtest 1: Record start */
    assert(history_record_start(&config, 1000000) == 0);

    down_history_entry_t entries[10];
    int count = history_load_all(entries, 10);
    assert(count == 1);
    assert(strcmp(entries[0].url, "https://example.com/ubuntu.iso") == 0);
    assert(strcmp(entries[0].output_path, "/tmp/test_history_ubuntu.iso") == 0);
    assert(entries[0].total_bytes == 1000000);
    assert(entries[0].status == DOWN_STATUS_IN_PROGRESS);

    /* Subtest 2: Record update (interrupted) */
    assert(history_record_update(&config, 400000, 1000000, DOWN_STATUS_INTERRUPTED) == 0);
    count = history_load_all(entries, 10);
    assert(count == 1);
    assert(entries[0].downloaded_bytes == 400000);
    assert(entries[0].status == DOWN_STATUS_INTERRUPTED);

    /* Subtest 3: Resumable detection without control file -> count 0 */
    down_history_entry_t resumable[10];
    int res_count = history_load_resumable(resumable, 10);
    assert(res_count == 0);

    /* Create dummy .down control file */
    char meta_path[1200];
    snprintf(meta_path, sizeof(meta_path), "%s%s", config.output_path, DOWN_META_EXT);
    FILE *mf = fopen(meta_path, "wb");
    assert(mf != NULL);
    down_meta_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, DOWN_META_MAGIC, DOWN_META_MAGIC_LEN);
    hdr.file_size = 1000000;
    hdr.chunk_size = 100000;
    hdr.num_chunks = 10;
    hdr.completed_chunks = 4;
    fwrite(&hdr, 1, sizeof(hdr), mf);
    fclose(mf);

    /* Subtest 4: Resumable detection with control file -> count 1 */
    res_count = history_load_resumable(resumable, 10);
    assert(res_count == 1);
    assert(strcmp(resumable[0].output_path, config.output_path) == 0);
    assert(resumable[0].downloaded_bytes == 400000);
    assert(resumable[0].total_bytes == 1000000);
    assert(resumable[0].has_meta_file == true);

    /* Subtest 5: Discard resumable */
    assert(history_discard_resumable(config.output_path) == 0);
    assert(access(meta_path, F_OK) != 0); /* Control file removed */
    res_count = history_load_resumable(resumable, 10);
    assert(res_count == 0);

    /* Subtest 6: Complete download */
    assert(history_record_complete(&config, 1000000) == 0);
    count = history_load_all(entries, 10);
    assert(count == 1);
    assert(entries[0].status == DOWN_STATUS_COMPLETED);
    assert(entries[0].downloaded_bytes == 1000000);

    /* Clean up */
    history_clear();
    unlink(meta_path);
    unlink(config.output_path);

    printf("[+] test_history passed successfully!\n");
    return 0;
}
