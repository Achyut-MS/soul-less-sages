# Skills.md — Zero-Dep Markdown Viewer

> Zero Dependency Hackathon · Track B — Parsers & Data Formats  
> Aug 28–31 2026 · 72h · stdlib-only (C23, no C++)

---

## Core Skills Inventory

### 1. Cross-Platform C Systems Programming (Linux & Windows)
**Level:** Expert  
**Used in:** Cross-platform desktop server, socket engine, memory management, file I/O

- POSIX socket programming (`socket`, `bind`, `listen`, `accept`, `select`) on Linux
- Winsock2 programming (`WSAStartup`, `WSACleanup`, `closesocket`, `ioctlsocket`) on Windows
- Cross-platform atomic file I/O (`fsync`/`rename` on Linux; `FlushFileBuffers`/`MoveFileExA` on Windows)
- Desktop app browser window spawning (`xdg-open` on Linux; `ShellExecuteA` on Windows)
- HTTP/1.1 request/response parsing without external libraries
- Manual memory management (malloc/free, no leaks — Valgrind & ASan clean)
- Signal handling for graceful shutdown (`SIGINT`, `SIGTERM`, `SetConsoleCtrlHandler`)

**Key files:** `platform.h`, `platform.c`, `http.c`, `file_writer.c`, `main.c`

---

### 2. Recursive-Descent Parsing
**Level:** Expert  
**Used in:** Markdown → HTML parser

- Grammar design (EBNF → parser functions)
- Tokenizer architecture with state tracking
- Line and column tracking through token stream
- Lookahead and backtracking strategies
- Error recovery (panic mode vs. single-token insertion)
- Delimiter-stack tracking for inline markers, with defined (not silently resolved) behavior on overlap
- AST construction and traversal
- HTML entity escaping during rendering

**Key files:** `tokenizer.c`, `md_parser.c`

**Critical patterns:**
```c
// Each non-terminal becomes a function
ast_node_t *parse_heading(tokenizer_t *t);
ast_node_t *parse_list(tokenizer_t *t);
ast_node_t *parse_inline(tokenizer_t *t);

// Line/col tracking in every token
token_t tok = tokenizer_next(t);
if (tok.type != TOKEN_TEXT) {
    parser_error(tok.line, tok.col, "expected text, got %s", token_name(tok.type));
}
```

---

### 3. DOM Tree Walking / HTML Serialization
**Level:** Advanced  
**Used in:** HTML → Markdown serializer

