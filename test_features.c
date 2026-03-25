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
 *    There are 40 lessons covering every feature of the editor.
 *    Take your time. The cursor position, line number, and percentage are
 *    always visible in the status bar at the bottom of the screen.
 *
 * ============================================================================
 */


/* ===========================================================================
 *
 *    BASICS (Lessons 1-5)
 *
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 1: Opening the Command Prompt
 *
 * The editor has a command prompt for executing commands by name.
 * There are two ways to open it:
 *
 * 1. Press ESC (when no selection is active and help is not showing).
 *    EXPECT: The message bar shows "> " and waits for your command.
 *
 * 2. Press Ctrl+Space (works in any context, always).
 *    EXPECT: Same "> " prompt appears.
 *
 * 3. To close the prompt without doing anything, press ESC again.
 *    EXPECT: Prompt disappears, back to normal editing.
 *
 * 4. Ctrl+C also cancels the prompt, just like ESC.
 *
 * Try both methods now. Open the prompt, then close it with ESC.
 * You will use the command prompt throughout this tutorial.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 2: Basic Navigation
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
 * 6. Press Ctrl+G. Type "1" and press Enter.
 *    EXPECT: The cursor jumps to the very first line of this file.
 *
 * 7. Press Ctrl+G again. Type "100" and press Enter.
 *    EXPECT: The cursor jumps to line 100.
 * ===========================================================================
 */

>>> Navigate to the start and end of this line using Home and End.
>>> Then try Ctrl+G to jump to a specific line number.


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
 *    The new line inherits the indentation of the original (auto-indent).
 *
 * 5. Press Backspace at column 0 of the new second half.
 *    EXPECT: The two lines rejoin into one.
 * ===========================================================================
 */

>>> Type at the end of this line:
>>> Split this line in the middle by pressing Enter


/* ===========================================================================
 * LESSON 4: Undo / Redo
 *
 * The undo system uses 256 groups with time-based coalescing.
 * Characters typed quickly together are grouped into one undo step.
 * A pause of ~500ms starts a new group. Enter always starts a new group.
 *
 * 1. Place cursor at the end of the first >>> line below.
 * 2. Type: THIS IS A TEST
 * 3. Press Ctrl+Z.
 *    EXPECT: Your typed text disappears (undone as one group).
 *
 * 4. Press Ctrl+Y.
 *    EXPECT: The text reappears (redone).
 *
 * 5. Press Ctrl+Z to undo, then type something NEW.
 * 6. Press Ctrl+Y.
 *    EXPECT: "Nothing to redo" -- new edits discard the redo history.
 *
 * 7. On the second >>> line, type "hello" quickly, wait 1 second,
 *    then type "world" quickly.
 * 8. Press Ctrl+Z once.
 *    EXPECT: Only "world" disappears (it was a separate undo group).
 * 9. Press Ctrl+Z again.
 *    EXPECT: "hello" disappears too.
 * ===========================================================================
 */

>>> Type here then undo with Ctrl+Z:
>>> Type with pauses to test undo grouping:


/* ===========================================================================
 * LESSON 5: Save and Quit
 *
 * SAVE:
 * 1. Press Ctrl+S.
 *    EXPECT: The file is saved. Status bar shows bytes written.
 *
 * 2. Or open the command prompt (ESC), type: save
 *    Press Enter. Same result.
 *
 * 3. For save-as: ESC, type: save as /tmp/test_backup.c
 *    Press Enter. File is saved to the new path.
 *
 * QUIT:
 * 1. Make an edit on the >>> line below, then press Ctrl+Q.
 *    EXPECT: "Unsaved changes. Save before quitting? (y/n/ESC)"
 *    Press ESC to cancel and stay in the editor.
 *
 * 2. Or use the command prompt: ESC, type: quit
 *    Same behavior as Ctrl+Q.
 *
 * 3. Force quit: ESC, type: quit!
 *    This exits immediately with no save check. Use with caution!
 * ===========================================================================
 */

>>> Type something here then try quitting with Ctrl+Q:


/* ===========================================================================
 *
 *    COMMANDS (Lessons 6-10)
 *
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 6: Tab Completion
 *
 * You don't have to type full command names. Tab completes them.
 *
 * 1. Press ESC to open the command prompt.
 * 2. Type: sa
 * 3. Press Tab.
 *    EXPECT: Completes to "save".
 *
 * 4. Press Tab again.
 *    EXPECT: Cycles to "save as".
 *
 * 5. Press Tab again.
 *    EXPECT: Cycles back to "save".
 *
 * 6. Press Enter to execute, or ESC to cancel.
 *
 * Try with other prefixes:
 *   th + Tab  -->  theme
 *   qu + Tab  -->  quit  -->  quit!
 *   se + Tab  -->  select  -->  select all  -->  ...
 *   fi + Tab  -->  find
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 7: Search Fallback
 *
 * When your input doesn't match any command, it becomes a search.
 *
 * 1. Press ESC to open the command prompt.
 * 2. Type: printf
 * 3. Press Enter.
 *    EXPECT: Since "printf" isn't a command, edit searches for "printf"
 *    in the file. The cursor jumps to the first match.
 *
 * This means the command prompt doubles as a quick search:
 *   ESC --> type what you're looking for --> Enter
 *
 * Keyboard shortcut equivalent: Ctrl+F (find)
 * ===========================================================================
 */

