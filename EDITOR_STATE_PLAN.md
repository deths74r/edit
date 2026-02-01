# Editor State Tracking & Input System Plan

## Current Architecture

**State**: Monolithic `struct editor_state` (line 254) — ~25 fields mixing cursor, viewport, document, terminal, mmap, and UI state. Plus a separate global `struct mouse_input` (line 241).

**Input**: Synchronous byte-at-a-time `read()` in `terminal_read_key()` (line 512) → monolithic `switch` in `editor_process_keypress()` (line 2462). Modes are implicit in the call stack — `editor_prompt()` creates a nested event loop for search/goto/save.

**Loop**: `while(1) { check_resize → refresh_screen → read_key → process }` — blocking, single-threaded, no event queue.

This works well for a ~2600-line editor. But there are some real structural limitations worth discussing:

---

## 1. State Tracking Alternatives

### A. Domain-Split State with Change Flags

Instead of one flat struct, split into logical domains:

```c
struct cursor_state {
    int x, y;
    int render_x;
    uint32_t flags;          /* CURSOR_MOVED, CURSOR_JUMPED, etc. */
    struct timeval last_move; /* timestamp of last movement */
    int velocity_x, velocity_y; /* cells/sec, computed from recent moves */
};

struct viewport_state {
    int row_offset, column_offset;
    int screen_rows, screen_columns;
    uint32_t flags;          /* VIEWPORT_SCROLLED, VIEWPORT_RESIZED */
};

struct document_state {
    int line_count;
    struct line *lines;
    int dirty;
    char *filename;
    struct editor_syntax *syntax;
    /* mmap fields */
};
```

The `flags` field is the key insight — every subsystem sets dirty bits when it mutates state, and the renderer only recomputes what changed. Right now `editor_refresh_screen()` redraws everything every frame. With change flags, you could skip redrawing the gutter if only the cursor moved horizontally, skip syntax re-highlight if only the viewport scrolled, etc.

The `last_move` timestamp and `velocity` fields enable something you already do for scroll speed (the `last_scroll_time` / `scroll_speed` fields at lines 289-292) — but generalized to all movement. Cursor acceleration, inertial scrolling, predictive line warming all fall out of velocity tracking.

### B. Event-Sourced State

Every mutation becomes an event appended to a log:

```c
enum event_type {
    EVENT_CHAR_INSERT,
    EVENT_CHAR_DELETE,
    EVENT_NEWLINE_INSERT,
    EVENT_NEWLINE_DELETE,
    EVENT_CURSOR_MOVE,
    EVENT_VIEWPORT_SCROLL,
    EVENT_FILE_OPEN,
    EVENT_FILE_SAVE,
};

struct editor_event {
    enum event_type type;
    struct timeval timestamp;
    union {
        struct { int x, y; uint32_t codepoint; } insert;
        struct { int x, y; } delete;
        struct { int from_x, from_y, to_x, to_y; } move;
        /* ... */
    };
};
```

Current state is derived by replaying the log (or maintained incrementally alongside it). This gives you:

- **Undo/redo for free** — walk backward/forward through the event log
- **Macro recording** — just save a slice of the event log and replay it
- **Analytics** — "you spend 40% of your time scrolling through this function"
- **Collaborative editing groundwork** — events can be serialized and sent to another instance

The cost is memory for the log, but a ring buffer caps it. You already store `dirty` as a boolean — this replaces it with a richer signal (dirty = event log has unsaved mutations).

### C. Ring Buffer State Snapshots

Keep the last N snapshots of "hot" state at regular intervals:

```c
#define STATE_RING_SIZE 64

struct state_snapshot {
    int cursor_x, cursor_y;
    int row_offset, column_offset;
    struct timeval timestamp;
};

struct state_ring {
    struct state_snapshot entries[STATE_RING_SIZE];
    int head;
};
```

This is lighter than full event sourcing but still enables:
- **Smooth interpolated scrolling** — render between two snapshots
- **"Where was I?"** — jump back to where cursor was N seconds ago
- **Input velocity computation** — diff recent snapshots to get movement speed
- **Predictive line warming** — if cursor has been moving down at 20 lines/sec, pre-warm lines 20-40 below viewport

---

## 2. Input System Alternatives

### A. epoll Event Loop with Unified Event Queue

This is probably the single highest-impact change. Replace the blocking `read()` with `epoll` (Linux) or `poll` (POSIX):

```c
struct input_event {
    enum {
        INPUT_KEY,
        INPUT_MOUSE,
        INPUT_RESIZE,
        INPUT_TIMER,
        INPUT_FILE_CHANGED,  /* inotify */
    } type;
    struct timeval timestamp;
    union {
        int key;
        struct mouse_input mouse;
        struct { int rows, cols; } resize;
        int timer_id;
    };
};

struct event_queue {
    struct input_event events[256];
    int head, tail;
};
```

