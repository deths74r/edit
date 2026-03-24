/*
 * ============================================================================
 *
 *                    edit -- Complete Feature Tutorial
 *
 *                    Open with:  ./edit test_features.c
 *
 *    Work through each lesson in order. Each lesson describes a feature,
 *    tells you exactly what to do, and explains what you should see.
 *    Lines marked with >>> are your practice targets.
 *
 *    Don't worry about saving -- you can always revert with:
 *        git checkout test_features.c
 *
 *    There are 40 lessons covering every feature from v0.2.0 through v1.0.0.
 *    Take your time. The cursor position, line number, and percentage are
 *    always visible in the status bar at the bottom of the screen.
 *
 * ============================================================================
 */


/* ===========================================================================
 * LESSON 1: Basic Navigation
 *
 * Let's start with the fundamentals of moving around.
 *
 * 1. Use the Arrow keys to move up, down, left, and right.
 *    EXPECT: The cursor moves one character or one line at a time.
 *
 * 2. Press Home (or Ctrl+A) to jump to the start of the >>> line below.
 *    EXPECT: Cursor snaps to column 1.
 *
 * 3. Press End (or Ctrl+E) to jump to the end of the >>> line.
 *    EXPECT: Cursor moves past the last character on the line.
 *
 * 4. Press Page Down.
 *    EXPECT: The viewport scrolls down by one screenful.
 *
 * 5. Press Page Up.
 *    EXPECT: The viewport scrolls back up by one screenful.
 *
 * 6. Press Alt+G (or Ctrl+G). Type "1" and press Enter.
 *    EXPECT: The cursor jumps to the very first line of this file.
 *
 * 7. Press Alt+G again. Type "100" and press Enter.
 *    EXPECT: The cursor jumps to line 100.
 *
 * Good work! You can also use Alt+H/J/K/L for vim-style movement.
 * ===========================================================================
 */

>>> Navigate to the start and end of this line using Home and End.
>>> Then try Alt+G to jump to a specific line number.


/* ===========================================================================
 * LESSON 2: Mouse Navigation
 *
 * Your mouse works too! The editor supports SGR mouse reporting.
 *
 * 1. Click anywhere on the >>> line below.
 *    EXPECT: The cursor jumps to exactly where you clicked.
 *
 * 2. Click on different lines and columns.
 *    EXPECT: The cursor follows your clicks precisely.
 *
 * 3. Use the scroll wheel to scroll up and down.
 *    EXPECT: The viewport scrolls. Try scrolling fast --
 *    the scroll speed accelerates when you scroll quickly
 *    and decelerates when you slow down.
 *
 * Scroll speed ramps from 1 line up to 10 lines per tick based on
 * how fast you're scrolling. Events within 50ms go faster; events
 * more than 200ms apart reset to speed 1.
 * ===========================================================================
 */

>>> Click here with your mouse to position the cursor.
>>> Try clicking at different positions on this line too.
>>> Now scroll up and down with the scroll wheel. Try fast and slow.


/* ===========================================================================
 * LESSON 3: Basic Editing
 *
 * Time to type, delete, and split lines.
 *
 * 1. Place your cursor at the end of the first >>> line below.
 *    Type: " hello world"
 *    EXPECT: The text appears at the cursor position.
 *
 * 2. Press Backspace a few times.
 *    EXPECT: Characters to the left of the cursor are deleted.
 *
 * 3. Move the cursor to the middle of the text and press Delete.
 *    EXPECT: The character to the right of the cursor is deleted.
 *
 * 4. Place cursor in the middle of the second >>> line and press Enter.
 *    EXPECT: The line splits in two at the cursor position.
 *
 * 5. Press Backspace at column 0 of the new second half.
 *    EXPECT: The two lines rejoin into one.
 * ===========================================================================
 */

>>> Type at the end of this line:
>>> Split this line in the middle by pressing Enter


/* ===========================================================================
 * LESSON 4: Auto-Indent
 *
 * When you press Enter, the new line inherits the leading whitespace
 * from the current line. This keeps your code properly indented.
 *
 * 1. Place your cursor at the end of the printf line below (it has
 *    two levels of tab indentation).
 * 2. Press Enter.
 *    EXPECT: The new line has two tabs of indentation already present.
 *    Your cursor is positioned after the tabs, ready to type.
 *
 * 3. Press Ctrl+U to undo and try it again on the single-indent line.
 *    EXPECT: New line has one tab of indentation.
 *
 * 4. Now go to column 0 of any line and press Enter.
 *    EXPECT: A blank line with NO indentation (the cursor was at the
 *    beginning, so there was no leading whitespace to copy).
 * ===========================================================================
 */

void auto_indent_demo(void)
{
	if (1) {
		printf("press Enter at the end of this line\n");
	}
	printf("one level of indent here\n");
}


/* ===========================================================================
 * LESSON 5: Undo / Redo
 *
 * The undo system uses 256 groups with time-based coalescing.
 *
 * PART A: Basic undo/redo
 * 1. Place cursor at the end of the first >>> line below.
 * 2. Type: THIS IS A TEST
 * 3. Press Ctrl+U.
 *    EXPECT: Your typed text disappears (undone as one group).
 * 4. Press Ctrl+R.
 *    EXPECT: The text reappears (redone).
 * 5. Press Ctrl+U to undo, then type something NEW.
 * 6. Press Ctrl+R.
 *    EXPECT: "Nothing to redo" -- new edits discard redo history.
 *
 * PART B: Time-based grouping
 * 7. Place cursor at the end of the second >>> line.
 * 8. Type "hello" quickly (no pauses).
 * 9. Wait about 1 second.
 * 10. Type "world" quickly.
 * 11. Press Ctrl+U once.
 *     EXPECT: Only "world" disappears (separate undo group).
 * 12. Press Ctrl+U again.
 *     EXPECT: "hello" disappears (it was its own group).
 *
 * PART C: Structural boundaries
 * 13. Place cursor at the | in the third >>> line.
 * 14. Press Enter to split the line.
 * 15. Press Ctrl+U.
 *     EXPECT: Lines rejoin -- Enter is always its own undo group.
 * ===========================================================================
 */