int main(void)
{
	printf("This line has printf for the search fallback test\n");
	return 0;
}


/* ===========================================================================
 * LESSON 8: Theme Cycling with Live Preview
 *
 * The command prompt offers live preview for themes via Tab.
 *
 * 1. Press ESC, type: theme
 * 2. Press Tab.
 *    EXPECT: The prompt fills "theme Cyberpunk" and the entire editor
 *    redraws with that theme -- live preview!
 *
 * 3. Press Tab again.
 *    EXPECT: Cycles to "theme Nightwatch" with live preview.
 *
 * 4. Keep pressing Tab to cycle through all 7 themes:
 *      Cyberpunk, Nightwatch, Daywatch, Tokyo Night,
 *      Akira, Tokyo Cyberpunk, Clarity
 *
 * 5. When you see one you like, press Enter to keep it.
 *    Or press ESC to revert to the original theme.
 *
 * You can also set a theme directly:
 *   ESC --> theme Tokyo Night --> Enter
 *
 * The selected theme is saved to ~/.config/edit/config automatically.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 9: Editor Settings
 *
 * The "set" command changes editor settings on the fly.
 *
 * TAB WIDTH:
 * 1. ESC, type: set tabstop 4
 *    EXPECT: All tabs display as 4 spaces wide instead of 8.
 * 2. ESC, type: set tabstop 8
 *    EXPECT: Back to 8-wide tabs.
 *
 * COLUMN RULER:
 * 1. ESC, type: set ruler 80
 *    EXPECT: A subtle vertical line appears at column 80.
 * 2. ESC, type: set ruler 0
 *    EXPECT: Ruler disappears.
 *
 * WORD WRAP:
 * 1. ESC, type: wrap  (or: set wrap)
 *    EXPECT: "Word wrap: on" -- long lines wrap at the screen edge.
 * 2. ESC, type: wrap
 *    EXPECT: "Word wrap: off" -- back to horizontal scrolling.
 *
 * LINE NUMBERS:
 * 1. ESC, type: numbers  (or: set numbers)
 *    EXPECT: Line numbers disappear from the gutter.
 * 2. ESC, type: numbers
 *    EXPECT: Line numbers reappear.
 *
 * These settings take effect immediately with live preview as you type.
 * Try: ESC, type "set tabstop " then type digits -- watch tabs change live!
 *
 * Look at the ruler on this long line:
 * ===========================================================================
 */

>>> This is a long line for testing the column ruler. It goes past 80 columns so you can see where the ruler appears. Keep going... and going... and going all the way past the ruler position.


/* ===========================================================================
 * LESSON 10: Suspend and Help
 *
 * SUSPEND:
 * 1. ESC, type: suspend
 *    EXPECT: The editor disappears and you see your shell prompt.
 * 2. Type: fg
 *    EXPECT: The editor reappears exactly as you left it.
 *
 * This is useful for running a quick shell command without quitting.
 *
 * HELP:
 * 1. Press F1 (or F11, or ESC then type: help).
 *    EXPECT: A help screen loads with all keybindings and commands.
 *    Scroll through it to see the full reference.
 * 2. Press ESC or Ctrl+Q to return to this file.
 *    EXPECT: Your file reappears with cursor exactly where you left it.
 *    Any unsaved edits are still present.
 * ===========================================================================
 */


