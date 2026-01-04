# Security Audit Report

**Date:** 2026-01-04
**Auditor:** Claude Code
**Scope:** edit v0.2.6 source code
**Status:** All critical and medium issues FIXED

## Executive Summary

The edit codebase demonstrates generally good security practices: no use of dangerous functions like `sprintf`, `strcpy`, `gets`, or `scanf`. One critical vulnerability and several medium issues were identified and fixed in this audit.

---

## Fixed Issues

### 1. Command Injection in Auto-Update (CRITICAL) - FIXED

**Location:** `src/update.c`

**Original Vulnerability:** The version string extracted from GitHub API JSON response was used directly in a `system()` call without validation.

**Fixes Applied:**
1. Added `is_valid_version()` function that validates version strings contain only alphanumeric characters, dots, and hyphens
2. Replaced `system()` call with `fork()`/`exec()` via new `safe_curl_download()` function - completely eliminates shell interpretation
3. Added `curl_is_available()` using fork/exec instead of system()

**Files Modified:** `src/update.c`

### 2. Potential ReDoS in Search (MEDIUM) - FIXED

**Location:** `src/edit.c`, `src/types.h`

**Original Vulnerability:** User-provided regex patterns could cause CPU exhaustion with catastrophic backtracking.

**Fix Applied:** Added `MAX_REGEX_PATTERN_LENGTH` constant (256 bytes) and validation before all `regcomp()` calls to reject overly long patterns.

**Files Modified:** `src/types.h`, `src/edit.c`

### 3. HOME Environment Variable Trust (LOW) - FIXED

**Location:** `src/autosave.c`, `src/theme.c`, `src/dialog.c`

**Original Vulnerability:** `getenv("HOME")` was trusted without validation. A malicious HOME value could cause files to be written to unintended locations.

**Fix Applied:** Added `safe_get_home()` inline function in `src/edit.h` that validates:
- HOME is not NULL
- HOME starts with `/` (absolute path)
- HOME is not too long (leaves room for subdirectories)
- HOME doesn't contain `..` (directory traversal)

All uses of `getenv("HOME")` replaced with `safe_get_home()`.

**Files Modified:** `src/edit.h`, `src/autosave.c`, `src/theme.c`, `src/dialog.c`

---

## Low Findings (Not Fixed - Acceptable Risk)

### 4. Static Buffer for Swap Path (LOW)

**Location:** `src/autosave.c:489`

**Issue:** Using a static buffer means the function is not reentrant. However, this is a single-threaded context.

**Status:** Acceptable risk - no fix needed.

### 5. TOCTOU in File Operations (LOW)

**Location:** `src/editor.c`, `src/edit.c`

**Issue:** Time-of-check to time-of-use race conditions exist between checking if a file exists and opening it.

**Status:** Acceptable risk. Adding O_NOFOLLOW would break legitimate symlink usage in a text editor. The window for exploitation is minimal and requires local access.

---

## Positive Observations

### Good Practices Found:

1. **No dangerous string functions:** No use of `sprintf`, `strcpy`, `strcat`, `gets`, or unbounded `scanf`.

2. **Consistent use of `snprintf`:** All string formatting uses size-bounded `snprintf`.

3. **Null-termination after strncpy:** Consistent pattern of null-terminating after `strncpy`.

4. **Proper mmap handling:** Memory-mapped files use `MAP_PRIVATE` and are properly unmapped on cleanup.

5. **Atomic file writes:** Swap files and saves use write-to-temp-then-rename pattern for atomicity.

6. **Clipboard commands hardcoded:** The `popen()` calls for clipboard use hardcoded command strings, preventing injection.

7. **Input bounds checking:** Mouse and keyboard input parsing has proper bounds on buffer reads.

8. **malloc/realloc failure handling:** Memory allocation failures are consistently checked.

---

## Summary of Changes

| File | Changes |
|------|---------|
| `src/update.c` | Added `is_valid_version()`, `safe_curl_download()`, replaced `system()` with fork/exec |
| `src/types.h` | Added `MAX_REGEX_PATTERN_LENGTH` constant |
| `src/edit.h` | Added `safe_get_home()` inline function |
| `src/edit.c` | Added regex pattern length validation at 3 locations |
| `src/autosave.c` | Replaced `getenv("HOME")` with `safe_get_home()` (2 locations) |
| `src/theme.c` | Replaced `getenv("HOME")` with `safe_get_home()` (2 locations) |
| `src/dialog.c` | Replaced `getenv("HOME")` with `safe_get_home()` (1 location) |

---

## Conclusion

All critical and medium security issues have been addressed. The codebase now includes:
- Input validation for version strings from untrusted network sources
- Shell-free command execution using fork/exec
- Regex pattern length limits to mitigate ReDoS
- Environment variable validation for HOME

The remaining low-priority findings are acceptable risks that don't warrant changes due to their minimal impact and the trade-offs involved.