>>> Type here then undo with Ctrl+U:
>>> Type with pauses to test grouping:
>>> first_half|second_half


/* ===========================================================================
 * LESSON 6: Scroll Margin
 *
 * The editor keeps 5 lines of context visible above and below your cursor.
 * You should never feel like you're typing at the very edge of the screen.
 *
 * 1. Hold the Down arrow and scroll through this file.
 *    EXPECT: The viewport begins scrolling BEFORE the cursor reaches
 *    the bottom row. You always see about 5 lines below the cursor.
 *
 * 2. Hold the Up arrow going back.
 *    EXPECT: Same behavior -- the viewport scrolls before the cursor
 *    reaches the top row.
 *
 * Here are some padding lines so you can see the effect clearly:
 * ===========================================================================
 */

/* padding */ /* padding */ /* padding */ /* padding */ /* padding */
/* padding */ /* padding */ /* padding */ /* padding */ /* padding */
/* padding */ /* padding */ /* padding */ /* padding */ /* padding */
/* padding */ /* padding */ /* padding */ /* padding */ /* padding */
/* padding */ /* padding */ /* padding */ /* padding */ /* padding */
/* padding */ /* padding */ /* padding */ /* padding */ /* padding */
/* padding */ /* padding */ /* padding */ /* padding */ /* padding */
/* padding */ /* padding */ /* padding */ /* padding */ /* padding */
/* padding */ /* padding */ /* padding */ /* padding */ /* padding */
/* padding */ /* padding */ /* padding */ /* padding */ /* padding */
/* padding */ /* padding */ /* padding */ /* padding */ /* padding */
/* padding */ /* padding */ /* padding */ /* padding */ /* padding */


/* ===========================================================================
 * LESSON 7: Virtual Column Preservation
 *
 * When you move vertically through lines of different lengths, the editor
 * remembers your desired column. When you reach a longer line again, the
 * cursor returns to that column.
 *
 * 1. Press End on the first >>> line below (it's long).
 * 2. Press Down through the two short lines.
 *    EXPECT: Cursor snaps to the end of each short line.
 * 3. Press Down one more time to reach the second long line.
 *    EXPECT: Cursor jumps back to the original far-right column.
 * 4. Press Left or Right once (resets the preferred column).
 * 5. Move up through the short lines again.
 *    EXPECT: The new "preferred column" is where you last pressed
 *    Left/Right, not the original far-right position.
 * ===========================================================================
 */

>>> int this_is_a_very_long_variable_name_that_extends_far_right_on_the_line = 100;
>>> short = 1;
>>> x = 2;
>>> int another_very_long_variable_name_matching_the_first_line_exactly = 200;


/* ===========================================================================
 * LESSON 8: Word Movement
 *
 * Ctrl+Right jumps forward by word. Ctrl+Left jumps backward.
 * The editor uses a three-class model:
 *   - Whitespace (spaces, tabs)
 *   - Punctuation (operators, brackets, etc.)
 *   - Word characters (letters, digits, underscores)
 *
 * 1. Press Home on the first >>> line below to go to column 1.
 * 2. Press Ctrl+Right repeatedly.
 *    EXPECT: Cursor lands on: int, foo_bar, =, 42, +, count, ;
 *    Notice foo_bar is ONE word (underscores are word characters).
 *    Notice = and + are separate punctuation words.
 *
 * 3. Press Ctrl+Left repeatedly from the end.
 *    EXPECT: Reverse order of the above.
 *
 * 4. At the end of the first >>> line, press Ctrl+Right.
 *    EXPECT: Cursor wraps to the start of the next line.
 *
 * 5. At the start of the second >>> line, press Ctrl+Left.
 *    EXPECT: Cursor wraps to the end of the previous line.
 * ===========================================================================
 */

>>> int foo_bar = 42 + count;
>>> char *next_line = "jumped here";
>>> result->value += offset;


/* ===========================================================================
 * LESSON 9: Keyboard Selection
 *
 * Hold Shift with arrow keys to select text. Selected text renders
 * with inverted colors (reverse video).
 *
 * 1. Place cursor at the 'H' in "Hello" on the first >>> line below.
 * 2. Press Shift+Right five times.
 *    EXPECT: "Hello" is highlighted with inverted colors.
 *
 * 3. Press Shift+Right a few more times.
 *    EXPECT: Selection extends character by character.
 *
 * 4. Press Shift+Left twice.
 *    EXPECT: Selection shrinks from the right.
 *
 * 5. Press plain Right arrow (no Shift).
 *    EXPECT: Selection clears, cursor jumps to the end of selection.
 *
 * 6. Try Shift+Down to select across multiple lines.
 *    EXPECT: Selection extends line by line.
 *
 * 7. Try Shift+Home to select from cursor to start of line.
 *    Try Shift+End to select from cursor to end of line.
 *
 * 8. Try Alt+A to select from cursor to line start.
 *    Try Alt+E to select from cursor to line end.
 *
 * 9. Press ESC to clear any selection.
 * ===========================================================================
 */

>>> Hello World, this is a selection test line with some extra text.
>>> And this is the second line for multi-line selection practice.
>>> Third line here for good measure. Keep going!
>>> Fourth line to give you plenty of room to experiment.


/* ===========================================================================
 * LESSON 10: Word Selection
 *
 * Shift+Ctrl+Left and Shift+Ctrl+Right select entire words at a time.
 *
 * 1. Place cursor at the start of "quick" on the >>> line below.
 * 2. Press Shift+Ctrl+Right.
 *    EXPECT: "quick" is selected (one whole word).
 *
 * 3. Press Shift+Ctrl+Right again.
 *    EXPECT: Selection extends to include " brown" (whitespace + word).
 *
 * 4. Press Shift+Ctrl+Left.
 *    EXPECT: Selection shrinks by one word.
 * ===========================================================================
 */

