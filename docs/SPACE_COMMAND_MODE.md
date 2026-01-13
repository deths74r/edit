# Space Hold-to-Activate Command Mode

Space can be used as a modifier key to enter command mode, similar to how Spacemacs uses space as a leader key. This feature uses **release order detection** to distinguish between fast typing and intentional commands.

## Usage

| Action | Result |
|--------|--------|
| Tap space quickly | Inserts a space character |
| Hold space + tap a key | Executes that key as a command |
| Hold space, release without pressing anything | Enters command mode |

## How It Works

The challenge with using space as a modifier is that fast typists often overlap keypresses - they hit the next key before fully releasing the previous one. This creates a conflict:

```
Fast typing:     space↓ → w↓ → space↑ → w↑   (typing "w" after space)
Command:         space↓ → g↓ → g↑ → space↑   (executing "g" command)
```

Both patterns look similar (space down, key down while space held), but the **release order** is different:

- **Fast typing**: Space releases FIRST (you're moving to the next key)
- **Command**: The other key releases FIRST (you tapped it while holding space)

### Detection Algorithm

1. When space is pressed, wait up to 100ms for the next event
2. If space releases quickly with no other key → insert space (normal tap)
3. If another key arrives while space is held:
   - Wait up to 300ms to see what releases first
   - If **space releases first** → fast typing: insert space + the letter
   - If **anything else happens** → it's a command: execute it

## Alternative Entry Methods

If you prefer not to use space-hold, you can also enter command mode with:

- **Ctrl+Space** - Traditional leader key
- **Shift+Space** - Alternative leader key  
- **Ctrl+Enter** - Another alternative

These work regardless of the Kitty keyboard protocol.

## Requirements

Space hold-to-activate requires the **Kitty keyboard protocol** to detect key press and release events. This is automatically enabled in compatible terminals:

- Kitty
- WezTerm
- Ghostty
- Foot
- Other terminals supporting the protocol

In terminals without the protocol, use Ctrl+Space or Shift+Space instead.

## Technical Details

The implementation uses Kitty keyboard protocol flags:
- Flag 1: Disambiguate escape codes
- Flag 2: Report key press/release events
- Flag 8: Report all keys as escape sequences

Combined flag value: `11` (sent as `ESC[>11u`)

### Key Event Format

The protocol sends events in CSI u format:
```
CSI codepoint ; modifiers:event-type u
```

Where event-type is:
- 1 = key press
- 2 = key repeat  
- 3 = key release

### Source Files

- `src/edit.c` - Space hold detection logic (lines ~7729-7800)
- `src/input.c` - Kitty protocol parsing, KEY_SPACE_PRESS/RELEASE handling
- `src/types.h` - Key code definitions, protocol constants
- `src/command.c` - Command mode state management