/* ===========================================================================
 *
 *    NAVIGATION (Lessons 11-14)
 *
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 11: Word Movement
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
 * LESSON 12: Goto Commands
 *
 * The goto command offers several ways to jump around:
 *
 * 1. ESC, type: goto 42
 *    EXPECT: Cursor jumps to line 42, centered on screen.
 *
 * 2. ESC, type: goto top
 *    EXPECT: Cursor jumps to the first line.
 *
 * 3. ESC, type: goto bottom
 *    EXPECT: Cursor jumps to the last line.
 *
 * 4. Place cursor on a bracket, then: ESC, type: goto match
 *    EXPECT: Cursor jumps to the matching bracket.
 *
 * 5. Ctrl+G also opens the go-to-line prompt directly.
 *
 * The goto command supports range syntax too:
 *   goto .     Jump to current line (no-op, but useful in ranges)
 *   goto ^     Jump to line 1 (same as goto top)
 *   goto $     Jump to last line (same as goto bottom)
 *
 * Try jumping to a few line numbers to get comfortable with navigation.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 13: Scroll Behavior
 *
 * The editor keeps about 5 lines of context visible above and below your
 * cursor. You should never feel like you're typing at the very edge.
 *
 * 1. Hold the Down arrow and scroll through this file.
 *    EXPECT: The viewport begins scrolling BEFORE the cursor reaches
 *    the bottom row. You always see lines below the cursor.
 *
 * 2. Hold the Up arrow going back.
 *    EXPECT: Same behavior at the top.
 *
 * 3. Use the mouse scroll wheel quickly.
 *    EXPECT: Scroll speed accelerates when you scroll fast (up to 10x).
 *    Events within 50ms apart go faster; events more than 200ms
 *    apart reset to speed 1.
 *
 * 4. Slow down your scroll wheel.
 *    EXPECT: Speed decelerates back to 1 line per tick.
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
/* padding */ /* padding */ /* padding */ /* padding */ /* padding */
/* padding */ /* padding */ /* padding */ /* padding */ /* padding */
/* padding */ /* padding */ /* padding */ /* padding */ /* padding */


/* ===========================================================================
 * LESSON 14: Virtual Column Preservation
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
>>> short_var = 1;
>>> x = 2;
>>> int another_very_long_variable_name_matching_the_first_line_exactly = 200;


/* ===========================================================================
 *
 *    SELECTION (Lessons 15-18)
 *
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 15: Keyboard Selection
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
 *    (These extend the selection, they don't just move.)
 *
 * 9. Press ESC to clear any selection.
 * ===========================================================================
 */

>>> Hello World, this is a selection test line with some extra text.
>>> And this is the second line for multi-line selection practice.
>>> Third line here for good measure. Keep going!
>>> Fourth line to give you plenty of room to experiment.


/* ===========================================================================
 * LESSON 16: Word Selection
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
 *
 * 5. Press ESC to clear.
 * ===========================================================================
 */

>>> the quick brown fox jumps over the lazy dog
>>> some_variable += another_variable * coefficient;


/* ===========================================================================
 * LESSON 17: Mouse Selection
 *
 * Click and drag to select text with your mouse.
 *
 * 1. Click at the 'M' in "Mouse" on the >>> line below. Hold the button.
 * 2. Drag to the right across "selection".
 *    EXPECT: Text highlights between the click point and the drag point.
 *
 * 3. Release the button.
 *    EXPECT: Selection stays highlighted (finalized on release).
 *    After release, moving the cursor does not change the selection.
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
 * LESSON 18: Select Commands
 *
 * The command prompt offers selection commands for precision:
 *
 * 1. ESC, type: select all
 *    EXPECT: The entire file is selected (highlighted).
 *    Press ESC to clear.
 *
 * 2. ESC, type: select line
 *    EXPECT: The current line is selected.
 *
 * 3. ESC, type: select word
 *    EXPECT: The word under the cursor is selected.
 *
 * 4. Place cursor on a bracket in the code below, then:
 *    ESC, type: select block
 *    EXPECT: Everything between the matching brackets is selected.
 *
 * 5. You can also select a range of lines:
 *    ESC, type: select line 1,10
 *    EXPECT: Lines 1 through 10 are selected.
 * ===========================================================================
 */

int select_demo(int x)
{
	if (x > 0) {
		return (x * 2) + 1;
	}
	return 0;
}


/* ===========================================================================
 *
 *    CLIPBOARD (Lessons 19-22)
 *
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 19: Copy / Cut / Paste
 *
 * Ctrl+C copies, Ctrl+X cuts, Ctrl+V pastes.
 * The editor also pushes to system clipboard via OSC 52.
 *
 * WITH A SELECTION:
 * 1. Select "COPY_THIS" on the first >>> line (Shift+Arrow or mouse).
 * 2. Press Ctrl+C.
 *    EXPECT: Status bar shows "Copied N bytes".
 * 3. Move cursor to the blank >>> line.
 * 4. Press Ctrl+V.
 *    EXPECT: "COPY_THIS" appears at cursor position.
 *
 * 5. Select "CUT_THIS" on the third >>> line.
 * 6. Press Ctrl+X.
 *    EXPECT: "CUT_THIS" disappears (cut to clipboard).
 * 7. Move to the second blank line and press Ctrl+V.
 *    EXPECT: "CUT_THIS" appears.
 *
 * WITHOUT A SELECTION (VS Code convention):
 * 8. Place cursor on any line, don't select anything.
 * 9. Press Ctrl+C.
 *    EXPECT: "Line copied" -- the entire line was copied.
 * 10. Press Ctrl+V on an empty line.
 *     EXPECT: The whole line is pasted.
 *
 * 11. With no selection, Ctrl+X cuts the entire line too.
 * ===========================================================================
 */