>>> the quick brown fox jumps over the lazy dog
>>> some_variable += another_variable * coefficient;


/* ===========================================================================
 * LESSON 11: Mouse Selection
 *
 * Click and drag to select text with your mouse.
 *
 * 1. Click at the 'M' in "Mouse" on the >>> line below. Hold the button.
 * 2. Drag to the right across "selection".
 *    EXPECT: Text highlights between the click point and the drag point.
 *
 * 3. Release the button.
 *    EXPECT: Selection stays highlighted.
 *
 * 4. Click somewhere else without dragging.
 *    EXPECT: Selection clears, cursor moves to click point.
 *
 * 5. Try dragging across multiple lines.
 *    EXPECT: Multi-line selection works just like keyboard selection.
 * ===========================================================================
 */

>>> Mouse selection test: click and drag across this text.
>>> You can also drag across multiple lines to select a block
>>> of text that spans several rows in the file.


/* ===========================================================================
 * LESSON 12: Copy / Paste
 *
 * Alt+C copies, Alt+X cuts, Alt+V pastes.
 * The editor also pushes to system clipboard via OSC 52.
 *
 * 1. Select "COPY_THIS" on the first >>> line (Shift+Arrow or mouse).
 * 2. Press Alt+C.
 *    EXPECT: Status bar shows "Copied N bytes".
 *
 * 3. Move cursor to the blank >>> line.
 * 4. Press Alt+V.
 *    EXPECT: "COPY_THIS" appears at cursor position.
 *
 * 5. Select "CUT_THIS" on the third >>> line.
 * 6. Press Alt+X.
 *    EXPECT: "CUT_THIS" disappears (cut to clipboard).
 *
 * 7. Move to the second blank line and press Alt+V.
 *    EXPECT: "CUT_THIS" appears.
 *
 * 8. Press Alt+V again without copying anything new.
 *    EXPECT: Pastes the same clipboard content again.
 * ===========================================================================
 */

>>> Please COPY_THIS to the line below.
>>>
>>> Please CUT_THIS to another line.
>>>


/* ===========================================================================
 * LESSON 13: Selection + Typing (Replace)
 *
 * When text is selected, typing replaces the selection. Backspace
 * and Delete also remove the entire selection.
 *
 * 1. Select "REPLACE_ME" on the first >>> line below.
 * 2. Type "done".
 *    EXPECT: "REPLACE_ME" vanishes and "done" takes its place.
 *
 * 3. Press Ctrl+U to undo.
 *    EXPECT: "REPLACE_ME" is fully restored.
 *
 * 4. Select "DELETE_ME" on the second >>> line.
 * 5. Press Backspace (or Delete).
 *    EXPECT: "DELETE_ME" is removed entirely.
 * ===========================================================================
 */

>>> int result = REPLACE_ME + 100;
>>> char *name = "DELETE_ME";


/* ===========================================================================
 * LESSON 14: Cut Line / Duplicate Line
 *
 * Alt+Shift+K cuts an entire line. Alt+D duplicates a line.
 *
 * 1. Place cursor anywhere on the "CUT THIS LINE" >>> line below.
 * 2. Press Alt+Shift+K.
 *    EXPECT: The entire line vanishes. It's now in your clipboard.
 *
 * 3. Move to a blank area and press Alt+V.
 *    EXPECT: The cut line is pasted.
 *
 * 4. Place cursor on the "DUPLICATE ME" >>> line.
 * 5. Press Alt+D.
 *    EXPECT: An identical copy appears immediately below,
 *    and your cursor moves to the new copy.
 *
 * 6. Press Ctrl+U to undo the duplicate.
 * ===========================================================================
 */

>>> CUT THIS LINE -- it should vanish with Alt+Shift+K

>>> DUPLICATE ME -- press Alt+D to clone this line


/* ===========================================================================
 * LESSON 15: Incremental Search
 *
 * Alt+F (or Ctrl+F) opens the search prompt. Results update live
 * as you type. Arrow keys navigate between matches.
 *
 * 1. Press Alt+F.
 *    EXPECT: A "Search:" prompt appears in the message bar.
 *
 * 2. Type "TODO" slowly, one letter at a time.
 *    EXPECT: The cursor jumps to the first match as you type.
 *    The matched text is highlighted with the match color.
 *
 * 3. Press Right arrow (or just keep typing characters to refine).
 *    EXPECT: Jumps to the next match.
 *
 * 4. Press Left arrow.
 *    EXPECT: Jumps back to the previous match.
 *
 * 5. Press Enter to accept the search (cursor stays at match).
 *
 * 6. Press ESC during a different search.
 *    EXPECT: Cursor returns to where it was before the search.
 *
 * Here are some markers to search for:
 * ===========================================================================
 */

/* TODO: implement feature alpha */
int alpha = 0;
/* TODO: refactor this section */
int beta = 1;
/* FIXME: handle edge case */
int gamma = 2;
/* TODO: add unit tests for delta */
int delta = 3;
/* FIXME: memory leak in epsilon */
int epsilon = 4;
/* TODO: optimize performance here */
int zeta = 5;


