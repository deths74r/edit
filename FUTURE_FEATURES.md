# Future Features: 8-Directional Cell Awareness

This document captures ideas for features enabled by cells having awareness of their neighbors in all directions (N, S, E, W, NE, SE, SW, NW).

## Core Concept

Currently cells only see left/right (horizontal neighbors within a line). Extending to 8-directional awareness enables 2D pattern recognition.

```
[NW] [N] [NE]
[W]  [X] [E]
[SW] [S] [SE]
```

## Implementation Approaches

1. **Query-time lookup**: Look up adjacent lines on-demand during render
2. **Column alignment index**: Data structure mapping columns to aligned content across lines
3. **Structure detection**: Detect multi-line structures (tables, blocks) as units

---

## Feature Ideas

### 1. Table Auto-Formatting with Pattern Continuation

When typing in a table, the editor detects the pattern above and auto-aligns:
```
| Header 1 | Header 2 |
|----------|----------|
| Cell 1   |          <- auto-expands to match column width
```

### 2. Table Box Drawing (UTF-8)

Render tables with Unicode box characters:
```
| A | B |          ┌───┬───┐
|---|---|    →     │ A │ B │
| C | D |          ├───┼───┤
                   │ C │ D │
                   └───┴───┘
```

### 3. Indent Guides / Scope Lines

Vertical lines showing indentation depth:
```
if (condition) {      │
    if (nested) {     │ │
        code();       │ │
    }                 │ │
}                     │
```

### 4. Bracket Pair Visualization

Visual lines connecting matched braces:
```
function foo() {  ─┐
    bar();         │
}  ───────────────┘
```

### 5. ASCII Art to Unicode Auto-Conversion

Detect box patterns and convert:
```
+---+          ┌───┐
|   |    →     │   │
+---+          └───┘
```

Rules:
- `+` with `-` E/W and `|` N/S → `┼`
- `+` with `-` E and `|` S → `┌`
- etc.

### 6. List Continuation Lines

Visual indicator showing list item continuation:
```
- First item with a very    │
  long wrapped line         │
- Second item
```

### 7. Diff/Change Visualization

Side-by-side awareness for diff views:
```
  old line
+ new line  │
- deleted   │
  unchanged
```

### 8. Code Alignment Detection

Detect aligned tokens across lines for columnar editing:
```
int x      = 1;     ← aligned =
int longer = 2;     ← aligned =
int y      = 3;     ← aligned =
```

### 9. Multi-line String/Comment Detection

Each line knows it's inside a multi-line context:
```
const s = `          ← start
  multi              ← inside
  line               ← inside
`;                   ← end
```

### 10. Flow Diagrams

Flowchart elements understanding connections:
```
  Start
    │
    ▼
 [Process] ──→ [Next]
    │
    ▼
   End
```

### 11. Smart Rectangle Selection

Cells detect rectangular regions across lines for block selection.

### 12. Rainbow Scope Lines

Color-coded vertical lines matching bracket colors for nested scopes.

---

## Priority / Dependencies

| Feature | Depends On | Complexity |
|---------|------------|------------|
| Table auto-format | Column alignment index | Medium |
| Table box drawing | Table auto-format | Medium |
| Indent guides | Column alignment index | Low |
| Bracket visualization | Existing pair matching | Medium |
| ASCII→Unicode | Pattern detection | High |
| Pattern continuation | Table auto-format | Medium |

## Notes

- Consider lazy computation - only compute 2D awareness for visible lines
- Cache invalidation is critical - changes to one line affect neighbors
- May want structure-level detection (tables, code blocks) rather than per-cell