>>> Please COPY_THIS to the line below.
>>>
>>> Please CUT_THIS to another line.
>>>


/* ===========================================================================
 * LESSON 20: Clipboard Commands
 *
 * The command prompt offers fine-grained clipboard operations:
 *
 * 1. ESC, type: copy line
 *    EXPECT: The current line is copied (same as Ctrl+C with no selection).
 *
 * 2. ESC, type: copy word
 *    EXPECT: The word under the cursor is copied.
 *
 * 3. ESC, type: copy all
 *    EXPECT: The entire file contents are copied to the clipboard.
 *
 * 4. ESC, type: copy path
 *    EXPECT: The filename of the current file is copied to the clipboard.
 *
 * Try each one and check the status bar message for confirmation.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 21: Cut and Paste Commands
 *
 * CUT commands remove text AND copy it to the clipboard:
 *
 * 1. Place cursor on the first >>> line below.
 *    ESC, type: cut line
 *    EXPECT: The line vanishes. It's in the clipboard.
 *
 * 2. ESC, type: cut word
 *    EXPECT: The word under the cursor is removed and copied.
 *
 * 3. ESC, type: cut trailing
 *    EXPECT: Trailing whitespace is removed from all lines.
 *
 * PASTE commands control where text is inserted:
 *
 * 4. ESC, type: paste above
 *    EXPECT: Clipboard contents are pasted ABOVE the current line.
 *
 * 5. ESC, type: paste below
 *    EXPECT: Clipboard contents are pasted BELOW the current line.
 *
 * 6. ESC, type: paste
 *    EXPECT: Same as Ctrl+V -- paste at cursor position.
 * ===========================================================================
 */

>>> CUT THIS LINE -- it should vanish with "cut line"
>>> Cut the word CUTWORD from this line with "cut word"
>>> This line has trailing spaces to test "cut trailing"   


/* ===========================================================================
 * LESSON 22: Duplicate
 *
 * Ctrl+D duplicates the current line.
 *
 * 1. Place cursor on the >>> line below.
 * 2. Press Ctrl+D.
 *    EXPECT: An identical copy appears immediately below,
 *    and your cursor moves to the new copy.
 *
 * 3. Press Ctrl+Z to undo.
 *
 * The "dup" command supports repeat prefixes:
 * 4. ESC, type: 3 dup line
 *    EXPECT: Three copies of the current line appear below it.
 *
 * 5. ESC, type: dup
 *    EXPECT: Duplicates the selection if one is active, or the current
 *    line if nothing is selected.
 * ===========================================================================
 */

>>> DUPLICATE ME -- press Ctrl+D to clone this line


/* ===========================================================================
 *
 *    SEARCH (Lessons 23-26)
 *
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 23: Incremental Search
 *
 * Ctrl+F opens the search prompt. Results update live as you type.
 *
 * 1. Press Ctrl+F.
 *    EXPECT: A "Search:" prompt appears in the message bar.
 *
 * 2. Type "TODO" slowly, one letter at a time.
 *    EXPECT: The cursor jumps to the first match as you type.
 *    The matched text is highlighted with the match color.
 *
 * 3. Press Right arrow to jump to the next match.
 *    EXPECT: Cursor moves forward to the next "TODO".
 *
 * 4. Press Left arrow to jump to the previous match.
 *    EXPECT: Cursor moves backward.
 *
 * 5. Press Up/Down arrows to browse search history.
 *    EXPECT: Previous search queries fill the search field.
 *
 * 6. Press Enter to accept the search (cursor stays at match).
 *
 * 7. Press ESC or Ctrl+C during a search.
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
 * LESSON 24: Case and Regex Toggles
 *
 * During a search, special keys toggle search modes:
 *
 * CASE SENSITIVITY:
 * 1. Press Ctrl+F, type "todo" (lowercase).
 *    EXPECT: No matches -- the markers above use uppercase "TODO".
 * 2. Press Alt+C to toggle case sensitivity off.
 *    EXPECT: Status shows "[case off]". Now it finds "TODO".
 * 3. Press Alt+C again to restore case sensitivity.
 *
 * REGEX MODE:
 * 4. Press Ctrl+F, type "TODO|FIXME" (literal text).
 *    EXPECT: No match (looking for the literal string "TODO|FIXME").
 * 5. Press Alt+X to enable regex mode (POSIX ERE).
 *    EXPECT: Status shows "[regex on]". Pattern matches both TODO and FIXME.
 * 6. Try the regex pattern: "[a-z]_[a-z]"
 *    EXPECT: Matches around underscores in variable names.
 * 7. Press Alt+X to disable regex mode.
 *
 * 8. Press ESC to cancel.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 25: All-Match Highlighting and Match Count
 *
 * During incremental search, ALL matches in the visible area are
 * highlighted, not just the current one. The status bar shows a count.
 *
 * 1. Press Ctrl+F and type "int".
 *    EXPECT: Every "int" on screen is highlighted in the match color.
 *    The status bar shows "Match N of M" (e.g., "Match 1 of 42").
 *
 * 2. Press Right arrow to navigate to the next match.
 *    EXPECT: The match counter updates: "Match 2 of 42", etc.
 *
 * 3. Keep pressing Right until the search wraps around.
 *    EXPECT: Status bar shows "[Wrapped] Match 1 of 42".
 *
 * 4. Press ESC to cancel.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 26: Find and Replace
 *
 * Ctrl+H opens find-and-replace.
 *
 * 1. Press Ctrl+H.
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
 *
 * You can also use the command: ESC, type: replace
 * ===========================================================================
 */