/* ===========================================================================
 * LESSON 16: Case-Insensitive Search
 *
 * During a search, Alt+C toggles case sensitivity.
 *
 * 1. Press Alt+F to start a search.
 * 2. Type "todo" (lowercase).
 *    EXPECT: No matches -- the markers above use uppercase "TODO".
 *
 * 3. Press Alt+C.
 *    EXPECT: Status bar shows "[case off]". The search re-runs.
 *    Now it finds "TODO" even though you typed "todo".
 *
 * 4. Press Alt+C again.
 *    EXPECT: Case sensitivity returns to normal ("[case on]").
 *
 * 5. Press ESC to cancel.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 17: Regex Search
 *
 * During a search, Alt+X toggles regex mode (POSIX ERE).
 *
 * 1. Press Alt+F to start a search.
 * 2. Type "TODO|FIXME" (literal text).
 *    EXPECT: No match (it's looking for the literal string "TODO|FIXME").
 *
 * 3. Press Alt+X to enable regex mode.
 *    EXPECT: Status bar shows "[regex on]". The pattern is now treated
 *    as a regular expression. It matches both "TODO" and "FIXME".
 *
 * 4. Try the regex pattern: "[a-z]_[a-z]"
 *    EXPECT: Matches two-character sequences around underscores in
 *    variable names like foo_bar, next_line, etc.
 *
 * 5. Press Alt+X to disable regex mode and ESC to cancel.
 *
 * Note: This uses POSIX Extended Regular Expressions (ERE), so you
 * get | for alternation, + for one-or-more, () for grouping, etc.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 18: All-Match Highlighting and Match Count
 *
 * During incremental search, ALL matches in the visible area are
 * highlighted, not just the current one. The status bar shows a count.
 *
 * 1. Press Alt+F and type "int".
 *    EXPECT: Every "int" on screen is highlighted in the match color.
 *    The status bar shows "Match N of M" (e.g., "Match 1 of 15").
 *
 * 2. Press Right arrow to navigate to the next match.
 *    EXPECT: The match counter updates: "Match 2 of 15", etc.
 *
 * 3. Keep pressing Right until the search wraps around.
 *    EXPECT: Status bar shows "[Wrapped] Match 1 of 15".
 *
 * 4. Press ESC to cancel.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 19: Search History
 *
 * The editor remembers your last 50 search queries. Use Up/Down
 * arrows during a search to browse through them.
 *
 * 1. Press Alt+F, type "alpha", and press Enter.
 *    (This performs a search and records "alpha" in history.)
 *
 * 2. Press Alt+F, type "beta", and press Enter.
 *    (This records "beta" in history.)
 *
 * 3. Press Alt+F to start a new search. Don't type anything yet.
 * 4. Press Up arrow.
 *    EXPECT: The search field fills with "beta" (most recent).
 *
 * 5. Press Up arrow again.
 *    EXPECT: The search field fills with "alpha" (older).
 *
 * 6. Press Down arrow.
 *    EXPECT: Back to "beta".
 *
 * 7. Press Down arrow again.
 *    EXPECT: Back to whatever you typed before browsing (empty).
 *
 * 8. Press ESC to cancel.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 20: Find and Replace
 *
 * Alt+R opens find-and-replace. It prompts for a search string,
 * then a replacement, then walks through each match asking for
 * confirmation.
 *
 * 1. Press Alt+R.
 *    EXPECT: Prompt says "Replace:".
 *
 * 2. Type "PLACEHOLDER" and press Enter.
 *    EXPECT: Prompt says "Replace with:".
 *
 * 3. Type "REPLACED" and press Enter.
 *    EXPECT: The cursor jumps to the first "PLACEHOLDER" match.
 *    Prompt says "Replace? (y)es (n)o (a)ll (ESC)cancel".
 *
 * 4. Press 'y' to replace the first match.
 *    EXPECT: "PLACEHOLDER" becomes "REPLACED", cursor moves to next.
 *
 * 5. Press 'n' to skip the second match.
 *    EXPECT: That match stays, cursor moves to next.
 *
 * 6. Press 'a' to replace ALL remaining matches at once.
 *    EXPECT: Status bar shows total replacements made.
 *
 * (Or press ESC at any point to cancel and stop replacing.)
 * ===========================================================================
 */

>>> The PLACEHOLDER value goes here.
>>> Another PLACEHOLDER to replace.
>>> A third PLACEHOLDER for the "all" option.
>>> The last PLACEHOLDER in this lesson.


/* ===========================================================================
 * LESSON 21: Syntax Highlighting
 *
 * The editor highlights 9 languages based on file extension:
 *   C/C++ (.c .h .cpp)    Python (.py)       JavaScript (.js .jsx .mjs)
 *   Go (.go)               Rust (.rs)          Bash (.sh .bash)
 *   JSON (.json)           YAML (.yml .yaml)   Markdown (.md)
 *
 * Since you opened this file as .c, you're seeing C syntax highlighting
 * right now! Look around and notice:
 *
 * - Keywords like "if", "for", "return", "switch" in one color
 * - Type keywords like "int", "char", "void" in another color
 * - String literals "like this" in a third color
 * - Numbers like 42 and 3.14 in yet another color
 * - Comments (like this one) in a muted color
 * - Multi-line comments too
 *
 * To test other languages, try:
 *   ./edit example.py      (Python highlighting)
 *   ./edit example.js      (JavaScript highlighting)
 *   ./edit example.go      (Go highlighting)
 *   ./edit example.rs      (Rust highlighting)
 *
 * The filetype is shown in the status bar (bottom left: "c").
 * ===========================================================================
 */

int syntax_demo(void)
{
	/* Multi-line comments
	 * are highlighted across
	 * multiple lines correctly */
	int number = 42;
	float pi = 3.14159f;
	char *message = "Hello, syntax highlighting!";
	char escape_demo = '\n';

	for (int i = 0; i < number; i++) {
		if (i % 2 == 0) {
			printf("even: %d\n", i);
		} else {
			continue;
		}
	}

	switch (number) {
	case 0:
		return -1;
	case 42:
		break;
	default:
		return 0;
	}

	// Single-line comment
	unsigned long big_number = 0xDEADBEEF;
	double scientific = 1.23e-4;

	typedef struct {
		int x;
		int y;
	} point;

	enum color { RED, GREEN, BLUE };

	while (number > 0) {
		number--;
	}

	static const char *names[] = {"alpha", "beta", "gamma"};

	return number;
}


