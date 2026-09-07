#ifndef DOWN_INTERACTIVE_H
#define DOWN_INTERACTIVE_H

#include "down.h"

/* Run the interactive setup wizard.
 * Prompts user for download URL (if not already set) and configuration
 * (Quick Start with defaults or Advanced setup for connections, path, etc.).
 *
 * Returns:
 *   0 on success (ready to proceed with download)
 *  -1 on cancellation, EOF, or error
 */
int interactive_run_wizard(down_config_t *config);

#endif /* DOWN_INTERACTIVE_H */
