/*
 * ============================================================================
 *
 *                edit -- Command System Tutorial
 *
 *                Open with:  ./edit test_commands.c
 *
 *    This tutorial teaches you the new command system. edit now has TWO
 *    ways to do everything: keyboard shortcuts (Ctrl+key) and the command
 *    prompt (ESC or Ctrl+Space).
 *
 *    Work through each lesson in order. Lines marked >>> are your targets.
 *
 *    To discard changes:  git checkout test_commands.c
 *
 * ============================================================================
 */


/* ===========================================================================
 * LESSON 1: Opening the Command Prompt
 *
 * There are two ways to open the command prompt:
 *
 * 1. Press ESC (when no selection is active and help is not showing).
 *    EXPECT: The message bar shows "> " and waits for your command.
 *
 * 2. Press Ctrl+Space (works in any context).
 *    EXPECT: Same "> " prompt appears.
 *
 * 3. To close the prompt without doing anything, press ESC again.
 *    EXPECT: Prompt disappears, back to normal editing.
 *
 * Try both methods now. Open the prompt, then close it with ESC.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 2: Your First Command — save
 *
 * 1. Press ESC to open the command prompt.
 * 2. Type: save
 * 3. Press Enter.
 *    EXPECT: The file is saved. Status bar shows bytes written.
 *
 * You can also provide a filename:
 * 1. Press ESC.
 * 2. Type: save as /tmp/test_backup.c
 * 3. Press Enter.
 *    EXPECT: File saved to the new path.
 *
 * Keyboard shortcut equivalent: Ctrl+S (save)
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 3: Tab Completion
 *
 * You don't have to type full command names.
 *
 * 1. Press ESC to open the command prompt.
 * 2. Type: sa
 * 3. Press Tab.
 *    EXPECT: Completes to "save" (or shows "save" and "save as" options).
 * 4. Press Tab again to cycle through matches.
 * 5. Press Enter to execute, or ESC to cancel.
 *
 * Try with other prefixes:
 *   th + Tab  →  theme
 *   qu + Tab  →  quit
 *   se + Tab  →  set
 *   fi + Tab  →  find
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 4: Search Fallback
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
 *   ESC → type what you're looking for → Enter
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
 * LESSON 5: quit and quit!
 *
 * > quit     — Quit with a save prompt if there are unsaved changes.
 * > quit!    — Quit immediately, no save check. Use with caution!
 *
 * 1. Make an edit (type something on the >>> line below).
 * 2. Press ESC, type: quit
 * 3. Press Enter.
 *    EXPECT: "Unsaved changes. Save before quitting? (y/n/ESC)"
 * 4. Press ESC to cancel and stay in the editor.
 *
 * Now try quit! (but be careful — it really quits):
 * 1. Press ESC, type: quit!
 * 2. Press Enter.
 *    EXPECT: Editor exits immediately, no prompt.
 *
 * Keyboard shortcut equivalent: Ctrl+Q (quit with save check)
 * ===========================================================================
 */

>>> Type something here then try quitting:


/* ===========================================================================
 * LESSON 6: undo and redo
 *
 * 1. Type some text on the >>> line below.
 * 2. Press ESC, type: undo
 * 3. Press Enter.
 *    EXPECT: Your typed text disappears.
 *
 * 4. Press ESC, type: redo
 * 5. Press Enter.
 *    EXPECT: Your text reappears.
 *
 * Keyboard shortcuts (much faster for these):
 *   Ctrl+Z = undo
 *   Ctrl+Y = redo
 * ===========================================================================
 */

>>> Type here, then undo/redo:


/* ===========================================================================
 * LESSON 7: theme
 *
 * Cycle through themes or jump to a specific one:
 *
 * 1. Press ESC, type: theme
 * 2. Press Enter.
 *    EXPECT: Theme cycles to the next one. Status shows the name.
 *
 * 3. Press ESC, type: theme tokyo night
 * 4. Press Enter.
 *    EXPECT: Theme switches directly to Tokyo Night.
 *
 * 5. Press ESC, type: theme cyberpunk
 * 6. Press Enter.
 *    EXPECT: Back to Cyberpunk.
 *
 * Available themes:
 *   cyberpunk, nightwatch, daywatch, tokyo night,
 *   akira, tokyo cyberpunk, clarity
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 8: set — Changing Editor Settings
 *
 * The "set" command changes editor settings on the fly.
 *
 * TAB WIDTH:
 * 1. Press ESC, type: set tabstop 4
 * 2. Press Enter.
 *    EXPECT: All tabs now display as 4 spaces wide instead of 8.
 *
 * 3. Press ESC, type: set tabstop 8
 * 4. Press Enter.
 *    EXPECT: Back to 8-wide tabs.
 *
 * COLUMN RULER:
 * 1. Press ESC, type: set ruler 80
 * 2. Press Enter.
 *    EXPECT: A subtle vertical line appears at column 80.
 *
 * 3. Press ESC, type: set ruler 0
 * 4. Press Enter.
 *    EXPECT: Ruler disappears.
 *
 * You can see the ruler on this long line:
 * ===========================================================================
 */

