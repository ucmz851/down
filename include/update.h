#ifndef INLAY_UPDATE_H
#define INLAY_UPDATE_H

#include "inlay.h"

/* Compare two semantic version strings (e.g. "0.0.1" and "0.0.2").
 * Returns:
 *   > 0 if v1 > v2
 *   = 0 if v1 == v2
 *   < 0 if v1 < v2
 */
int version_compare(const char *v1, const char *v2);

/* Check GitHub Releases for newer version of inlay.
 * If auto_install is true, prompts user and upgrades executable in-place.
 * Returns:
 *   0 on success (either updated or already up-to-date)
 *   1 if a newer version is available (when only checking)
 *  -1 on network/API/permissions error
 */
int update_check_and_apply(bool auto_install);

#endif /* INLAY_UPDATE_H */