- Minimal HTML fragment parsing (browser gives clean DOM)
- Recursive tree traversal
- Tag-to-Markdown mapping
- Context-aware escaping (when to escape `*`, `#`, `>`, `` ` ``)
- Nested structure reconstruction (lists in blockquotes, etc.)
- Deliberate lossy transforms documented explicitly (ordered-list renumbering)
- Malformed-fragment detection and structured error response (no partial/garbled output)

**Key files:** `html_serializer.c`

**Tag vocabulary (scoped):**
| Tag | MD | Notes |
|---|---|---|
| h1–h6 | `#` × level | |
| strong | `**text**` | |
| em | `*text*` | |
| ul/li | `- item` | Indentation for nesting |
| ol/li | `N. item` | Always sequential renumbering, source numbers discarded |
| pre/code | ` ```lang\ncode\n``` ` | Extract class for lang |
| code (inline) | `` `code` `` | |
| blockquote | `> text` | `> >` for nesting, max 3 levels |
| a | `[text](url)` | Escape URL if needed |
| p | text + `\n\n` | |
| br | `  \n` | Two-space line break |

---

### 4. Zero-Dependency Web Development
**Level:** Advanced  
**Used in:** Client-side code

- Vanilla JavaScript (no frameworks, no build tools)
- `fetch()` API for HTTP requests
- `contenteditable` API and mutation observation
- Debouncing patterns with `setTimeout`
- Flexbox/Grid layout without CSS frameworks
- Event delegation and input handling
- JSON serialization/deserialization (native `JSON.parse`/`JSON.stringify`)
- Rendering caret-annotated, monospace error snippets from structured error responses

**Key files:** `static/client.js`, `static/styles.css`, `static/index.html`

---

### 5. Test-Driven Development (C)
**Level:** Advanced  
**Used in:** All parser components

- Custom test harness (no external test frameworks)
- Fixture-based testing (`.md` files in `tests/fixtures/`)
- Assertion macros
- Memory leak detection with Valgrind
- Edge-case enumeration methodology

**Test harness pattern:**
```c
#define ASSERT_EQ(a, b)     do { if ((a) != (b)) {         fprintf(stderr, "FAIL: %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b);         return 1;     } } while(0)

int test_heading_parsing() {
    const char *md = "# Hello";
    ast_node_t *ast = parse(md);
    ASSERT_EQ(ast->type, NODE_HEADING);
    ASSERT_EQ(ast->heading.level, 1);
    ASSERT_STR_EQ(ast->heading.text, "Hello");
    free_ast(ast);
    return 0;
}
```

---

### 6. Correctness Harness: Conformance & Fuzzing (New)
**Level:** Intermediate–Advanced  
**Used in:** `tests/commonmark/`, `tests/fuzz_roundtrip.c`

- Loading and running a subset of the official CommonMark `spec.json` test corpus against `md_to_html()`, with an explicit exclusion list for out-of-scope grammar (tables, footnotes, setext headings, raw HTML blocks)
- Hand-rolled fuzzing without libFuzzer/AFL: byte-mutation and grammar-aware random Markdown generation
- Proving a round-trip **fixed point**: `render(html_to_md(md_to_html(x)))` must equal `render(md_to_html(x))` after one full cycle — the concrete evidence behind the bidirectional-sync claim
- Minimal-input reduction on fuzzer failure (shrink to smallest reproducing case)
- Reading and reporting `gcov`/`lcov` line coverage as a percentage, not just a raw test count

**Key files:** `tests/commonmark/run_conformance.c`, `tests/fuzz_roundtrip.c`

---

### 7. Build Systems & Tooling
**Level:** Intermediate  
**Used in:** Project scaffolding

- GNU Make for C projects
- Compiler flags: `-Wall -Wextra -Werror -std=c23 -O2`
- Sanitizer builds: `-fsanitize=address,undefined` (separate `make asan` target)
- Coverage builds: `--coverage` + `gcov`/`lcov` (separate `make coverage` target)
- Single-translation-unit build for the Single File bonus (`make single` — concatenates all `.c`/`.h` and compiles as one file, alongside the normal modular build)
- Static analysis: `cppcheck`, `clang --analyze`
- Debugging: `gdb`, `lldb`
- Memory debugging: `valgrind --leak-check=full`
- Git workflow for 72h hackathon pace

---

## Skill Gaps & Mitigation

| Gap | Risk | Mitigation |
|---|---|---|
| Hand-rolled HTTP parsing | Medium | Reference RFC 2616; keep scope to HTTP/1.1 only; budget 10–12h not 8h |
| JSON parsing in C | Low | Use minimal string extraction; no full parser needed |
| contenteditable quirks | Medium | Test across Chrome/Firefox; keep DOM simple |
| Unicode handling in C | Low | UTF-8 pass-through; no normalization needed for v1 |
| Signal-safe file cleanup | Low | Use `sigaction` with SA_RESTART; minimal handler |
| Overlapping inline delimiter ambiguity | Low (resolved) | Defined as hard parse error at design time, not discovered mid-Phase-3 |
| CommonMark conformance runner scope creep | Medium | Explicit exclusion list agreed before Phase 3 starts; report ratio, don't chase 100% |
| Fuzzer eating core parser time | Medium | Time-boxed to Phase 6 with a default 5-min run budget, longer run optional/background |

---

## Learning Resources (Pre-Hackathon)

### Parsing
- "Crafting Interpreters" by Bob Nystrom (free online) — Chapters 4-6 on scanning + parsing
- Recursive descent tutorial: https://eli.thegreenplace.net/2010/01/02/top-down-operator-precedence-parsing/
- CommonMark spec + test suite: https://spec.commonmark.org/

### C Network Programming
- Beej's Guide to Network Programming: https://beej.us/guide/bgnet/
- HTTP/1.1 RFC 2616 (Section 5: Request, Section 6: Response)

### contenteditable
- MDN: `contenteditable` — https://developer.mozilla.org/en-US/docs/Web/HTML/Global_attributes/contenteditable
- MDN: `MutationObserver` — https://developer.mozilla.org/en-US/docs/Web/API/MutationObserver

### Fuzzing & Sanitizers
- libFuzzer docs (for technique reference only — not linked as a dependency): https://llvm.org/docs/LibFuzzer.html
- AddressSanitizer / UndefinedBehaviorSanitizer: https://github.com/google/sanitizers

---

## Skill Checklist (Self-Assessment Before Hackathon)

- [ ] Can write a recursive-descent parser for a simple expression grammar
- [ ] Can implement `socket()` → `bind()` → `listen()` → `accept()` loop in C
- [ ] Can parse HTTP request-line and headers from raw bytes
- [ ] Can build a minimal test harness in C with macros
- [ ] Can use Valgrind and achieve 0 leaks on a small program
- [ ] Can build with ASan/UBSan and interpret its output
- [ ] Can write a simple grammar-aware fuzzer and reduce a failing case to a minimal repro
- [ ] Can run gcov/lcov and read a coverage report
- [ ] Can implement debounced `fetch()` in vanilla JS
- [ ] Can handle `contenteditable` input events and extract `innerHTML`
- [ ] Can explain why this project uses C (not Go, not C++) in 30 seconds