>>> This is a long line for testing the column ruler. It goes past 80 columns so you can see where the ruler appears. Keep going... and going... and going all the way past the ruler position.


/* ===========================================================================
 * LESSON 9: wrap and numbers
 *
 * Toggle display features:
 *
 * WORD WRAP:
 * 1. Press ESC, type: wrap
 * 2. Press Enter.
 *    EXPECT: "Word wrap: on" — long lines wrap at word boundaries.
 *    Look at the long line above — it should wrap now.
 *
 * 3. Press ESC, type: wrap
 * 4. Press Enter.
 *    EXPECT: "Word wrap: off" — back to horizontal scrolling.
 *
 * LINE NUMBERS:
 * 1. Press ESC, type: numbers
 * 2. Press Enter.
 *    EXPECT: Line numbers disappear from the gutter.
 *
 * 3. Press ESC, type: numbers
 * 4. Press Enter.
 *    EXPECT: Line numbers reappear.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 10: find and replace
 *
 * FIND:
 * 1. Press ESC, type: find TODO
 * 2. Press Enter.
 *    EXPECT: Search opens with "TODO" pre-filled. Matches highlight.
 *    Use Left/Right arrows to navigate between matches.
 *    Press Enter to accept or ESC to cancel.
 *
 * REPLACE:
 * 1. Press ESC, type: replace
 * 2. Press Enter.
 *    EXPECT: Find-and-replace prompt opens.
 *    Enter the search term, then the replacement.
 *    Confirm each: (y)es (n)o (a)ll (ESC)cancel
 *
 * Keyboard shortcuts:
 *   Ctrl+F = find
 *   Ctrl+H = find and replace
 * ===========================================================================
 */

/* TODO: this is a search target */
/* TODO: another search target */
/* TODO: third search target */
int todo_count = 3; /* TODO: update this count */


/* ===========================================================================
 * LESSON 11: help
 *
 * 1. Press ESC, type: help
 * 2. Press Enter.
 *    EXPECT: The help screen loads with all keybindings and commands.
 *    Scroll through it to see the full reference.
 *
 * 3. Press ESC to return to this file.
 *
 * Keyboard shortcut: F1
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 12: suspend
 *
 * 1. Press ESC, type: suspend
 * 2. Press Enter.
 *    EXPECT: The editor disappears and you see your shell prompt.
 *
 * 3. Type: fg
 *    EXPECT: The editor reappears exactly as you left it.
 *
 * This is useful for running a quick shell command without quitting.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 13: The New Keyboard Shortcuts
 *
 * All shortcuts have moved from Alt to Ctrl. Try each one:
 *
 * FILE:
 *   Ctrl+S          Save
 *   Ctrl+Q          Quit (prompts if unsaved)
 *
 * UNDO/REDO:
 *   Ctrl+Z          Undo  (was Ctrl+U)
 *   Ctrl+Y          Redo  (was Ctrl+R)
 *
 * CLIPBOARD:
 *   Ctrl+C          Copy (selection, or whole line if nothing selected)
 *   Ctrl+X          Cut  (selection, or whole line if nothing selected)
 *   Ctrl+V          Paste
 *   Ctrl+D          Duplicate line
 *
 * EDITING:
 *   Ctrl+/          Toggle comment
 *   Ctrl+]          Jump to matching bracket
 *   Ctrl+H          Find and replace
 *
 * NAVIGATION:
 *   Ctrl+F          Find
 *   Ctrl+G          Go to line
 *   Ctrl+A          Start of line
 *   Ctrl+E          End of line
 *
 * DISPLAY:
 *   F1              Help screen
 *
 * VS CODE CONVENTION:
 *   With NO selection, Ctrl+C copies the whole line.
 *   With NO selection, Ctrl+X cuts the whole line.
 *   Try it on the >>> line below:
 *
 * 1. Place cursor on the >>> line (don't select anything).
 * 2. Press Ctrl+C.
 *    EXPECT: "Copied N bytes" — the entire line was copied.
 * 3. Move to the blank line below it.
 * 4. Press Ctrl+V.
 *    EXPECT: The line is pasted.
 * ===========================================================================
 */

