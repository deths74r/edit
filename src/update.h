/*****************************************************************************
 * update.h - Self-update functionality
 *
 * Provides the ability to check for new versions on GitHub and update
 * the editor binary in-place.
 *****************************************************************************/

#ifndef UPDATE_H
#define UPDATE_H

#include <stdbool.h>

/*
 * Check for updates and show appropriate UI.
 * This is the main entry point, triggered by Alt+U.
 *
 * Flow:
 * 1. Shows "Checking for updates..." status
 * 2. Queries GitHub releases API via curl
 * 3. If update available: shows confirmation dialog
 * 4. If user confirms: downloads and installs new binary
 * 5. Shows result status message
 */
void update_check(void);

/*
 * Query GitHub for the latest release version.
 * Returns true if a newer version is available.
 *
 * On success, stores the latest version string (e.g., "0.3.0") in
 * the provided buffer.
 */
bool update_check_available(char *latest_version, size_t buffer_size);

/*
 * Download and install the specified version.
 * Returns true on success.
 *
 * Process:
 * 1. Gets path to current executable via /proc/self/exe
 * 2. Downloads new binary to <exe_path>.new
 * 3. Renames current binary to <exe_path>.old (backup)
 * 4. Renames new binary to <exe_path>
 */
bool update_install(const char *version);

/*
 * Compare two version strings (e.g., "0.2.0" vs "0.3.0").
 * Returns:
 *   < 0 if version_a < version_b
 *   = 0 if version_a == version_b
 *   > 0 if version_a > version_b
 */
int update_version_compare(const char *version_a, const char *version_b);

#endif /* UPDATE_H */
