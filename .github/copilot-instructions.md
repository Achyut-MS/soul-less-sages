# Copilot Instructions — Zero-Dep Markdown Viewer

## Build & Test Commands

### Quick Start
```bash
# Build modular executable
cd src-c && make && ./mdview ./notes.md

# Windows
cd src-c; mingw32-make; .\mdview.exe .\notes.md
```

### Common Build Targets
```bash
# Build default executable
make

# Single-translation-unit build (for +5 bonus, runs alongside modular build)
make single

# Run full test suite
make test

# AddressSanitizer build (ASan + UBSan, then runs tests)
make asan

# Code coverage analysis
make coverage

# Round-trip fuzzer (default: 300s budget)
make fuzz
DURATION=600 make fuzz  # Custom duration

# CommonMark conformance runner
make commonmark
./run_conformance
```

### Running Individual Tests
```bash
# Build specific test executable
make test_parser
./test_parser

make test_html_serializer
./test_html_serializer

make test_platform
./test_platform

make test_file_writer
./test_file_writer
```

### Integration Tests
```bash
# Linux
bash ./tests/integration_tests.sh

# Windows
powershell -ExecutionPolicy Bypass -File ./tests/integration_tests.ps1
```

---

## High-Level Architecture

The system is a **single-threaded, blocking-I/O C23 server** that runs locally (127.0.0.1) and spawns a native desktop browser window. It implements bidirectional Markdown↔HTML sync with caret-annotated compiler-style error reporting.

### Data Flow
```
Browser (vanilla HTML/CSS/JS)
    ↓ (textarea → POST /render)
    ↓ (contenteditable → POST /serialize)
    ↓
Raw Socket HTTP Server (C)
    ↓
┌───────────────────────────────────────┐
│ Markdown → HTML (recursive descent)   │
│ HTML → Markdown (scoped tag walker)   │
│ Tokenizer (line/col tracking)         │
│ Error reporting (caret + snippet)     │
│ Debounced atomic file writes          │
└───────────────────────────────────────┘
```

### Key Components
| Component | Language | Purpose |
|---|---|---|
| `platform.h/c` | C | Cross-platform abstraction: POSIX sockets (Linux) + Winsock2 (Windows), atomic file I/O, desktop window spawn |
| `http.c` | C | Raw socket HTTP/1.1 server, request parser, routing (GET /, POST /render, POST /serialize) |
| `md_parser.c` | C | Recursive-descent Markdown → HTML, grammar scope v1 (no tables/footnotes) |
| `tokenizer.c` | C | Feeds md_parser; tracks line/col for every token |
| `error_report.c` | C | Caret-annotated error formatting (`line N, col M: message`) |
| `html_serializer.c` | C | Reverse transform: HTML → Markdown (scoped to tags our parser emits only) |
| `file_writer.c` | C | Debounced (150–300ms), atomic writes via temp+rename / temp+FlushFileBuffers |
| `static/client.js` | JS | Vanilla JS: debounced fetch, contenteditable sync, no framework |
| `tests/test_*.c` | C | Hand-rolled test harness (no external framework) |
| `tests/commonmark/run_conformance.c` | C | Runs official spec.json subset; reports pass ratio |
| `tests/fuzz_roundtrip.c` | C | Grammar-aware fuzzer; proves fixed point on round trip |

---

## Key Conventions

### Zero Dependencies Discipline
- **No third-party libraries**, only C standard library + POSIX/Win32 OS APIs
- Missing stdlib functions (e.g., `getline`) are hand-rolled
- `#include <stdio.h>`, `<stdlib.h>`, `<string.h>`, `<sys/socket.h>`, `<winsock2.h>` only
- Valgrind/ASan clean on full test suite; memory leaks are build-blocking bugs

### Parser & Tokenizer Pattern
Every token carries `line` and `col` metadata:
```c
typedef struct {
    token_type_t type;
    const char  *start;
    size_t       len;
    size_t       line;  // 1-indexed
    size_t       col;   // 1-indexed, within line
} token_t;
```

Parser functions for each grammar non-terminal:
```c
ast_node_t *parse_heading(tokenizer_t *t);
ast_node_t *parse_list(tokenizer_t *t);
ast_node_t *parse_inline(tokenizer_t *t);
```

Errors always include caret + context, never silent failures:
```
error: unterminated code fence (expected ```)
  --> notes.md:14:3
   |
14 | ```python
   |    ^^^^^^ fence opened here, never closed
```

### HTML Serializer: Scoped Tag Vocabulary
Only 9 tag types are supported (by design):
- `<h1>` – `<h6>` → `# ` + level
- `<strong>`, `<em>` → `**text**`, `*text*`
- `<ul>`, `<ol>`, `<li>` → `- item`, `N. item` (renumbered sequentially)
- `<pre><code>`, `<code>` → ` ```lang\n...\n``` `, `` `inline` ``
- `<blockquote>` → `> text`
- `<a>` → `[text](url)`
- `<p>` → text + blank line
- `<br>` → two-space line break

Unknown tags are stripped; malformed HTML fragments return `400` with descriptive error, **never partial output**.

