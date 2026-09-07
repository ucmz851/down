#ifndef DOWN_SWARM_H
#define DOWN_SWARM_H

#include "down.h"
#include "batch.h"

/* Execute multiple downloads concurrently using the Elastic Swarm Coordinator.
 *
 * Parameters:
 *   base_config    - Common configuration options (connections, directory, timeout, etc.)
 *   queue          - Queue of URLs to download
 *   max_concurrent - Maximum number of simultaneous downloads (1-16)
 *
 * Returns:
 *   0 if all downloads succeed,
 *   1 if one or more downloads failed,
 *   -2 if interrupted by signal (SIGINT).
 */
int swarm_execute(const down_config_t *base_config, batch_queue_t *queue, int max_concurrent);

#endif /* DOWN_SWARM_H */