>>> Copy this entire line with Ctrl+C (no selection needed)


/* ===========================================================================
 * LESSON 14: Ctrl+C Cancels Prompts
 *
 * When you're in a prompt (search, save-as, command, etc.),
 * Ctrl+C acts as Cancel — just like ESC.
 *
 * 1. Press Ctrl+F to open search.
 * 2. Type something.
 * 3. Press Ctrl+C.
 *    EXPECT: Search cancels, cursor returns to original position.
 *
 * This works in ALL prompts: search, save-as, goto line, commands.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 15: Command + Shortcut Equivalence
 *
 * Every keyboard shortcut has a command equivalent. Use whichever
 * feels natural:
 *
 *   Ctrl+S        =  > save
 *   Ctrl+Q        =  > quit
 *   Ctrl+Z        =  > undo
 *   Ctrl+Y        =  > redo
 *   Ctrl+F        =  > find
 *   Ctrl+H        =  > replace
 *   F1            =  > help
 *
 * Commands without shortcuts (use the prompt):
 *   > quit!          Force quit
 *   > save as        Save with new name
 *   > suspend        Suspend to shell
 *   > theme          Cycle or set theme
 *   > set tabstop N  Change tab width
 *   > set ruler N    Set column ruler
 *   > wrap           Toggle word wrap
 *   > numbers        Toggle line numbers
 *
 * The shortcuts are fast-paths for common operations.
 * The command prompt handles everything else.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 16: Putting It All Together
 *
 * Try this workflow:
 *
 * 1. ESC → theme tokyo night     (switch theme)
 * 2. ESC → set ruler 80          (show column guide)
 * 3. ESC → set tabstop 4         (narrower tabs)
 * 4. Ctrl+G → 1                  (go to line 1)
 * 5. Ctrl+F → LESSON             (search for "LESSON")
 *    Use Right arrow to jump between lessons.
 *    Press ESC to stop searching.
 * 6. ESC → wrap                  (turn on word wrap)
 * 7. ESC → wrap                  (turn it off)
 * 8. ESC → theme cyberpunk       (switch back)
 * 9. ESC → set ruler 0           (remove ruler)
 * 10. ESC → set tabstop 8        (restore tabs)
 * 11. Ctrl+S                     (save)
 *
 * You just used the command system and keyboard shortcuts
 * together seamlessly. That's the power of the unified model.
 * ===========================================================================
 */


/* ===========================================================================
 * LESSON 17: Quick Reference
 *
 * OPENING THE COMMAND PROMPT:
 *   ESC              Context-aware (clears selection first if active)
 *   Ctrl+Space       Always opens the prompt
 *
 * COMMANDS:
 *   save [filename]  Save (optionally to a new file)
 *   save as <file>   Save with a new filename
 *   quit             Quit (prompts if unsaved)
 *   quit!            Quit immediately
 *   undo             Undo last change
 *   redo             Redo last undone change
 *   help             Show help screen
 *   suspend          Suspend to shell (fg to resume)
 *   theme [name]     Cycle or set theme
 *   set tabstop N    Set tab display width (1-32)
 *   set ruler N      Set column ruler (0=off)
 *   set wrap         Toggle word wrap (same as "wrap")
 *   set numbers      Toggle line numbers (same as "numbers")
 *   find [text]      Open search (optional pre-fill)
 *   replace          Open find and replace
 *   wrap             Toggle word wrap
 *   numbers          Toggle line numbers
 *
 * SEARCH FALLBACK:
 *   Any text that doesn't match a command becomes a search.
 *   ESC → printf → Enter  =  searches for "printf"
 *
 * TAB COMPLETION:
 *   Type a partial command name, press Tab to complete.
 *
 * ===========================================================================
 *
 *    Congratulations! You've learned the entire command system.
 *
 *    To discard changes:  git checkout test_commands.c
 *    To quit:             Ctrl+Q or ESC → quit
 *
 * ===========================================================================
 */