>>> The PLACEHOLDER value goes here.
>>> Another PLACEHOLDER to replace.
>>> A third PLACEHOLDER for the "all" option.
>>> The last PLACEHOLDER in this lesson.


/* ===========================================================================
 *
 *    CODE EDITING (Lessons 27-30)
 *
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 27: Comment Toggle
 *
 * Ctrl+/ toggles line comments. Works on single lines and selections.
 * Uses the filetype's comment style (// for C, # for Python, etc.).
 *
 * 1. Place cursor on the first >>> line below. Press Ctrl+/.
 *    EXPECT: "// " is prepended to the line (commented out).
 *
 * 2. Press Ctrl+/ again on the same line.
 *    EXPECT: The "// " prefix is removed (uncommented).
 *
 * 3. Select multiple lines (the three >>> lines below) using
 *    Shift+Down, then press Ctrl+/.
 *    EXPECT: All three lines get "// " prepended.
 *
 * 4. With the same lines still selected, press Ctrl+/ again.
 *    EXPECT: All three lines have "// " removed.
 *
 * The toggle is smart: if ALL selected lines are commented, it
 * uncomments. If any line is NOT commented, it comments all of them.
 *
 * Command equivalent: ESC, type: comment
 * ===========================================================================
 */

>>> int comment_me = 1;
>>> int comment_me_too = 2;
>>> int and_me = 3;


/* ===========================================================================
 * LESSON 28: Indent / Dedent Block
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
 * Command equivalents: indent, outdent
 * ===========================================================================
 */

>>> if (condition) {
>>> 	nested_call();
>>> 	another_call();
>>> }


/* ===========================================================================
 * LESSON 29: Bracket Matching
 *
 * Press Ctrl+] when the cursor is on a bracket to jump to its match.
 * Brackets are colorized by nesting depth (bracket pair colorization).
 *
 * 1. Place cursor on the opening '{' of the function below.
 *    EXPECT: The matching closing '}' is subtly highlighted.
 *
 * 2. Press Ctrl+].
 *    EXPECT: Cursor jumps to the matching closing '}'.
 *
 * 3. Press Ctrl+] again.
 *    EXPECT: Cursor jumps back to the opening '{'.
 *
 * 4. Try with parentheses '(' and ')'.
 * 5. Try with square brackets '[' and ']'.
 *
 * 6. Place cursor on a character that is NOT a bracket.
 * 7. Press Ctrl+].
 *    EXPECT: Status bar says "No matching bracket".
 *
 * BRACKET PAIR COLORIZATION:
 * Look at the deeply nested code below. Each nesting level gets
 * a different color, cycling through 4 colors. Brackets inside
 * strings and comments keep their string/comment color.
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


/* ===========================================================================
 * LESSON 30: Text Transforms
 *
 * The command prompt has powerful text transformation commands.
 * These operate on the selection, a line range, or the current line.
 *
 * CASE TRANSFORMS:
 * 1. Select "transform me" on the first >>> line below.
 *    ESC, type: upper
 *    EXPECT: "transform me" becomes "TRANSFORM ME".
 *
 * 2. ESC, type: lower
 *    EXPECT: "TRANSFORM ME" becomes "transform me".
 *
 * 3. ESC, type: title
 *    EXPECT: "transform me" becomes "Transform Me".
 *
 * LINE TRANSFORMS (select the block of >>> lines below):
 * 4. ESC, type: sort
 *    EXPECT: Lines are sorted alphabetically ascending.
 *
 * 5. ESC, type: sort reverse
 *    EXPECT: Lines are sorted descending.
 *
 * 6. ESC, type: reverse
 *    EXPECT: Line order is reversed (not sorted, just flipped).
 *
 * 7. ESC, type: uniq
 *    EXPECT: Adjacent duplicate lines are removed.
 *
 * 8. ESC, type: trim
 *    EXPECT: Trailing whitespace is stripped from each line.
 *
 * 9. ESC, type: number
 *    EXPECT: Each line gets a number prefix (1. 2. 3. etc.).
 *
 * 10. ESC, type: collapse
 *     EXPECT: Multiple consecutive blank lines collapse to one.
 *
 * All transforms support ranges: ESC, type: 5,10 upper
 * ===========================================================================
 */