/* ===========================================================================
 * LESSON 22: Bracket Matching
 *
 * Press Alt+] when the cursor is on a bracket to jump to its match.
 * The matching bracket is highlighted even without jumping.
 *
 * 1. Place cursor on the opening '{' of the function below.
 *    EXPECT: The matching closing '}' is highlighted in the match color.
 *
 * 2. Press Alt+].
 *    EXPECT: Cursor jumps to the matching closing '}'.
 *
 * 3. Press Alt+] again.
 *    EXPECT: Cursor jumps back to the opening '{'.
 *
 * 4. Try with parentheses '(' and ')'.
 * 5. Try with square brackets '[' and ']'.
 *
 * 6. Place cursor on a character that is NOT a bracket.
 * 7. Press Alt+].
 *    EXPECT: Status bar says "No matching bracket".
 *
 * Note: Brackets inside strings and comments are ignored by the
 * matching algorithm (they don't count as real brackets).
 * ===========================================================================
 */

int bracket_demo(int x, int y)
{
	if (x > 0) {
		int array[10] = {0};
		for (int i = 0; i < 10; i++) {
			array[i] = (x + y) * (i + 1);
			if (array[i] > 100) {
				printf("big: %d\n", array[i]);
			}
		}
		return array[0];
	}
	/* Brackets in strings don't count: "({[]})" */
	return (x < 0) ? (-x) : (x);
}


/* ===========================================================================
 * LESSON 23: Bracket Pair Colorization
 *
 * Nested brackets are colored by depth using cycling colors. Each
 * nesting level gets a different color from the theme, cycling through
 * 4 colors (keyword1, keyword2, string, number).
 *
 * Look at the deeply nested code below. You should see:
 * - Outermost brackets in one color
 * - Second level in another color
 * - Third level in a third color
 * - Fourth level in a fourth color
 * - Fifth level cycles back to the first color
 *
 * Brackets inside strings and comments keep their string/comment color
 * instead of getting bracket colorization.
 * ===========================================================================
 */

int deeply_nested(int a, int b, int c)
{
	if (a > 0) {
		if (b > 0) {
			if (c > 0) {
				return ((a + (b * (c + 1))) * ((a - b) + (c * 2)));
			} else {
				return (a * (b + (c - (a % (b + 1)))));
			}
		}
	}
	int matrix[4][4] = {{1,2,3,4},{5,6,7,8},{9,10,11,12},{13,14,15,16}};
	return matrix[a % 4][b % 4];
}

/* More bracket depth for practice: */
int combo(int n) { return (((n + 1) * (n + 2)) / ((n + 3) * (n + 4))); }


/* ===========================================================================
 * LESSON 24: Comment Toggle
 *
 * Alt+/ toggles line comments. Works on single lines and selections.
 * Uses the filetype's comment style (// for C, # for Python, etc.).
 *
 * 1. Place cursor on the first >>> line below. Press Alt+/.
 *    EXPECT: "// " is prepended to the line (commented out).
 *
 * 2. Press Alt+/ again on the same line.
 *    EXPECT: The "// " prefix is removed (uncommented).
 *
 * 3. Select multiple lines (the three >>> lines below) using
 *    Shift+Down, then press Alt+/.
 *    EXPECT: All three lines get "// " prepended.
 *
 * 4. With the same lines still selected, press Alt+/ again.
 *    EXPECT: All three lines have "// " removed.
 *
 * The toggle is smart: if ALL selected lines are commented, it
 * uncomments. If any line is NOT commented, it comments all of them.
 * ===========================================================================
 */

>>> int comment_me = 1;
>>> int comment_me_too = 2;
>>> int and_me = 3;


/* ===========================================================================
 * LESSON 25: Indent / Dedent Block
 *
 * With text selected, Tab indents and Shift+Tab dedents.
 * A tab character is inserted/removed at the start of each line.
 *
 * 1. Select all four >>> lines below (Shift+Down or mouse drag).
 * 2. Press Tab.
 *    EXPECT: Each line gains one tab of indentation.
 *
 * 3. Press Tab again.
 *    EXPECT: Each line gains another tab (now two levels deep).
 *
 * 4. Press Shift+Tab.
 *    EXPECT: One level of indentation removed from each line.
 *
 * 5. Press Shift+Tab again.
 *    EXPECT: Back to original indentation.
 *
 * Note: Without a selection, Tab inserts a normal tab character.
 * ===========================================================================
 */

>>> int indent_me = 1;
>>> int indent_me_too = 2;
>>> int keep_going = 3;
>>> int last_one = 4;


/* ===========================================================================
 * LESSON 26: Git Gutter Markers
 *
 * When line numbers are visible, the gutter shows change markers:
 *   + (in keyword2 color) = added line
 *   ~ (in number color)   = modified line
 *
 * If line numbers are off, press Alt+N first to turn them on.
 *
 * 1. Look at the gutter (left column, next to line numbers).
 *    EXPECT: No markers on unmodified lines.
 *
 * 2. Type something at the end of the >>> line below.
 *    EXPECT: A "~" appears in the gutter for that line.
 *
 * 3. Press Enter to create a new line and type something.
 *    EXPECT: A "+" appears in the gutter for the new line.
 *
 * 4. If you save the file (Ctrl+S), all markers reset to none.
 *    (But don't save if you want to keep testing other lessons!)
 * ===========================================================================
 */

>>> Modify this line to see the ~ gutter marker.


/* ===========================================================================
 * LESSON 27: Trailing Whitespace Visualization
 *
 * The editor highlights trailing whitespace with a subtle background
 * tint (using the line_number color as background). This makes it
 * easy to spot and clean up trailing spaces and tabs.
 *
 * Look at the lines below -- some have trailing whitespace that
 * should be visible with a different background color:
 * ===========================================================================
 */

>>> This line has trailing spaces.      
>>> This line has trailing tabs.		
>>> This line has NO trailing whitespace.
>>> Mix of content then spaces.   
>>> Clean line, no trailing.


