# Repository Guidelines

## Project Structure & Module Organization
- `edit.c` holds the entire editor implementation (single-file C23 program).
- `lib/gstr/` contains the header-only Unicode/grapheme library used by the editor, with its own tests in `lib/gstr/test/`.
- `test_utf8.txt` is a manual fixture for verifying Unicode rendering and grapheme behavior.
- Build artifacts (`edit`, `*.o`) live in the repo root after running `make`.

## Build, Test, and Development Commands
- `make` builds the debug binary `./edit` with symbols.
- `make release` builds an optimized binary (`-O2`).
- `make lint` scans `edit.c` for stray control characters.
- `make clean` removes `edit` and object files.
- `make -C lib/gstr test` builds and runs the gstr unit tests.

## Coding Style & Naming Conventions
- C23 only; compile with `-std=c23`.
- Indentation uses tabs only (8-column width). No spaces for indenting.
- Brace style: functions open on a new line; control structures on the same line.
- Names are full words: `snake_case` for variables/structs/enums, `SCREAMING_SNAKE_CASE` for constants/macros, and `module_verb_object` for functions.
- Do not typedef structs/enums; always use `struct name` / `enum name`.
- Avoid abbreviations and single-letter variables (except loop counters).
- Comments are required above functions, structs, macros, and non-obvious logic.

## Testing Guidelines
- Core editor has no automated tests; rely on manual verification with `./edit` and `test_utf8.txt`.
- gstr coverage lives in `lib/gstr/test/test_gstr.c` and runs via `make -C lib/gstr test`.
- Test naming follows `test_*.c` in `lib/gstr/test/`.

## Commit & Pull Request Guidelines
- Use short, imperative commit subjects (e.g., "Fix terminal resize cursor jump").
- Keep commits focused on one change or bugfix.
- PRs should include a concise summary and the exact commands run (e.g., `make`, `make -C lib/gstr test`).
- If behavior changes, describe user-visible impact and any manual test steps.

## Notes for Contributors
- Preserve existing section banners in `edit.c` (e.g., `/*** Terminal ***/`).
- Use named constants instead of magic numbers or hardcoded escape sequences.