>>> transform me to see case changes
>>> cherry
>>> apple
>>> banana
>>> date
>>> apple
>>> banana


/* ===========================================================================
 *
 *    DISPLAY (Lessons 31-34)
 *
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 31: Syntax Highlighting
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
 * - Multi-line comments spanning lines correctly
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
	typedef struct { int x; int y; } point;
	enum color { RED, GREEN, BLUE };
	static const char *names[] = {"alpha", "beta", "gamma"};

	return number;
}


/* ===========================================================================
 * LESSON 32: Git Gutter Markers
 *
 * When line numbers are visible, the gutter shows change markers:
 *   + (green)  = added line (new line that didn't exist before)
 *   ~ (yellow) = modified line (existing line that was changed)
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
 *    The gutter tracks changes since last save, not since last commit.
 * ===========================================================================
 */

>>> Modify this line to see the ~ gutter marker.


/* ===========================================================================
 * LESSON 33: Trailing Whitespace and Scroll Indicators
 *
 * TRAILING WHITESPACE:
 * The editor highlights trailing whitespace with a subtle background
 * tint. This makes it easy to spot and clean up trailing spaces/tabs.
 * Look at the lines below -- some have trailing whitespace:
 * ===========================================================================
 */

>>> This line has trailing spaces.      
>>> This line has trailing tabs.		
>>> This line has NO trailing whitespace.
>>> Mix of content then spaces.   
>>> Clean line, no trailing.

/* ===========================================================================
 * (Lesson 33 continued)
 *
 * HORIZONTAL SCROLL INDICATORS:
 * When a line extends beyond the right edge of your terminal, a ">"
 * appears at the rightmost column. When you've scrolled right past
 * the start, a "<" appears at the leftmost column.
 *
 * 1. Place cursor on the very long >>> line below.
 * 2. Press End to go to the end of the line.
 *    EXPECT: A "<" appears on the left edge (content hidden to the left).
 * 3. Press Home to go back.
 *    EXPECT: A ">" appears on the right edge (content extends rightward).
 *
 * Note: Indicators only appear when word wrap is OFF (the default).
 * ===========================================================================
 */

>>> This is an extremely long line that is designed to extend far beyond the right edge of most terminal windows because it contains a very lengthy sequence of words and characters that just keeps going and going and going, well past the 80-column mark, past the 120-column mark, and even past the 160-column mark, continuing with more and more text until it finally reaches this point where we can stop and say: you have scrolled far enough to see both the < and > indicators. Congratulations, intrepid horizontal scroller!


/* ===========================================================================
 * LESSON 34: Word Wrap
 *
 * ESC, type: wrap  (toggles soft word wrap)
 *
 * 1. ESC, type: wrap
 *    EXPECT: Status says "Word wrap: on".
 *    The very long line above and the >>> line below now wrap to
 *    multiple screen rows. No horizontal scrolling needed.
 *
 * 2. Look at the gutter on continuation rows.
 *    EXPECT: Continuation rows show ".." in the gutter instead of
 *    a line number, so you know they're part of the same line.
 *
 * 3. Move cursor through the wrapped line with Up/Down arrows.
 *    EXPECT: The cursor moves through visual rows, not file lines.
 *    This feels natural -- each visible row is one step.
 *
 * 4. ESC, type: wrap
 *    EXPECT: "Word wrap: off". Lines go back to single-row display
 *    with horizontal scrolling.
 * ===========================================================================
 */

>>> This is another very long line that is specifically here for testing word wrap mode. When wrap is active, this line should wrap to multiple visual rows on your screen. Each continuation row gets a ".." marker in the gutter area where the line number would normally appear. The cursor should still navigate correctly through the wrapped text. Word wrap makes it much easier to read long lines without horizontal scrolling.