/* ===========================================================================
 * LESSON 28: Horizontal Scroll Indicators
 *
 * When a line extends beyond the right edge of your terminal, a ">"
 * indicator appears at the rightmost column. When you've scrolled
 * right past the start of a line, a "<" indicator appears at the
 * leftmost column.
 *
 * 1. Place cursor on the very long >>> line below.
 * 2. Press End to go to the end of the line.
 *    EXPECT: The viewport scrolls right. A "<" appears on the left
 *    edge of that line to show content is hidden to the left.
 *
 * 3. Press Home to go back to the beginning.
 *    EXPECT: A ">" appears on the right edge to show content
 *    continues beyond the screen.
 *
 * 4. Try scrolling right with Right arrow to watch the indicators
 *    appear and disappear.
 *
 * Note: Indicators only appear when word wrap is OFF (the default).
 * ===========================================================================
 */

>>> This is an extremely long line that is designed to extend far beyond the right edge of most terminal windows because it contains a very lengthy sequence of words and characters that just keeps going and going and going, well past the 80-column mark, past the 120-column mark, and even past the 160-column mark, continuing with more and more text until it finally reaches this point where we can stop and say: you have scrolled far enough to see both the < and > indicators. Congratulations, intrepid horizontal scroller!


/* ===========================================================================
 * LESSON 29: Status Bar
 *
 * The status bar at the bottom of the screen shows useful information.
 * Take a look right now and identify:
 *
 * LEFT SIDE:
 *   - Filename (or "[No Name]" for new files)
 *   - [RO] if the file is read-only
 *   - [+] if the file has unsaved modifications (dirty flag)
 *   - Filetype (e.g., "c", "python", "text")
 *
 * RIGHT SIDE:
 *   - Line:Column (e.g., "42:15")
 *   - Position indicator (Top / Bot / percentage like "25%")
 *
 * 1. Make an edit to see [+] appear.
 * 2. Navigate to the top of the file.
 *    EXPECT: Position shows "Top".
 * 3. Navigate to the bottom.
 *    EXPECT: Position shows "Bot".
 * 4. Navigate to the middle.
 *    EXPECT: Position shows a percentage like "50%".
 *
 * Long filenames are truncated with "..." to fit the available space.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 30: Color Themes
 *
 * Press Alt+T to cycle through all 7 themes. The theme name appears
 * briefly in the status message bar.
 *
 * Available themes:
 *   1. Cyberpunk      -- Dark neon (magenta, cyan, green)
 *   2. Nightwatch     -- Monochrome dark (shades of gray)
 *   3. Daywatch       -- Monochrome light (dark text on white)
 *   4. Tokyo Night    -- Deep indigo with soft purple and blue
 *   5. Akira          -- Neo-Tokyo (red and cyan on dark)
 *   6. Tokyo Cyberpunk -- Indigo base with neon accents
 *   7. Clarity        -- Colorblind-accessible (blue/orange/yellow)
 *
 * 1. Press Alt+T.
 *    EXPECT: Colors change. Status message shows the theme name.
 *
 * 2. Press Alt+T repeatedly to cycle through all 7 themes.
 *    EXPECT: Each theme has a distinct look. Notice how syntax
 *    highlighting colors, background, and status bar all change.
 *
 * 3. After 7 presses, you're back to the original theme.
 *
 * The selected theme is automatically saved to your config file
 * (~/.config/edit/config) so it persists across sessions.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 31: Help Screen
 *
 * F11 (or Alt+?) opens a built-in help reference. The help screen
 * temporarily replaces your file -- no state is lost.
 *
 * 1. Press F11 (or Alt+?).
 *    EXPECT: The file disappears. A help reference loads in its place.
 *    Status bar says "HELP -- Press ESC or Alt+Q to return".
 *
 * 2. Scroll through the help with arrow keys, Page Down, mouse wheel.
 *    EXPECT: All navigation works normally.
 *
 * 3. Try typing a character.
 *    EXPECT: "Help is read-only" message. No text is inserted.
 *
 * 4. Try Alt+F to search within help.
 *    EXPECT: Search works normally (read-only browsing).
 *
 * 5. Press ESC (or Alt+Q).
 *    EXPECT: This file reappears with your cursor exactly where
 *    you left it. Any unsaved edits are still present. The editor
 *    state is fully preserved through the snapshot/restore system.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 32: Suspend / Resume
 *
 * Ctrl+Z suspends the editor and drops you back to your shell.
 * Use "fg" to resume. This is the standard UNIX job control.
 *
 * 1. Press Ctrl+Z.
 *    EXPECT: The editor disappears. Your shell prompt appears.
 *    The terminal is restored to normal mode.
 *
 * 2. Type: fg
 *    EXPECT: The editor reappears with everything intact.
 *    Raw mode and mouse reporting are re-enabled.
 *
 * This is useful for running shell commands (git, make, etc.)
 * without quitting the editor.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 33: Soft Word Wrap
 *
 * Alt+W toggles soft word wrap. When enabled, long lines wrap at
 * the screen edge instead of scrolling horizontally.
 *
 * 1. Press Alt+W.
 *    EXPECT: Status message says "Word wrap: on".
 *    The very long line from Lesson 28 (and the one below) now
 *    wraps to multiple screen rows. No horizontal scrolling needed.
 *    Continuation rows have a blank gutter (no line number).
 *
 * 2. Notice the horizontal scroll indicators (< and >) are gone.
 *    Word wrap and horizontal scroll are mutually exclusive.
 *
 * 3. Press Alt+W again.
 *    EXPECT: "Word wrap: off". Lines go back to single-row display
 *    with horizontal scrolling.
 * ===========================================================================
 */

>>> This is another very long line that is specifically here for testing word wrap mode. When Alt+W is active, this line should wrap to multiple visual rows on your screen. Each continuation row gets a blank gutter area where the line number would normally appear. The cursor should still navigate correctly through the wrapped text. Word wrap makes it much easier to read long lines without horizontal scrolling.


