# Hybrid Mode Test File

Press **Alt+M** to toggle hybrid markdown rendering mode.

## Headers (# should hide)

# Header 1
## Header 2
### Header 3
#### Header 4
##### Header 5
###### Header 6

## Text Formatting (delimiters should hide)

This is **bold text** with asterisks.
This is *italic text* with asterisks.
This is ***bold and italic*** together.
This is `inline code` with backticks.
This is ``code with `backticks` inside`` using double backticks.

## Links (URL should show in status bar)

Here is a [simple link](https://example.com) to test.
Here is a [longer link](https://github.com/anthropics/claude-code) with more text.
Here is a [link with spaces](https://example.com/path/to/page) in the URL.

## Images

![Alt text](image.png)
![Logo](https://example.com/logo.png)

## Lists (markers should become bullets)

Unordered with dash:
- Item one
- Item two
- Item three

Unordered with asterisk:
* Item A
* Item B
* Item C

Unordered with plus:
+ First
+ Second
+ Third

Ordered list:
1. First item
2. Second item
3. Third item

## Task Lists (checkboxes should render)

- [ ] Unchecked task
- [x] Checked task
- [ ] Another unchecked
- [X] Also checked (capital X)

Nested tasks:
- [ ] Parent task
  - [ ] Child unchecked
  - [x] Child checked

## Tables (pipes should become box characters)

| Column A | Column B | Column C |
|----------|----------|----------|
| Cell 1   | Cell 2   | Cell 3   |
| Cell 4   | Cell 5   | Cell 6   |

| Left | Center | Right |
|:-----|:------:|------:|
| L1   | C1     | R1    |
| L2   | C2     | R2    |

## Blockquotes

> This is a blockquote.
> It can span multiple lines.

> Blockquote with **bold** and *italic* inside.

## Horizontal Rules (already substituted)

---

***

___

## Code Blocks (should NOT be affected)

```c
int main() {
    printf("Hello, world!\n");
    return 0;
}
```

```markdown
# This markdown inside code block should stay raw
**not bold** *not italic*
```

## Edge Cases

Empty bold: ****
Empty italic: **
Empty code: ``
Unclosed **bold
Unclosed *italic
Unclosed `code

Nested: **bold with *italic* inside**
Adjacent: **bold***italic*

## Testing Instructions

1. Open this file in edit
2. Press Alt+M to enable hybrid mode
3. Verify:
   - Header # symbols hide (move cursor to header to reveal)
   - Bold ** and italic * hide around text
   - Code backticks hide
   - List markers become bullets (-)
   - Task boxes show as checkboxes
   - Table pipes become box drawing characters
   - Links show URL in status bar when cursor is on them
4. Press Alt+M again to return to raw mode
5. All syntax should be visible again
