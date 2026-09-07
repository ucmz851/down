#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include "scheduler.h"
#include "meta.h"

#define TEST_SCHED_TARGET "test_sched_file.bin"
#define CHUNK_SIZE (512 * 1024)
#define NUM_CHUNKS 8
#define TOTAL_SIZE (NUM_CHUNKS * CHUNK_SIZE)

int main(void) {
    printf("[*] Running test_scheduler...\n");

    const char *url = "http://example.com/test_sched.bin";
    down_meta_t meta;
    unlink(TEST_SCHED_TARGET DOWN_META_EXT);

    int ret = meta_open(&meta, TEST_SCHED_TARGET, url, TOTAL_SIZE, CHUNK_SIZE, false);
    assert(ret == 0);

    down_scheduler_t sched;
    ret = scheduler_init(&sched, &meta, TOTAL_SIZE, CHUNK_SIZE, 2, false);
    assert(ret == 0);
    assert(sched.num_chunks == NUM_CHUNKS);

    uint64_t w0_start = 0, w0_end = 0;
    uint64_t w1_start = 0, w1_end = 0;

    /* Worker 0 claims first batch (chunks 0 to 3) */
    sched_result_t res0 = scheduler_get_work(&sched, 0, &w0_start, &w0_end);
    assert(res0 == SCHED_WORK_ASSIGNED);
    assert(w0_start == 0);
    assert(w0_end == (4ULL * CHUNK_SIZE - 1));

    /* Worker 1 claims second batch (chunks 4 to 7) */
    sched_result_t res1 = scheduler_get_work(&sched, 1, &w1_start, &w1_end);
    assert(res1 == SCHED_WORK_ASSIGNED);
    assert(w1_start == (4ULL * CHUNK_SIZE));
    assert(w1_end == (8ULL * CHUNK_SIZE - 1));

    /* Worker 0 rapidly completes chunks 0, 1, 2, 3 */
    for (uint32_t c = 0; c < 4; c++) {
        scheduler_chunk_completed(&sched, c);
    }
    scheduler_update_progress(&sched, 0, 4ULL * CHUNK_SIZE);

    /* Worker 1 is a "lagger": it has only downloaded 0 bytes and is still at chunk 4 */
    scheduler_update_progress(&sched, 1, 4ULL * CHUNK_SIZE);

    /* Worker 0 asks for more work. Since all chunks are claimed, it should bisect lagger Worker 1! */
    uint64_t w0_steal_start = 0, w0_steal_end = 0;
    sched_result_t res_steal = scheduler_get_work(&sched, 0, &w0_steal_start, &w0_steal_end);
    assert(res_steal == SCHED_WORK_ASSIGNED);

    /* Worker 1 had chunks [4, 7] (2 MB). Midpoint should be chunk 6!
       Stolen range should be chunks [6, 7] -> [6 * CHUNK_SIZE, 8 * CHUNK_SIZE - 1] */
    printf("[*] Lagger bisection result: Worker 0 stole bytes %" PRIu64 " to %" PRIu64 "\n",
           w0_steal_start, w0_steal_end);
    assert(w0_steal_start == 6ULL * CHUNK_SIZE);
    assert(w0_steal_end == (8ULL * CHUNK_SIZE - 1));

    /* Verify Worker 1's assigned end was bisected down to chunk 5 (6 * CHUNK_SIZE - 1) */
    assert(sched.workers[1].assigned_end == (6ULL * CHUNK_SIZE - 1));

    /* Complete remaining chunks: 4, 5 (by Worker 1) and 6, 7 (by Worker 0) */
    scheduler_chunk_completed(&sched, 4);
    scheduler_chunk_completed(&sched, 5);
    scheduler_chunk_completed(&sched, 6);
    scheduler_chunk_completed(&sched, 7);

    /* Now all work is done */
    uint64_t dummy_s, dummy_e;
    sched_result_t res_final = scheduler_get_work(&sched, 0, &dummy_s, &dummy_e);
    assert(res_final == SCHED_WORK_DONE);

    scheduler_destroy(&sched);
    meta_remove(&meta);

    printf("[+] test_scheduler passed successfully: dynamic work-stealing & lagger-bisection verified!\n");
    return 0;
}