/* ===========================================================================
 * LESSON 34: Column Ruler
 *
 * A vertical ruler line can be displayed at a specific column position.
 * This is useful for enforcing line length limits (80 or 120 columns).
 *
 * The ruler is set in the config file. If no ruler is configured, the
 * column is 0 (disabled).
 *
 * To enable a ruler at column 80, add this to your config file:
 *   ruler = 80
 *
 * The ruler appears as a subtle background tint on the specified column,
 * similar to how trailing whitespace is visualized. It uses the
 * line_number theme color as the background tint.
 *
 * See Lesson 35 for how to set this up.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 35: Config File
 *
 * The editor reads settings from ~/.config/edit/config on startup.
 * The config file uses a simple key = value format with # comments.
 *
 * Supported settings:
 *
 *   # Tab display width (2-16)
 *   tabstop = 4
 *
 *   # Color theme (must match a theme name exactly)
 *   theme = Tokyo Night
 *
 *   # Show/hide line numbers
 *   line_numbers = true
 *
 *   # Column ruler position (0 = disabled, max 500)
 *   ruler = 80
 *
 * To test:
 * 1. Create the config directory:
 *      mkdir -p ~/.config/edit
 *
 * 2. Create/edit the config file:
 *      echo "tabstop = 4" > ~/.config/edit/config
 *      echo "ruler = 80" >> ~/.config/edit/config
 *      echo "theme = Cyberpunk" >> ~/.config/edit/config
 *
 * 3. Restart the editor and open this file again.
 *    EXPECT: Tab stops display at 4-space width instead of 8.
 *    A ruler line appears at column 80.
 *    The Cyberpunk theme is active (or whichever you set).
 *
 * Note: The theme setting is automatically updated when you
 * cycle themes with Alt+T. Other settings require manual editing.
 *
 * Unknown keys are silently ignored. Parse errors show in the
 * status bar at startup (e.g., "Config error line 3: missing '='").
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 36: Ctrl Key Aliases
 *
 * Many Alt+key bindings have Ctrl+key aliases for convenience.
 *
 * 1. Press Ctrl+S.
 *    EXPECT: Same as Alt+S (save).
 *
 * 2. Press Ctrl+F.
 *    EXPECT: Same as Alt+F (search prompt opens). Press ESC to cancel.
 *
 * 3. Press Ctrl+G.
 *    EXPECT: Same as Alt+G (go-to-line prompt). Press ESC to cancel.
 *
 * 4. Press Ctrl+Q.
 *    EXPECT: Same as Alt+Q (quit -- answer the prompt if dirty).
 *
 * Here's the full alias table:
 *   Ctrl+S  =  Alt+S   (save)
 *   Ctrl+Q  =  Alt+Q   (quit)
 *   Ctrl+F  =  Alt+F   (find)
 *   Ctrl+G  =  Alt+G   (go to line)
 *   Ctrl+A  =  Home    (start of line)
 *   Ctrl+E  =  End     (end of line)
 *   Ctrl+U  =  Undo
 *   Ctrl+R  =  Redo
 *   Ctrl+Z  =  Suspend
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 37: Atomic Saves and File Safety
 *
 * The editor has several layers of file safety:
 *
 * ATOMIC SAVES:
 *   When you save, the editor writes to a temporary file first,
 *   then renames it over the original. This means the original file
 *   is never corrupted, even if the write fails halfway through.
 *   An fsync() is called before rename for durability.
 *
 * SWAP FILES:
 *   Every 30 seconds, if there are unsaved changes, the editor
 *   writes a swap file (filename.edit.swp). If the editor crashes,
 *   you can recover your work. The swap file is automatically removed
 *   on clean exit or successful save.
 *
 * FILE CHANGE DETECTION:
 *   Before saving, the editor checks if the file's modification time
 *   or inode has changed since it was opened. If another program
 *   modified the file, you get a warning: "File changed on disk.
 *   Save anyway?" This prevents accidentally overwriting changes
 *   made by another editor or tool.
 *
 * EMERGENCY SAVE:
 *   If the editor receives SIGTERM or SIGHUP, it attempts to save
 *   unsaved work to a .edit_recovery_PID file before exiting.
 *
 * (No practice steps here -- just know these features are protecting
 * your work behind the scenes.)
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 38: Stdin Pipe Support
 *
 * The editor can read from stdin when piped, letting you use it as
 * a viewer or editor for command output.
 *
 * Try these in your shell (not inside the editor!):
 *
 *   echo "hello world" | ./edit
 *   cat /etc/hosts | ./edit
 *   ls -la | ./edit
 *   git diff | ./edit
 *
 * EXPECT: The editor opens with the piped content in the buffer.
 * The file shows as "[No Name]" since it came from stdin.
 * You can edit, search, and save to a new filename.
 *
 * When stdin is a pipe, the editor reads all data, then reopens
 * /dev/tty for terminal input so keyboard interaction works normally.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 39: File Locking and Read-Only Detection
 *
 * FILE LOCKING:
 *   When the editor opens a file, it acquires an exclusive flock().
 *   If another instance of the editor has the same file open, the
 *   new instance opens it in read-only mode.
 *
 * READ-ONLY DETECTION:
 *   If the file is not writable (permissions), the editor detects
 *   this on open and shows "[RO]" in the status bar.
 *
 * To test file locking:
 * 1. Open this file in the editor: ./edit test_features.c
 * 2. Open a second terminal and try: ./edit test_features.c
 *    EXPECT: The second instance shows "[RO]" and a message about
 *    the file being locked by another process.
 *
 * To test read-only detection:
 * 1. Create a read-only file:
 *      echo "read only" > /tmp/test_ro.txt
 *      chmod 444 /tmp/test_ro.txt
 * 2. Open it: ./edit /tmp/test_ro.txt
 *    EXPECT: "[RO]" appears in the status bar.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 40: Unicode Stress Test
 *
 * The editor uses the gstr library for grapheme-aware editing. Every
 * cursor movement, insertion, and deletion operates on grapheme cluster
 * boundaries (not raw bytes or codepoints).
 *
 * 1. Arrow through each line below character by character.
 *    EXPECT: The cursor never lands in the middle of an emoji or
 *    grapheme cluster. Each arrow press moves past one complete
 *    visual character.
 *
 * 2. Try selecting emoji and CJK characters with Shift+Right.
 *    EXPECT: Selection highlights one visual character at a time.
 *
 * 3. Try Ctrl+Right word movement across the CJK text.
 *    EXPECT: Word boundaries are detected correctly.
 *
 * 4. Try typing between emoji -- place cursor between two emoji
 *    and type a character.
 *    EXPECT: The character inserts cleanly without corrupting
 *    the surrounding emoji.
 *
 * 5. Try deleting emoji with Backspace and Delete.
 *    EXPECT: The entire grapheme cluster is removed, not just
 *    one byte or codepoint.
 * ===========================================================================
 */