### List Renumbering (Documented Lossy Transform)
Ordered lists are **always renumbered 1, 2, 3, ...** on HTML→Markdown, regardless of source numbers. This is intentional, called out in README as a known limitation, and the fuzzer treats it as idempotent/expected (not a bug). Original numbering is not preserved.

### Overlapping Inline Delimiters
`**a *b** c*` is a **hard parse error**, not silently resolved. Defined at design time to avoid CommonMark-style precedence complexity. Parser emits caret-annotated error and recovers to next blank line.

### Debounced Atomic Writes
File changes from `/serialize` endpoint are:
1. Debounced 150–300ms (batches rapid edits)
2. Atomic: write to `.tmp`, `fsync`/`FlushFileBuffers`, then `rename()`
3. Never corrupts the original file on crash/disk-full/permission error

### HTTP/1.1 & Concurrency Model
- **Single-threaded blocking `accept()` loop** — no async/threading complexity
- Each client connection: parse request → route → respond → close
- `Connection: close` (no keep-alive)
- Graceful shutdown: signal handlers close listener socket, triggering blocked `accept()` to return and run cleanup

### Test Patterns
Custom test harness with assertion macros (no external framework):
```c
#define ASSERT_EQ(a, b) do { if ((a) != (b)) { fprintf(stderr, "FAIL: %s:%d\n", __FILE__, __LINE__); return 1; } } while(0)
#define ASSERT_STR_EQ(a, b) do { if (strcmp(a, b) != 0) { ... } } while(0)
```

Tests are organized by component:
- `test_parser.c` — Markdown parsing edge cases
- `test_html_serializer.c` — HTML → Markdown round-trip
- `test_platform.c` — Socket lifecycle, browser fallback, atomic writes
- `test_file_writer.c` — Debounce collapse, atomicity under crash simulation
- `fuzz_roundtrip.c` — Proves `render(html_to_md(md_to_html(x)))` == `render(md_to_html(x))`
- `commonmark/run_conformance.c` — Reports X/Y spec tests passing (excludes out-of-scope categories)

### Cross-Platform Abstraction
All OS-specific code is isolated in `platform.h/c`:
```c
// Caller never sees OS-specific details
int platform_accept(platform_socket_t listener, platform_socket_t *client);
int platform_read(platform_socket_t sock, void *buf, size_t len);
int platform_write(platform_socket_t sock, const void *data, size_t len);
void platform_open_browser(const char *url);  // xdg-open vs ShellExecuteA
```

On Windows (MinGW/MSVC), link with `-lws2_32 -lshell32`.

### Compiler Flags
```
-std=c23       # (auto-fallback to -std=c2x for GCC < 14)
-Wall -Wextra -Werror
-O2            # (or -O0 for coverage, -g for asan)
```

### Build Artifacts & Cleanup
- `make clean` removes: `.o`, `.gcda`, `.gcno`, all binaries
- Single-file target (`mdview_single`) is generated by `mdview_single.c` (a stub that `#include`s all `.c` files)
- Coverage reports go to `.gcov` files + `lcov` (optional `lcov.info`)

---

## Known Scope Boundaries (v1)

### In Scope
- Headings, lists (ul/ol), code fences, blockquotes, links, inline bold/italic/code
- Bidirectional sync (source ↔ preview)
- Compiler-style line/col error reporting
- Debounced atomic disk writes

### Out of Scope (v1)
- Tables, footnotes, setext headings, raw HTML blocks
- Deep nesting (>3 levels)
- WebSocket (gated stretch goal — only if core finishes by hour 64)
- Unicode normalization (UTF-8 pass-through only)
- General HTML→Markdown (only our renderer's tag set)

---

## CI/Inspection Tools

### Static Analysis
```bash
cppcheck src-c/
clang --analyze src-c/*.c
```

### Debugging
```bash
gdb ./mdview
lldb ./mdview
valgrind --leak-check=full ./test_parser
```

### Coverage
```bash
make coverage
# Outputs line coverage % for each module
```

---

## Quick Reference: What to Edit for Common Tasks

| Task | File(s) |
|---|---|
| Add Markdown syntax support (e.g., strikethrough) | `tokenizer.c`, `md_parser.c`, `html_serializer.c` + add test in `tests/test_parser.c` |
| Fix parser bug | `md_parser.c` + check `tokenizer.c` for token tracking, `error_report.c` for error message |
| Support new HTML tag in reverse | `html_serializer.c` + test in `tests/test_html_serializer.c` |
| Change HTTP routing or response format | `http.c` + test in `tests/integration_tests.sh` / `.ps1` |
| Fix cross-platform issue | `platform.h` / `platform.c` + test in `tests/test_platform.c` |
| Modify file write behavior | `file_writer.c` + test in `tests/test_file_writer.c` |
| Adjust UI or real-time sync logic | `static/client.js` |
| Add build variant | `src-c/Makefile` or root `Makefile` |

---

## Pitch for Judges

This is a **pure C23 desktop Markdown viewer** with **zero third-party dependencies**, running on **Linux & Windows** from a **single C executable** (~1MB). The bidirectional-sync claim is **proven with a round-trip fuzzer** that asserts the correctness fixed point; errors are **compiler-grade caret-annotated diagnostics**. The HTTP layer is hand-rolled sockets, the parser is recursive-descent with line/col tracking, and all memory is Valgrind/ASan clean.
