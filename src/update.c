/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024-2026 Edward Edmonds
 */

/*
 * update.c - Self-update functionality for edit
 *
 * Checks GitHub releases for new versions and updates the binary in-place.
 * Uses curl for HTTP requests (no library dependencies).
 */

#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>

#include "update.h"
#include "types.h"

/*****************************************************************************
 * External Dependencies
 *****************************************************************************/

/* Editor state */
extern struct editor_state editor;

/* Functions from editor.c */
extern void editor_set_status_message(const char *format, ...);

/* GitHub repository for update checks */
#define GITHUB_REPO "edwardedmonds/edit"

/* Maximum time to wait for curl (seconds) */
#define CURL_TIMEOUT 10

/*****************************************************************************
 * Version Comparison
 *****************************************************************************/

/*
 * Compare two version strings (e.g., "0.2.0" vs "0.3.0").
 * Handles versions with 1-4 numeric components separated by dots.
 */
int update_version_compare(const char *version_a, const char *version_b)
{
	int parts_a[4] = {0, 0, 0, 0};
	int parts_b[4] = {0, 0, 0, 0};

	/* Parse version_a */
	sscanf(version_a, "%d.%d.%d.%d",
	       &parts_a[0], &parts_a[1], &parts_a[2], &parts_a[3]);

	/* Parse version_b */
	sscanf(version_b, "%d.%d.%d.%d",
	       &parts_b[0], &parts_b[1], &parts_b[2], &parts_b[3]);

	/* Compare each component */
	for (int i = 0; i < 4; i++) {
		if (parts_a[i] < parts_b[i])
			return -1;
		if (parts_a[i] > parts_b[i])
			return 1;
	}

	return 0;
}

/*****************************************************************************
 * Update Check
 *****************************************************************************/

/*
 * Check if curl is available on the system.
 * Uses fork/exec instead of system() to avoid shell interpretation.
 */
static bool curl_is_available(void)
{
	pid_t pid = fork();
	if (pid < 0) {
		return false;
	}
	if (pid == 0) {
		/* Child: redirect stdout/stderr to /dev/null */
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		execlp("curl", "curl", "--version", (char *)NULL);
		_exit(127);
	}
	/* Parent: wait for child */
	int status;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/*
 * Download a file using curl via fork/exec (no shell interpretation).
 * Returns 0 on success, -1 on error.
 */
static int safe_curl_download(const char *url, const char *output_path)
{
	pid_t pid = fork();
	if (pid < 0) {
		return -1;
	}
	if (pid == 0) {
		/* Child: redirect stderr to /dev/null */
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		execlp("curl", "curl",
		       "-sL",              /* silent, follow redirects */
		       "--max-time", "60", /* timeout */
		       "-o", output_path,  /* output file */
		       url,                /* URL to download */
		       (char *)NULL);
		_exit(127);
	}
	/* Parent: wait for child */
	int status;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		return 0;
	}
	return -1;
}

/*
 * Validate that a version string contains only safe characters.
 * Allows alphanumeric, dots, and hyphens (e.g., "0.2.6", "1.0.0-beta").
 * This prevents command injection when the version is used in shell commands.
 */
static bool is_valid_version(const char *version)
{
	if (version == NULL || version[0] == '\0')
		return false;
	for (const char *p = version; *p; p++) {
		if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-') {
			return false;
		}
	}
	return true;
}

/*
 * Extract version string from GitHub API JSON response.
 * Looks for "tag_name": "v0.3.0" and extracts "0.3.0".
 * Validates the version contains only safe characters (alphanumeric, dots, hyphens)
 * to prevent command injection when used in shell commands.
 * Returns true on success, false if parsing fails or version is invalid.
 */
static bool parse_tag_name(const char *json, char *version, size_t version_size)
{
	const char *tag_start = strstr(json, "\"tag_name\"");
	if (!tag_start)
		return false;

	/* Find the colon after tag_name */
	const char *colon = strchr(tag_start, ':');
	if (!colon)
		return false;

	/* Find the opening quote of the value */
	const char *quote_start = strchr(colon, '"');
	if (!quote_start)
		return false;
	quote_start++;  /* Skip the quote */

	/* Skip 'v' prefix if present */
	if (*quote_start == 'v' || *quote_start == 'V')
		quote_start++;

	/* Find the closing quote */
	const char *quote_end = strchr(quote_start, '"');
	if (!quote_end)
		return false;

	/* Copy the version string */
	size_t len = quote_end - quote_start;
	if (len >= version_size)
		len = version_size - 1;

	strncpy(version, quote_start, len);
	version[len] = '\0';

	/* Validate version to prevent command injection */
	if (!is_valid_version(version))
		return false;

	return true;
}

/*
 * Query GitHub for the latest release version.
 */