The main loop becomes:

```c
while (1) {
    int n = epoll_wait(epoll_fd, epoll_events, MAX_EVENTS, next_timer_ms);
    for (int i = 0; i < n; i++) {
        if (epoll_events[i].data.fd == STDIN_FILENO)
            drain_stdin_into_queue(&queue);
        else if (epoll_events[i].data.fd == signal_fd)
            drain_signals_into_queue(&queue);
        else if (epoll_events[i].data.fd == inotify_fd)
            drain_inotify_into_queue(&queue);
    }
    process_timers(&queue);
    while (!queue_empty(&queue)) {
        struct input_event ev = queue_pop(&queue);
        editor_process_event(&ev);
    }
    editor_refresh_screen();
}
```

What this unlocks:
- **Timers** — cursor blink, status message auto-clear (currently you check `time()` on every refresh at line 2420), smooth scroll animation, auto-save
- **File watching** — inotify detects external changes, prompt to reload
- **Non-blocking I/O** — never block on read, so the editor stays responsive during paste (terminal can dump thousands of bytes at once)
- **Batched input** — if the user pastes 500 characters, you can process them all before a single redraw instead of redrawing 500 times
- **Future extensibility** — subprocess output (compile errors, LSP), socket input, etc.

The biggest win is **batched input**. Right now, pasting 1000 characters means 1000 cycles of `read_key → process → refresh_screen`. With an event queue, you drain all available stdin bytes, parse them into events, process them all, then redraw once.

### B. Keymap Stack (Replaces Nested Loops)

Your current mode system uses nested function calls — `editor_find()` calls `editor_prompt()` which has its own `while(1)` loop calling `terminal_read_key()`. This means the main loop's resize handling, timer processing, etc. are suspended during prompts.

A keymap stack makes modes explicit and composable:

```c
typedef void (*key_handler_fn)(int key, void *context);

struct keymap_entry {
    int key;
    key_handler_fn handler;
};

struct keymap {
    struct keymap_entry *entries;
    int count;
    key_handler_fn default_handler; /* for unmatched keys */
    void *context;                  /* mode-specific state */
};

struct keymap_stack {
    struct keymap *maps[8];
    int depth;
};
```

Normal editing pushes the base keymap. Search mode pushes a search keymap on top. Every keypress is dispatched to the top keymap first. `ESC` pops the current keymap, returning to the previous mode. The main loop *always runs* — no nested event loops:

```c
while (1) {
    drain_events(&queue);
    while (!queue_empty(&queue)) {
        struct input_event ev = queue_pop(&queue);
        keymap_dispatch(&keymap_stack, ev.key, ev);
    }
    editor_refresh_screen();
}
```

This fixes a real bug-prone area: right now if you're in `editor_prompt()` and a `SIGWINCH` fires, `resize_pending` gets set but won't be processed until you exit the prompt and return to the main loop. With a keymap stack, resize is always handled because the main loop always runs.

### C. Input Grammar / Sequence Matcher

For vim-style composable commands, you'd want a grammar-based system instead of a flat switch:

```c
/* A key sequence like "d2w" parses as: operator=delete, count=2, motion=word */
struct input_sequence {
    int operator;    /* d, c, y, etc. */
    int count;       /* numeric prefix */
    int motion;      /* w, b, e, $, etc. */
};
```

Even without going full vim, a sequence matcher enables things like:
- **Multi-key shortcuts** — `Ctrl+K Ctrl+C` to comment (VS Code style)
- **Prefix arguments** — `5↓` to move 5 lines down
- **Chords** — detect two keys pressed within 30ms as a simultaneous chord

The implementation uses a trie of key sequences with timeout:

```c
struct key_node {
    int key;
    key_handler_fn action;       /* NULL if this is an intermediate node */
    struct key_node *children;
    int child_count;
    int timeout_ms;              /* how long to wait for next key */
};
```

When a key arrives, walk the trie. If you reach a leaf, fire the action. If you reach an internal node, start a timer — if the next key arrives before timeout and matches a child, continue. If not, fire the partial match (if any) or fall through.

### D. Dedicated Input Thread with Lock-Free Ring Buffer

Separate input reading from input processing:

```c
/* Reader thread */
void *input_reader_thread(void *arg)
{
    struct lock_free_ring *ring = arg;
    while (1) {
        char buf[4096];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n > 0)
            ring_push_bytes(ring, buf, n);
    }
}
```