>>> Simple emoji: 🌍🌎🌏 and 😀😎🤖🎉🚀💡
>>> Flag sequences: 🇺🇸🇬🇧🇯🇵🇩🇪🇫🇷🇧🇷🇨🇦🇦🇺
>>> CJK wide chars: 日本語テスト 中文测试 한국어
>>> ZWJ families: 👨‍👩‍👧‍👦 👩‍👩‍👦‍👦 👨‍👨‍👧‍👧
>>> Skin tones: 👋🏻👋🏽👋🏿 🤝🏻 🤝🏿
>>> Fullwidth text: Ｆｕｌｌｗｉｄｔｈ　ｔｅｘｔ
>>> Mixed: Hello🌍World こんにちは🎌세계 🇺🇸USA
>>> Combining marks: é (e + combining acute) ñ (n + combining tilde)
>>> Math symbols: ∀x∈ℝ: x² ≥ 0 ∧ √(x²) = |x|
>>> Box drawing: ┌──────┬──────┐
>>>              │ cell │ cell │
>>>              └──────┴──────┘


/* ===========================================================================
 *
 *    ╔══════════════════════════════════════════════════════════════════╗
 *    ║                                                                ║
 *    ║              CONGRATULATIONS! You've completed                 ║
 *    ║              all 40 lessons of the edit tutorial.              ║
 *    ║                                                                ║
 *    ╚══════════════════════════════════════════════════════════════════╝
 *
 *    Here's a summary of everything you tested:
 *
 *    NAVIGATION (Lessons 1-2)
 *      - Arrow keys, Home/End, Ctrl+A/E, PgUp/PgDn
 *      - Alt+G / Ctrl+G go-to-line
 *      - Mouse click to position, scroll wheel with acceleration
 *      - Alt+H/J/K/L vim-style movement
 *
 *    EDITING (Lessons 3-5)
 *      - Insert characters, Backspace, Delete, Enter
 *      - Auto-indent (leading whitespace preservation)
 *      - Undo / Redo with time-based grouping
 *
 *    VIEWPORT (Lessons 6-7)
 *      - 5-line scroll margin
 *      - Virtual column preservation across short lines
 *
 *    WORD MOVEMENT (Lesson 8)
 *      - Ctrl+Left/Right with three-class model
 *
 *    SELECTION (Lessons 9-11)
 *      - Shift+Arrow keyboard selection
 *      - Shift+Home/End, Alt+A/E select-to-boundary
 *      - Shift+Ctrl+Arrow word selection
 *      - Mouse click-and-drag selection
 *
 *    CLIPBOARD (Lessons 12-14)
 *      - Alt+C copy, Alt+X cut, Alt+V paste, OSC 52
 *      - Selection + typing replaces selected text
 *      - Alt+Shift+K cut line, Alt+D duplicate line
 *
 *    SEARCH (Lessons 15-20)
 *      - Incremental search (Alt+F / Ctrl+F)
 *      - Case-insensitive toggle (Alt+C during search)
 *      - Regex toggle (Alt+X during search, POSIX ERE)
 *      - All-match highlighting and match count
 *      - Search history (Up/Down arrows, 50-entry ring)
 *      - Find and replace (Alt+R, y/n/a/ESC)
 *
 *    SYNTAX & BRACKETS (Lessons 21-23)
 *      - Syntax highlighting for 9 languages
 *      - Bracket matching (Alt+])
 *      - Bracket pair colorization by depth
 *
 *    CODE EDITING (Lessons 24-25)
 *      - Comment toggle (Alt+/)
 *      - Block indent/dedent (Tab / Shift+Tab with selection)
 *
 *    VISUAL FEATURES (Lessons 26-30)
 *      - Git gutter markers (+ added, ~ modified)
 *      - Trailing whitespace visualization
 *      - Horizontal scroll indicators (< and >)
 *      - Status bar (filename, [RO], [+], filetype, position)
 *      - 7 color themes with Alt+T (persisted to config)
 *
 *    DISPLAY & HELP (Lessons 31-34)
 *      - Help screen (F11 / Alt+?)
 *      - Suspend/resume (Ctrl+Z / fg)
 *      - Soft word wrap (Alt+W)
 *      - Column ruler (via config file)
 *
 *    CONFIG & SAFETY (Lessons 35-39)
 *      - Config file (~/.config/edit/config)
 *      - Ctrl key aliases for common operations
 *      - Atomic saves (temp file + rename + fsync)
 *      - Swap files for crash recovery
 *      - File change detection on save
 *      - Stdin pipe support (echo "hello" | ./edit)
 *      - File locking and read-only detection
 *
 *    UNICODE (Lesson 40)
 *      - Emoji, flags, CJK, ZWJ sequences
 *      - Grapheme-aware cursor movement and editing
 *      - Combining marks and fullwidth characters
 *
 *    To discard all changes:  git checkout test_features.c
 *    To quit without saving:  Alt+Q (or Ctrl+Q), then 'n'
 *
 * ===========================================================================
 */

int main(void) { return 0; }
