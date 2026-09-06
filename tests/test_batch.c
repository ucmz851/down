#include "batch.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("[*] Running test_batch...\n");

    const char *tmp_path = "/tmp/inlay_test_urls.txt";
    FILE *f = fopen(tmp_path, "w");
    assert(f != NULL);

    fprintf(f, "# Batch download test file\n");
    fprintf(f, "\n");
    fprintf(f, "https://example.com/file1.iso\n");
    fprintf(f, "  out=custom1.iso\n");
    fprintf(f, "  checksum=sha256:1111111111111111111111111111111111111111111111111111111111111111\n");
    fprintf(f, "; another comment\n");
    fprintf(f, "s3://my-bucket/archive.tar.gz   override.tar.gz\n");
    fprintf(f, "https://example.com/plain.bin\n");
    fclose(f);

    batch_queue_t queue;
    batch_queue_init(&queue);

    assert(batch_load_file(tmp_path, &queue) == 0);
    assert(queue.count == 3);

    /* Entry 0 */
    assert(strcmp(queue.entries[0].url, "https://example.com/file1.iso") == 0);
    assert(strcmp(queue.entries[0].output_name, "custom1.iso") == 0);
    assert(strcmp(queue.entries[0].checksum_spec, "sha256:1111111111111111111111111111111111111111111111111111111111111111") == 0);

    /* Entry 1 */
    assert(strcmp(queue.entries[1].url, "s3://my-bucket/archive.tar.gz") == 0);
    assert(strcmp(queue.entries[1].output_name, "override.tar.gz") == 0);

    /* Entry 2 */
    assert(strcmp(queue.entries[2].url, "https://example.com/plain.bin") == 0);
    assert(queue.entries[2].output_name[0] == '\0');

    batch_queue_free(&queue);
    unlink(tmp_path);

    printf("[+] test_batch passed successfully!\n");
    return 0;
}
