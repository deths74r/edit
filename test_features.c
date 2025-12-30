/*
 * Test file for Phase 15-16 features
 *
 * PHASE 15: Auto-indent & Comment Toggle
 * =======================================
 *
 * Test Auto-indent (Enter key):
 * 1. Place cursor at end of line 25 (after the {)
 * 2. Press Enter - new line should have same indent (4 spaces)
 * 3. Place cursor in middle of line 30 (inside the indent)
 * 4. Press Enter - indent should be limited to cursor position
 * 5. Try with tabs on lines 45-47
 *
 * Test Comment Toggle (Ctrl+/ or Alt+/):
 * 1. Place cursor on line 25, press Ctrl+/ - should add // prefix
 * 2. Press Ctrl+/ again - should remove // prefix
 * 3. Select lines 25-28, press Ctrl+/ - all lines get commented
 * 4. With same selection, press Ctrl+/ - all lines uncommented
 * 5. Select lines with mixed indent (55-58), toggle comment
 *    - Comments should align at minimum indent level
 *
 * PHASE 16: Find & Replace
 * ========================
 *
 * Test Replace Mode (Ctrl+R):
 * 1. Press Ctrl+R - should show "Find: [] | Replace: "
 * 2. Type "foo" - matches should highlight
 * 3. Press Tab - cursor moves to replace field
 * 4. Type "bar"
 * 5. Press Enter - replaces current "foo" with "bar", finds next
 * 6. Press Alt+A - replaces all remaining "foo" with "bar"
 * 7. Press Ctrl+Z - should undo all replacements from Alt+A
 * 8. Press Escape - cancels and restores original position
 */

#include <stdio.h>

int main() {
    int foo = 10;
    int bar = 20;

    if (foo > 0) {
        printf("foo is positive\n");
        foo = foo + 1;
    }

    // This is already a comment
    int baz = foo + bar;

	// Tab-indented line
	int tab_indent = 1;
	if (tab_indent) {
		printf("tabs work too\n");
	}

    return 0;
}

/* Test data for find & replace - lots of "foo" occurrences */
void test_replace() {
    int foo = 1;      // foo here
    int foo2 = 2;     // foo in variable name
    char *s = "foo";  // foo in string

    // Mixed indent for comment toggle test
    if (1) {
            int deep = 1;
        int shallow = 2;
    int none = 3;
    }

    foo = foo + foo;  // multiple foo on one line

    /* Block with repeated words */
    int test = 1;
    int test2 = 2;
    int test3 = 3;
    int testing = 4;
}

/*
 * Edge cases to test:
 *
 * 1. Replace "foo" with "" (empty) - should delete all "foo"
 * 2. Replace "a" with "aa" - replacement contains search term
 * 3. Replace at start of line
 * 4. Replace at end of line
 * 5. Case-insensitive: search "FOO" should find "foo"
 * 6. Undo single replace vs undo replace-all
 */

// Quick test: uncomment next line and try features
// int foo = foo + foo;
