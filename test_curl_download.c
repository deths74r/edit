/*
 * test_curl_download.c - Test the safe_curl_download function
 */

#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Copy of safe_curl_download from update.c */
static int safe_curl_download(const char *url, const char *output_path)
{
	pid_t pid = fork();
	if (pid < 0) {
		return -1;
	}
	if (pid == 0) {
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		execlp("curl", "curl",
		       "-sL",
		       "--max-time", "10",
		       "-o", output_path,
		       url,
		       (char *)NULL);
		_exit(127);
	}
	int status;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		return 0;
	}
	return -1;
}

int main(void)
{
	printf("=== Testing safe_curl_download ===\n\n");

	const char *test_url = "https://httpbin.org/robots.txt";
	const char *output_file = "/tmp/test_curl_download.txt";

	printf("Downloading %s to %s...\n", test_url, output_file);

	int result = safe_curl_download(test_url, output_file);

	if (result == 0) {
		printf("PASS: Download succeeded\n");

		/* Verify file exists and has content */
		struct stat st;
		if (stat(output_file, &st) == 0 && st.st_size > 0) {
			printf("PASS: Downloaded file exists (%ld bytes)\n", (long)st.st_size);

			/* Show first line of content */
			FILE *f = fopen(output_file, "r");
			if (f) {
				char line[256];
				if (fgets(line, sizeof(line), f)) {
					printf("Content: %s", line);
				}
				fclose(f);
			}

			unlink(output_file);  /* Clean up */
			return 0;
		} else {
			printf("FAIL: Downloaded file missing or empty\n");
			return 1;
		}
	} else {
		printf("FAIL: Download failed (curl may not be working or no network)\n");
		return 1;
	}
}