The main thread consumes from the ring buffer and parses escape sequences. This means:
- Input is never dropped during expensive operations (large file save, full re-highlight)
- Paste detection is trivial — if the ring has >N bytes available, it's a paste
- You get precise inter-key timing even when the main thread is busy rendering

The lock-free ring buffer is a well-understood primitive (single producer, single consumer needs only atomics, no mutexes):

```c
struct lock_free_ring {
    char buffer[4096];
    _Atomic uint32_t write_pos;
    _Atomic uint32_t read_pos;
};
```

---

## 3. Novel / Experimental Ideas

### Speculative Pre-Warming Based on Movement Velocity

You already have the temperature system (COLD → WARM → HOT). Combine it with cursor velocity tracking:

```c
void editor_predictive_warm(void)
{
    /* If scrolling down at v lines/sec, warm the next v*LOOKAHEAD lines */
    int velocity = compute_scroll_velocity(); /* from state ring */
    int direction = (velocity > 0) ? 1 : -1;
    int lookahead = abs(velocity) * WARM_LOOKAHEAD_SECONDS;

    int start = editor.row_offset + editor.screen_rows * direction;
    int end = start + lookahead * direction;

    for (int i = start; i != end; i += direction) {
        if (i >= 0 && i < editor.line_count)
            line_ensure_warm(&editor.lines[i]);
    }
}
```

This means fast scrolling through a huge file never hits cold lines — they're warmed ahead of the viewport. The cost is minimal because warming is just decoding mmap bytes into cells, and you're only doing it for lines that are about to become visible anyway.

### Differential Rendering

Instead of rebuilding the entire screen buffer every frame, diff the new frame against the previous one and only emit escape sequences for cells that changed:

```c
struct screen_cell {
    uint32_t codepoint;
    uint16_t syntax;     /* foreground/style */
    uint16_t bg;         /* background */
};

struct screen_buffer {
    struct screen_cell *current;
    struct screen_cell *previous;
    int rows, cols;
};

void screen_flush(struct screen_buffer *screen, struct append_buffer *output)
{
    for (int y = 0; y < screen->rows; y++) {
        for (int x = 0; x < screen->cols; x++) {
            int idx = y * screen->cols + x;
            if (memcmp(&screen->current[idx], &screen->previous[idx],
                       sizeof(struct screen_cell)) != 0) {
                /* emit cursor move + character */
                append_cursor_position(output, y, x);
                append_cell(output, &screen->current[idx]);
            }
        }
    }
    /* Swap buffers */
    struct screen_cell *tmp = screen->previous;
    screen->previous = screen->current;
    screen->current = tmp;
}
```

For most frames (single character typed, cursor moved one position), this reduces output from ~100KB of escape sequences to maybe 50-100 bytes. Over SSH or on slow terminals, this is a massive latency improvement.

### Transactional State Updates

Wrap state mutations in transactions that can be committed or rolled back:

```c
struct state_transaction {
    struct cursor_state saved_cursor;
    struct viewport_state saved_viewport;
    int active;
};

void transaction_begin(struct state_transaction *tx)
{
    tx->saved_cursor = editor.cursor;
    tx->saved_viewport = editor.viewport;
    tx->active = 1;
}

void transaction_rollback(struct state_transaction *tx)
{
    if (tx->active) {
        editor.cursor = tx->saved_cursor;
        editor.viewport = tx->saved_viewport;
        tx->active = 0;
    }
}

void transaction_commit(struct state_transaction *tx)
{
    tx->active = 0;
}
```

You're already doing this manually in `editor_find()` (lines 1808-1826) — saving cursor/viewport, restoring on ESC. Transactions formalize the pattern and make it reusable for any speculative operation.

---

## Recommended Priority

If you want maximum impact for the codebase as it stands:

1. **epoll event loop + event queue** — Fixes the nested-loop mode problem, enables timers/file watching, makes paste fast. This is foundational; everything else builds on it.

2. **Keymap stack** — Replaces implicit call-stack modes with explicit, composable keymaps. Eliminates the class of bugs where signals/resize are missed during prompts.

3. **Domain-split state with change flags + differential rendering** — These pair naturally. Split state tells you *what* changed; differential rendering uses that to minimize terminal output.

4. **Predictive line warming** — Low-hanging fruit given your existing temperature system. Just needs velocity tracking (a few timestamps in a ring buffer).

5. **Event sourcing** — Higher implementation cost, but gives you undo/redo and macro recording. Worth it if you plan to add either feature.

The input thread and grammar-based parsing are powerful but add complexity (threading, timeout management) that may not be justified yet. They become valuable when you start adding multi-key sequences or plugin-like extensibility.
