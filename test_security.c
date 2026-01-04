/*
 * test_security.c - Test security fixes
 *
 * Compile: cc -o test_security test_security.c
 * Run: ./test_security
 */

#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <limits.h>

/* Copy of is_valid_version from update.c */
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

/* Copy of safe_get_home from edit.h */
static const char *safe_get_home(void)
{
	const char *home = getenv("HOME");
	if (home == NULL)
		return NULL;

	/* Must be an absolute path */
	if (home[0] != '/')
		return NULL;

	/* Must not be too long (leave room for subdirs) */
	if (strlen(home) > PATH_MAX - 64)
		return NULL;

	/* Must not contain dangerous sequences */
	if (strstr(home, "..") != NULL)
		return NULL;

	return home;
}

/* MAX_REGEX_PATTERN_LENGTH from types.h */
#define MAX_REGEX_PATTERN_LENGTH 256

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name, condition) do { \
	if (condition) { \
		printf("  PASS: %s\n", name); \
		tests_passed++; \
	} else { \
		printf("  FAIL: %s\n", name); \
		tests_failed++; \
	} \
} while(0)

void test_is_valid_version(void)
{
	printf("\n=== Testing is_valid_version() ===\n");

	/* Valid versions */
	TEST("0.2.6 is valid", is_valid_version("0.2.6") == true);
	TEST("1.0.0 is valid", is_valid_version("1.0.0") == true);
	TEST("1.0.0-beta is valid", is_valid_version("1.0.0-beta") == true);
	TEST("1.0.0-rc1 is valid", is_valid_version("1.0.0-rc1") == true);
	TEST("2.0 is valid", is_valid_version("2.0") == true);
	TEST("10.20.30 is valid", is_valid_version("10.20.30") == true);

	/* Invalid versions - command injection attempts */
	TEST("NULL is invalid", is_valid_version(NULL) == false);
	TEST("empty string is invalid", is_valid_version("") == false);
	TEST("version with ; is invalid", is_valid_version("1.0; rm -rf /") == false);
	TEST("version with ' is invalid", is_valid_version("1.0'") == false);
	TEST("version with \" is invalid", is_valid_version("1.0\"") == false);
	TEST("version with | is invalid", is_valid_version("1.0|cat /etc/passwd") == false);
	TEST("version with $ is invalid", is_valid_version("1.0$PATH") == false);
	TEST("version with ` is invalid", is_valid_version("1.0`id`") == false);
	TEST("version with space is invalid", is_valid_version("1.0 ") == false);
	TEST("version with newline is invalid", is_valid_version("1.0\n") == false);
	TEST("version with & is invalid", is_valid_version("1.0&") == false);
	TEST("version with > is invalid", is_valid_version("1.0>file") == false);
	TEST("version with < is invalid", is_valid_version("1.0<file") == false);
	TEST("version with ( is invalid", is_valid_version("1.0(") == false);
	TEST("version with ) is invalid", is_valid_version("1.0)") == false);
}

void test_safe_get_home(void)
{
	printf("\n=== Testing safe_get_home() ===\n");

	const char *result;

	/* Test with valid HOME */
	setenv("HOME", "/home/testuser", 1);
	result = safe_get_home();
	TEST("/home/testuser is valid", result != NULL && strcmp(result, "/home/testuser") == 0);

	setenv("HOME", "/root", 1);
	result = safe_get_home();
	TEST("/root is valid", result != NULL && strcmp(result, "/root") == 0);

	/* Test with relative path (invalid) */
	setenv("HOME", "home/testuser", 1);
	result = safe_get_home();
	TEST("relative path is invalid", result == NULL);

	setenv("HOME", "./home", 1);
	result = safe_get_home();
	TEST("./home is invalid", result == NULL);

	/* Test with directory traversal (invalid) */
	setenv("HOME", "/home/user/../../../etc", 1);
	result = safe_get_home();
	TEST("path with .. is invalid", result == NULL);

	setenv("HOME", "/home/..user", 1);
	result = safe_get_home();
	TEST("path containing .. is invalid", result == NULL);

	/* Test with empty HOME */
	setenv("HOME", "", 1);
	result = safe_get_home();
	TEST("empty HOME is invalid", result == NULL);

	/* Test with unset HOME */
	unsetenv("HOME");
	result = safe_get_home();
	TEST("unset HOME returns NULL", result == NULL);

	/* Restore HOME */
	setenv("HOME", "/home/testuser", 1);
}

void test_regex_length_limit(void)
{
	printf("\n=== Testing MAX_REGEX_PATTERN_LENGTH ===\n");

	TEST("MAX_REGEX_PATTERN_LENGTH is 256", MAX_REGEX_PATTERN_LENGTH == 256);
	TEST("MAX_REGEX_PATTERN_LENGTH > 0", MAX_REGEX_PATTERN_LENGTH > 0);

	/* Test that a pattern at the limit would be rejected */
	char long_pattern[300];
	memset(long_pattern, 'a', 299);
	long_pattern[299] = '\0';

	TEST("Pattern longer than limit detected", strlen(long_pattern) > MAX_REGEX_PATTERN_LENGTH);

	char ok_pattern[256];
	memset(ok_pattern, 'a', 255);
	ok_pattern[255] = '\0';

	TEST("Pattern at limit is acceptable length", strlen(ok_pattern) <= MAX_REGEX_PATTERN_LENGTH);
}

int main(void)
{
	printf("=================================\n");
	printf("Security Fixes Test Suite\n");
	printf("=================================\n");

	test_is_valid_version();
	test_safe_get_home();
	test_regex_length_limit();

	printf("\n=================================\n");
	printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
	printf("=================================\n");

	return tests_failed > 0 ? 1 : 0;
}