/* ===========================================================================
 *
 *    ADVANCED (Lessons 35-38)
 *
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 35: Bookmarks
 *
 * Bookmarks let you save cursor positions and jump back to them.
 * Each bookmark is named with a single lowercase letter (a-z).
 *
 * 1. Place cursor somewhere memorable (like this line).
 *    ESC, type: mark a
 *    EXPECT: "Bookmark 'a' set at line N".
 *
 * 2. Navigate far away (Ctrl+G to jump to line 1).
 *
 * 3. ESC, type: jump a
 *    EXPECT: Cursor jumps back to where you set bookmark 'a'.
 *
 * 4. Set a few more bookmarks:
 *    ESC, type: mark b  (at some other location)
 *    ESC, type: mark c  (at yet another location)
 *
 * 5. ESC, type: marks
 *    EXPECT: Status bar lists all active bookmarks with their positions.
 *
 * 6. ESC, type: mark clear
 *    EXPECT: All bookmarks are cleared.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 36: Macros
 *
 * Record a sequence of keystrokes and replay them.
 *
 * 1. ESC, type: record start
 *    EXPECT: Status bar shows "Recording..." -- every keystroke is captured.
 *
 * 2. Perform some edits:
 *    - Go to the first >>> line below
 *    - Press Home, then type "// " to comment it
 *    - Press Down arrow to move to the next line
 *
 * 3. ESC, type: record stop
 *    EXPECT: "Macro recorded (N events)".
 *
 * 4. ESC, type: record play
 *    EXPECT: The same sequence replays -- the next line gets commented.
 *
 * 5. ESC, type: record play 3
 *    EXPECT: The macro replays 3 more times, commenting 3 more lines.
 *
 * The macro buffer holds up to 1024 events.
 * ===========================================================================
 */

>>> Line one for macro practice
>>> Line two for macro practice
>>> Line three for macro practice
>>> Line four for macro practice
>>> Line five for macro practice
>>> Line six for macro practice


/* ===========================================================================
 * LESSON 37: Stats and Info
 *
 * Several commands show information about the file and cursor:
 *
 * 1. ESC, type: stats
 *    EXPECT: Status bar shows file statistics:
 *    lines, words, characters, and file size.
 *
 * 2. ESC, type: count TODO
 *    EXPECT: Shows the number of occurrences of "TODO" in the file.
 *
 * 3. ESC, type: pos
 *    EXPECT: Shows detailed cursor position info:
 *    line, column, byte offset, percentage through file.
 *
 * These are read-only -- they just display information.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 38: Live Preview and Smart Arguments
 *
 * Several commands show live preview as you type their arguments:
 *
 * SETTINGS PREVIEW:
 * 1. ESC, type: set tabstop
 *    Now type a space and then digits (e.g., "set tabstop 2").
 *    EXPECT: Tab widths change live as you type each digit!
 *    Press ESC to revert, or Enter to keep the new value.
 *
 * 2. Same works for: set ruler 80  (ruler appears as you type 80)
 *
 * THEME PREVIEW:
 * 3. ESC, type: theme
 *    As you Tab through themes, each one previews live.
 *    ESC reverts to the original theme.
 *
 * GOTO PREVIEW:
 * 4. ESC, type: goto
 *    As you type a line number, the cursor moves there in real time.
 *    ESC returns to where you started.
 *
 * RANGE HIGHLIGHTING:
 * 5. ESC, type a line range like: 1,10
 *    EXPECT: Lines 1-10 are highlighted with a preview tint as you
 *    type, showing which lines will be affected by your command.
 *
 * The live preview makes it safe to explore -- ESC always reverts.
 * ===========================================================================
 */


/* ===========================================================================
 *
 *    FILES (Lessons 39-40)
 *
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 39: Config File, Stdin Pipe, File Locking
 *
 * CONFIG FILE:
 * The editor reads settings from ~/.config/edit/config on startup.
 * Format: key = value, one per line, # for comments.
 *
 * Supported settings:
 *   tabstop = 4          Tab display width (1-32)
 *   theme = Tokyo Night  Color theme name
 *   line_numbers = true   Show/hide line numbers
 *   ruler = 80           Column ruler position (0 = off)
 *
 * To create:
 *   mkdir -p ~/.config/edit
 *   echo "tabstop = 4" > ~/.config/edit/config
 *   echo "ruler = 80" >> ~/.config/edit/config
 *
 * STDIN PIPE:
 * The editor reads from stdin when piped:
 *   echo "hello world" | ./edit
 *   git diff | ./edit
 * Opens with piped content as "[No Name]". Keyboard works normally.
 *
 * FILE LOCKING:
 * The editor acquires an exclusive flock() on the file.
 * A second instance opens the same file as read-only with [RO].
 *
 * FILE SAFETY:
 * - Atomic saves: writes to temp file, then renames (never corrupts)
 * - Swap files: auto-saved every 30 seconds (filename.edit.swp)
 * - File change detection: warns if file was modified externally
 * - Emergency save: SIGTERM/SIGHUP saves to .edit_recovery_PID
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 40: Unicode Stress Test
 *
 * The editor uses grapheme-aware cursor movement via the gstr library.
 * The cursor never lands in the middle of a grapheme cluster -- ZWJ
 * sequences, flag emoji, combining marks, and wide characters are
 * all handled correctly.
 *
 * Navigate through the lines below with arrow keys. Each emoji or
 * combined character should be ONE cursor step, not multiple.
 *
 * Press Ctrl+Left and Ctrl+Right to word-jump through them.
 * Try selecting them with Shift+Arrow.
 * ===========================================================================
 */