bool update_check_available(char *latest_version, size_t buffer_size)
{
	if (!curl_is_available()) {
		editor_set_status_message("Update check failed: curl not found");
		return false;
	}

	/* Build curl command */
	char command[512];
	snprintf(command, sizeof(command),
	         "curl -s --max-time %d "
	         "-H 'Accept: application/vnd.github.v3+json' "
	         "'https://api.github.com/repos/%s/releases/latest' 2>/dev/null",
	         CURL_TIMEOUT, GITHUB_REPO);

	/* Execute curl and read output */
	FILE *fp = popen(command, "r");
	if (!fp) {
		editor_set_status_message("Update check failed: could not run curl");
		return false;
	}

	/* Read response (up to 64KB should be plenty for release info) */
	char response[65536];
	size_t total_read = 0;
	size_t bytes_read;

	while ((bytes_read = fread(response + total_read, 1,
	                           sizeof(response) - total_read - 1, fp)) > 0) {
		total_read += bytes_read;
		if (total_read >= sizeof(response) - 1)
			break;
	}
	response[total_read] = '\0';

	int status = pclose(fp);
	if (status != 0 || total_read == 0) {
		editor_set_status_message("Update check failed: no response from GitHub");
		return false;
	}

	/* Check for rate limiting or errors */
	if (strstr(response, "\"message\":") && strstr(response, "rate limit")) {
		editor_set_status_message("Update check failed: GitHub rate limit exceeded");
		return false;
	}

	/* Parse the tag_name from JSON */
	char remote_version[64];
	if (!parse_tag_name(response, remote_version, sizeof(remote_version))) {
		editor_set_status_message("Update check failed: could not parse version");
		return false;
	}

	/* Compare versions */
	int cmp = update_version_compare(EDIT_VERSION, remote_version);

	if (cmp >= 0) {
		/* Already on latest or newer */
		editor_set_status_message("You're on the latest version (v%s)", EDIT_VERSION);
		return false;
	}

	/* Newer version available */
	strncpy(latest_version, remote_version, buffer_size - 1);
	latest_version[buffer_size - 1] = '\0';

	return true;
}

/*****************************************************************************
 * Update Installation
 *****************************************************************************/

/*
 * Get the path to the currently running executable.
 */
static bool get_executable_path(char *path, size_t path_size)
{
	ssize_t len = readlink("/proc/self/exe", path, path_size - 1);
	if (len < 0) {
		return false;
	}
	path[len] = '\0';
	return true;
}

/*
 * Check if we have write permission to the binary location.
 */
static bool can_write_to_binary(const char *exe_path)
{
	/* Check if we can write to the directory containing the binary */
	char dir_path[PATH_MAX];
	strncpy(dir_path, exe_path, sizeof(dir_path) - 1);
	dir_path[sizeof(dir_path) - 1] = '\0';

	char *last_slash = strrchr(dir_path, '/');
	if (last_slash) {
		*last_slash = '\0';
	}

	return access(dir_path, W_OK) == 0;
}

/*
 * Download and install the specified version.
 */
bool update_install(const char *version)
{
	char exe_path[PATH_MAX - 8];  /* Leave room for .new/.old suffix */
	char new_path[PATH_MAX];
	char old_path[PATH_MAX];

	/* Get current executable path */
	if (!get_executable_path(exe_path, sizeof(exe_path))) {
		editor_set_status_message("Update failed: could not determine binary path");
		return false;
	}

	/* Check path isn't too long for our suffixes */
	if (strlen(exe_path) > PATH_MAX - 8) {
		editor_set_status_message("Update failed: binary path too long");
		return false;
	}

	/* Check write permission */
	if (!can_write_to_binary(exe_path)) {
		editor_set_status_message("Update failed: no write permission to %s", exe_path);
		return false;
	}

	/* Build paths for new and backup files */
	snprintf(new_path, sizeof(new_path), "%s.new", exe_path);
	snprintf(old_path, sizeof(old_path), "%s.old", exe_path);

	/* Show status */
	editor_set_status_message("Downloading v%s...", version);

	/* Build download URL */
	char url[512];
	snprintf(url, sizeof(url),
	         "https://github.com/%s/releases/download/v%s/edit",
	         GITHUB_REPO, version);

	/* Download new binary using fork/exec (no shell interpretation) */
	if (safe_curl_download(url, new_path) != 0) {
		unlink(new_path);  /* Clean up partial download */
		editor_set_status_message("Update failed: download error");
		return false;
	}

	/* Verify the downloaded file exists and has reasonable size */
	struct stat st;
	if (stat(new_path, &st) != 0 || st.st_size < 10000) {
		unlink(new_path);
		editor_set_status_message("Update failed: invalid download (binary not attached to release?)");
		return false;
	}

	/* Make new binary executable */
	if (chmod(new_path, 0755) != 0) {
		unlink(new_path);
		editor_set_status_message("Update failed: could not set permissions");
		return false;
	}

	/* Remove old backup if it exists */
	unlink(old_path);

	/* Rename current binary to .old (backup) */
	if (rename(exe_path, old_path) != 0) {
		unlink(new_path);
		editor_set_status_message("Update failed: could not create backup");
		return false;
	}

	/* Rename new binary to current */
	if (rename(new_path, exe_path) != 0) {
		/* Try to restore from backup */
		rename(old_path, exe_path);
		editor_set_status_message("Update failed: could not install new binary");
		return false;
	}

	editor_set_status_message("Updated to v%s! Restart edit to use new version.", version);
	return true;
}

/*****************************************************************************
 * Main Entry Point
 *****************************************************************************/

/*
 * Check for updates and show appropriate UI.
 * This is triggered by Alt+U.
 */
void update_check(void)
{
	editor_set_status_message("Checking for updates...");

	/* Force a screen refresh to show the status message */
	/* (The caller should handle this) */

	char latest_version[64];
	if (!update_check_available(latest_version, sizeof(latest_version))) {
		/* Status message already set by update_check_available */
		return;
	}

	/* Update is available - this will be handled by the caller
	 * which should show a confirmation dialog */
	editor.update_available = true;
	strncpy(editor.update_version, latest_version, sizeof(editor.update_version) - 1);
	editor.update_version[sizeof(editor.update_version) - 1] = '\0';
}