>>> ASCII:   Hello World 1234567890 ~!@#$%^&*()
>>> Latin:   Straße Ärger Ñoño résumé naïve Ångström
>>> CJK:     漢字テスト 中文测试 한국어 テスト
>>> Emoji:   😀 🎉 🚀 ❤️ 🌍 🎵 🔥 ✨ 💡 🧑‍💻
>>> ZWJ:     👨‍👩‍👧‍👦 👩‍🔬 🏳️‍🌈 👨‍❤️‍👨 🧑‍🤝‍🧑
>>> Flags:   🇺🇸 🇬🇧 🇯🇵 🇩🇪 🇫🇷 🇧🇷 🇰🇷 🇮🇳
>>> Wide:    ＡＢＣ １２３ ！＠＃
>>> Combine: é = e + ◌́     ñ = n + ◌̃     ü = u + ◌̈
>>> Arabic:  مرحبا بالعالم
>>> Thai:    สวัสดีครับ
>>> Mixed:   Hello世界🌍مرحبا🚀résumé👨‍👩‍👧‍👦


/* ===========================================================================
 *
 *    CONGRATULATIONS!
 *
 *    You have completed all 40 lessons of the edit tutorial.
 *    You now know every feature of the editor.
 *
 *    QUICK REFERENCE -- KEYBOARD SHORTCUTS:
 *
 *    File:
 *      Ctrl+S               Save
 *      Ctrl+Q               Quit (prompts if unsaved)
 *
 *    Navigation:
 *      Arrow keys            Move cursor
 *      Home / End            Start / end of line
 *      Ctrl+A / Ctrl+E       Start / end of line
 *      Ctrl+Left/Right       Jump by word
 *      PgUp / PgDn           Scroll by screen
 *      Ctrl+G                Go to line number
 *      Ctrl+]                Jump to matching bracket
 *      Mouse click           Position cursor
 *      Mouse scroll          Scroll with acceleration
 *
 *    Search:
 *      Ctrl+F                Find (incremental)
 *                              Left/Right navigate matches
 *                              Up/Down browse search history
 *                              Alt+C toggle case sensitivity
 *                              Alt+X toggle regex mode
 *                              Enter to accept, ESC to cancel
 *      Ctrl+H                Find and replace (y/n/a/ESC)
 *
 *    Selection:
 *      Shift+Arrow           Select by character/line
 *      Shift+Home/End        Select to line start/end
 *      Shift+Ctrl+Left/Right Select by word
 *      Alt+A / Alt+E         Select to line start / end
 *      Mouse drag            Select with mouse
 *      ESC                   Clear selection
 *
 *    Clipboard:
 *      Ctrl+C                Copy (whole line if no selection)
 *      Ctrl+X                Cut (whole line if no selection)
 *      Ctrl+V                Paste
 *      Ctrl+D                Duplicate line
 *
 *    Editing:
 *      Ctrl+Z                Undo
 *      Ctrl+Y                Redo
 *      Ctrl+/                Toggle line comment
 *      Tab (with selection)  Indent block
 *      Shift+Tab (w/ sel)    Dedent block
 *
 *    Display:
 *      F1 / F11              Help screen
 *      ESC                   Open command prompt
 *      Ctrl+Space            Open command prompt (always)
 *
 *    QUICK REFERENCE -- COMMANDS:
 *
 *    Core:      save, save as, quit, quit!, undo, redo
 *    Navigate:  goto <line|top|bottom|match>, find, replace
 *    Display:   theme, set tabstop, set ruler, wrap, numbers, help
 *    Select:    select all, select line, select word, select block
 *    Clipboard: copy, copy line, copy word, copy all, copy path
 *               cut, cut line, cut word, cut trailing
 *               paste, paste above, paste below
 *               dup, dup line
 *    Transform: upper, lower, title, sort, sort reverse, reverse
 *               uniq, trim, collapse, number, indent, outdent, comment
 *    Advanced:  mark <a-z>, jump <a-z>, marks, mark clear
 *               record start, record stop, record play [N]
 *               stats, count <text>, pos
 *    System:    suspend
 *
 *    Themes: Cyberpunk, Nightwatch, Daywatch, Tokyo Night,
 *            Akira, Tokyo Cyberpunk, Clarity
 *
 *    Languages: C/C++, Python, JavaScript, Go, Rust, Bash,
 *               JSON, YAML, Markdown
 *
 *    To discard changes:  git checkout test_features.c
 *    To quit:             Ctrl+Q  or  ESC --> quit
 *
 * ===========================================================================
 */
