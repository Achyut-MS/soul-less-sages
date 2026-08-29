# Architecture — Zero-Dep Markdown Viewer

> Zero Dependency Hackathon · Track B · Aug 28–31 2026

---

## System Overview

The system is a single-language (C23), cross-platform desktop application for **Linux and Windows** consisting of:

1. **Cross-Platform OS Abstraction (`platform.h`)** — unified sockets, atomic file persistence, and automatic desktop window spawning
2. **HTTP / Socket Engine** — raw sockets (POSIX sockets on Linux, Winsock2 on Windows), hand-rolled HTTP/1.1 parser
3. **Markdown Parser** — recursive-descent, line/col error tracking, caret-annotated errors
4. **HTML Serializer** — scoped DOM-tag walker (only tags our renderer emits)
5. **File Writer** — debounced, atomic disk writes (`fsync` / `FlushFileBuffers`)
6. **Local Desktop Client** — vanilla HTML/CSS/JS, zero frameworks, auto-spawned locally
7. **Correctness Harness** — CommonMark conformance runner + round-trip fixed-point fuzzer + gcov coverage

---

## Component Diagram

```
┌────────────────────────────────────────────────────────────────────────────┐
│                              BROWSER                                        │
│  ┌─────────────────────┐                    ┌─────────────────────────────┐ │
│  │   Source Editor     │                    │    Live Preview Pane        │ │
│  │   <textarea>        │                    │    <div contenteditable>    │ │
│  │                     │                    │                             │ │
│  │  User types MD ─────┼──── POST /render ──┼────→ Server renders HTML   │ │
│  │                     │    {md: "..."}     │         ↓                   │ │
│  │                     │                    │    HTML injected into DOM  │ │
│  │                     │                    │                             │ │
│  │  Source updated ←───┼──── POST /serialize┼←─── User edits preview      │ │
│  │  (rewrite .md)     │    {html: "..."}   │         ↑                   │ │
│  │                     │                    │    DOM → HTML string        │ │
│  └─────────────────────┘                    └─────────────────────────────┘ │
│                              Debounced 150–300ms                            │
└────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌────────────────────────────────────────────────────────────────────────────┐
│                              SERVER (C)                                     │
│                                                                             │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────┐  │
│  │   HTTP Layer │    │   MD Parser  │    │  HTML Serializer │  │  Writer  │  │
│  │  (raw sockets│    │ (recursive   │    │  (scoped DOM    │  │(debounced│  │
│  │   + parser)  │    │   descent)   │    │   tag walker)  │  │ + atomic)│  │
│  └──────┬───────┘    └──────┬───────┘    └──────┬───────┘    └────┬─────┘  │
│         │                   │                    │                 │        │
│         └───────────────────┴────────────────────┴─────────────────┘        │
│                                                                             │
│  Endpoints:                                                                 │
│    GET  /              → serve static/index.html                           │
│    GET  /static/*      → serve css/js files                                │
│    POST /render        → {md} → md_to_html() → HTML                        │
│    POST /serialize     → {html} → html_to_md() → MD → write to disk        │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

---

## Cross-Platform Desktop & OS Abstraction (`platform.h`)

To deliver a true native desktop application on both **Linux** and **Windows** with **zero third-party dependencies** (no Electron, no Tauri, no external windowing packages), the project introduces a clean, lightweight abstraction layer:

```
┌────────────────────────────────────────────────────────────────────────────┐
│                    Cross-Platform Abstraction (platform.h)                 │
├─────────────────────────────────────┬──────────────────────────────────────┤
│               LINUX                 │               WINDOWS                │
├─────────────────────────────────────┼──────────────────────────────────────┤
│ • POSIX Sockets (<sys/socket.h>)    │ • Winsock2 (<winsock2.h>, ws2_32)    │
│ • Atomic rename() + fsync()         │ • MoveFileExA() + FlushFileBuffers() │
│ • Non-blocking fcntl(O_NONBLOCK)    │ • ioctlsocket(FIONBIO)               │
│ • Desktop launch: xdg-open          │ • Desktop launch: ShellExecuteA()    │
│ • Build: gcc / clang (GNU make)     │ • Build: MinGW / MSVC cl.exe         │
└─────────────────────────────────────┴──────────────────────────────────────┘
```

### Desktop Application Auto-Spawn Flow
When the user executes `./mdview ./notes.md` (Linux) or `.\mdview.exe .\notes.md` (Windows):
1. The C engine binds to `127.0.0.1` on a free/default port (e.g. `8080`).
2. The engine immediately invokes `platform_open_browser("http://127.0.0.1:8080")`:
   - On Linux: Spawns the desktop browser view via `fork()` / `execvp("xdg-open", ...)` or platform default.
   - On Windows: Invokes native `ShellExecuteA(NULL, "open", "http://127.0.0.1:8080", NULL, NULL, SW_SHOWNORMAL)`.
3. The editor opens as a seamless, local desktop window interface.

---

## HTTP Layer (Raw Sockets)

### Why Raw Sockets?
- **STDLIB.md entry**: "Implemented HTTP/1.1 request parsing on raw POSIX/Winsock sockets instead of using `express`/`cpp-httplib`"
- **Craft points**: Manual `socket()`, `bind()`, `listen()`, `accept()`, `read()`, `write()`, with `WSAStartup`/`WSACleanup` on Windows.
- **Scope**: HTTP/1.1, keep-alive optional, no TLS, no HTTP/2

### Time Budget Warning
This layer has no stdlib fallback in C — it must be built from scratch. Budgeted at **10–12 hours**, not 8, based on the risk register. If it threatens to exceed hour 12, cut keep-alive support first, then multi-request pipelining, before touching anything in Phase 2+.

### Request Parser (Hand-Rolled)
```
Raw bytes → tokenize lines → parse request-line → parse headers → route

Request-Line: METHOD SP REQUEST-URI SP HTTP-VERSION CRLF
Headers:      field-name: field-value CRLF
Body:         read Content-Length bytes
```

### Response Builder
```c
typedef struct {
    int   status;
    char *content_type;
    char *body;
    size_t body_len;
} http_response_t;

// Serializes to:
// HTTP/1.1 200 OK\r\n
// Content-Type: text/html\r\n
// Content-Length: N\r\n
// \r\n
// <body>
```

---

## Markdown Parser (Recursive Descent)

### Grammar (v1)
```
document    ::= block*
block       ::= heading | list | code_fence | blockquote | paragraph
heading     ::= '#'+' ' inline+ '\n'
list        ::= (ul_item | ol_item)+
ul_item     ::= ('- ' | '* ') inline+ '\n'
ol_item     ::= digit+ '. ' inline+ '\n'
code_fence  ::= '```' [lang]? '\n' text '\n' '```' '\n'
blockquote  ::= '>' ' ' inline+ '\n'
paragraph   ::= inline+ '\n'
inline      ::= bold | italic | code_inline | link | text
bold        ::= '**' inline+ '**'
italic      ::= '*' inline+ '*'
code_inline ::= '`' [^`]+ '`'
link        ::= '[' text ']' '(' url ')'
text        ::= [^*\[`]+  (any char not starting inline syntax)
```

### RESOLVED: Overlapping Inline Delimiters
`**a *b** c*` is a **hard parse error**, not silently resolved by precedence rules. Decision made at design time (not during Phase 3) to avoid CommonMark-style precedence complexity within the 72h budget.

Rule: once an inline delimiter run opens (`**` or `*`), the parser tracks it on a delimiter stack. If a closing delimiter of a *different* type is encountered before the currently-open one closes, the parser emits:
```
line N, col M: overlapping inline delimiters — '*' closed before matching '**' opened at col K
```
and treats the rest of the paragraph as text (panic-mode recovery to the next blank line).

### RESOLVED: List Renumbering on Serialize
Ordered lists are **always renumbered sequentially starting at 1** on `html_to_md()`, regardless of the original source numbers (`3. / 5. / 5.` becomes `1. / 2. / 3.`). This is a deliberate, documented lossy transform — original numbering intent is not preserved across a round trip. Called out explicitly in README known limitations so it isn't a "surprise" against the round-trip fidelity goal (PRD §5.2); the round-trip fixed-point fuzzer treats this renumbering as expected/idempotent behavior, not a bug.

### Tokenizer with Line/Col Tracking
```c
typedef struct {
    const char *src;
    size_t      pos;
    size_t      line;
    size_t      col;
} tokenizer_t;

typedef struct {
    token_type_t type;
    const char  *start;
    size_t       len;
    size_t       line;
    size_t       col;
} token_t;
```

Every token carries `line` and `col` from the source. The parser reports errors compiler-style, with a caret-annotated source snippet — this is a **required** field, not optional:
```
error: unterminated code fence (expected ```)
  --> notes.md:14:3
   |
14 | ```python
   |    ^^^^^^ fence opened here, never closed
```

```
error: unmatched '**' — no closing delimiter found
  --> notes.md:7:12
   |
 7 | This is **bold text that never closes
   |         ^^ opened here
```

```
error: invalid heading level (max 6, got 8)
  --> notes.md:22:1
   |
22 | ######## Too many hashes
   | ^^^^^^^^
```

### Parser State Machine
```
tokenizer_next() → token
                    ↓
            parser_dispatch()
                    ↓
            ┌───────┴───────┐
            ▼               ▼
      block_parser()   inline_parser()
            │               │
            ▼               ▼
      emit HTML node   emit HTML inline node
```

---

## Correctness Harness

### CommonMark Conformance Runner
`tests/commonmark/spec.json` (the official CommonMark test suite) is loaded by a runner that feeds each input through `md_to_html()` and diffs against expected output. Since our grammar is intentionally scoped smaller for the hackathon (excluding footnotes, HTML blocks, etc., though simple GFM tables, blockquotes, and lists are supported), many advanced/nested tests fail. The runner reports the actual conformance ratio (177/652 passed, 27.15%), which is documented in [STDLIB.md](STDLIB.md) and the README.

### Round-Trip Fixed-Point Fuzzer
`tests/fuzz_roundtrip.c` generates randomized (byte-mutation and grammar-aware) Markdown inputs and asserts:
```
render(html_to_md(md_to_html(x))) == render(md_to_html(x))
```
i.e., after one full round trip through both directions, a second round trip is a no-op. This is the concrete proof behind the "bidirectional sync actually converges" claim, not just a demo. Runs for a fixed time budget (default: 5 minutes) as part of `make test`, longer overnight as a background CI-style run if time allows.

### Sanitizer Builds
In addition to the Valgrind-clean requirement (SKILLS.md §5), a second build target compiles with `-fsanitize=address,undefined` and runs the fuzzer + full test suite against it. Zero ASan/UBSan findings across the fuzz corpus is reported alongside the Valgrind result.

### Coverage
`gcov`/`lcov` line coverage percentage is captured from a full test run and reported in the README next to the raw test count (e.g., "62 tests, 94% line coverage") rather than the count alone.

---

## HTML Serializer (Scoped DOM Walker)

### Scope Deliberately Limited
The serializer only needs to understand tags **our own renderer emits**:

| Tag | MD Equivalent |
|---|---|
| `<h1>`–`<h6>` | `#` × level + space |
| `<strong>` | `**text**` |
| `<em>` | `*text*` |
| `<ul>` / `<li>` | `- item` |
| `<ol>` / `<li>` | `1. item` (always renumbered sequentially — see RESOLVED note above) |
| `<pre><code>` | ` ```lang\ncode\n``` ` |
| `<code>` (inline) | `` `code` `` |
| `<blockquote>` | `> text` |
| `<a href="...">` | `[text](url)` |
| `<p>` | text + blank line |
| `<br>` | two spaces + newline (or just newline) |

### Algorithm
```
html_to_md(node):
    if node is text: return escape_special_chars(text)
    if node is <hN>:  return "#"*N + " " + html_to_md(children)
    if node is <strong>: return "**" + html_to_md(children) + "**"
    if node is <em>: return "*" + html_to_md(children) + "*"
    if node is <ul>: return join(map(li_to_md, children), "\n")
    if node is <ol>: return join(map(ol_li_to_md_renumbered, children), "\n")
    if node is <pre><code>: return fence + lang + "\n" + text + "\n" + fence
    if node is <blockquote>: return "> " + html_to_md(children)
    if node is <a>: return "[" + html_to_md(children) + "](" + href + ")"
    if node is <p>: return html_to_md(children) + "\n\n"
    else: return html_to_md(children)  // strip unknown tags
```

### Why Not a Real HTML Parser?
- We control the input HTML (it's our own renderer's output)
- A full HTML5 parser is ~10k lines — out of scope for 72h
- The browser gives us a clean DOM tree via `innerHTML`
- We walk the tree recursively, no need for tag soup handling

### Malformed Reverse-Direction Input
If the contenteditable pane sends HTML that doesn't parse as a clean fragment (e.g., a browser-injected malformed nesting from a paste), `html_to_md()` returns a `400` with a descriptive (non-line/col, since there's no meaningful source position in DOM-derived HTML) error message, and the client leaves the textarea untouched rather than overwriting good source with a partial/garbled result. This path is explicitly covered by `test_html_serializer.c`.

---

## File Writer

### Requirements
- **Debounced**: batch rapid edits (150–300ms idle time before write)
- **Atomic**: write to temp file, then `rename()` — never corrupt the original
- **Thread-safe**: single writer thread / event loop serialization

### Flow
```
serialize request arrives
        ↓
   reset debounce timer
        ↓
   timer fires (no new request in 200ms)
        ↓
   write to /path/.notes.md.tmp
        ↓
   fsync() the temp file
        ↓
   rename(".notes.md.tmp", "notes.md")
        ↓
   respond 200 OK
```

---

## Client (Vanilla JS)

### Design Principle: Dumb Terminal
The browser does zero parsing. It only:
1. Captures `textarea` input → debounced `fetch('/render', {md})`
2. Injects returned HTML into `contenteditable` preview pane
3. Captures `contenteditable` mutations → debounced `fetch('/serialize', {html})`
4. Updates `textarea` with returned MD + writes are server-side

### Sync Strategy
```javascript
// Source → Preview
let renderTimer;
textarea.addEventListener('input', () => {
    clearTimeout(renderTimer);
    renderTimer = setTimeout(() => {
        fetch('/render', {method:'POST', body: JSON.stringify({md: textarea.value})})
            .then(r => r.text())
            .then(html => preview.innerHTML = html);
    }, 200);
});

// Preview → Source
let serializeTimer;
preview.addEventListener('input', () => {
    clearTimeout(serializeTimer);
    serializeTimer = setTimeout(() => {
        fetch('/serialize', {method:'POST', body: JSON.stringify({html: preview.innerHTML})})
            .then(r => r.json())
            .then(({md}) => textarea.value = md);
    }, 200);
});
```

### CSS Layout
```css
.container { display: flex; height: 100vh; }
.source    { flex: 1; resize: none; font-family: monospace; }
.preview   { flex: 1; overflow-y: auto; padding: 1rem; }
```

---

## Data Flow Summary

| Direction | Path | Transformer | Persistence |
|---|---|---|---|
| Source → Preview | `textarea` → POST `/render` → response | `md_to_html()` | None (ephemeral) |
| Preview → Source | `contenteditable` → POST `/serialize` → response | `html_to_md()` | Atomic write to `.md` file |

---

## Error Handling Strategy

| Layer | Error Type | Response |
|---|---|---|
| Tokenizer | Unterminated fence, unmatched `**`, overlapping delimiters | `400 Bad Request` + caret-annotated `line X, col Y: message` |
| Parser | Invalid nesting, empty heading | `400 Bad Request` + caret-annotated `line X, col Y: message` |
| Serializer | Malformed/unparseable HTML fragment from client | `400 Bad Request` + descriptive message; textarea left untouched |
| HTTP | Malformed request | `400` or `404` with minimal body |
| File Writer | Disk full, permission denied | `500 Internal Server Error` |

---

## Performance Notes

- **Raw sockets on localhost**: latency is sub-millisecond; HTTP POST round-trip is negligible
- **Parser**: recursive descent on a single document is O(n) where n = input length
- **Memory**: stream tokens, don't materialize full AST if possible (but small docs → AST is fine)
- **Debouncing**: 150–300ms feels instant; prevents disk thrashing on rapid keystrokes

---

## Build Targets

- `make` — standard modular build (`http.c`, `md_parser.c`, etc. compiled separately)
- `make single` — concatenates all `.c`/`.h` into one translation unit, compiles to `mdview_single` — targets the +5 Single File bonus without abandoning the modular architecture for day-to-day development
- `make asan` — build with `-fsanitize=address,undefined` for the fuzzer/sanitizer harness
- `make coverage` — build with `--coverage`, run test suite, emit `lcov` report

---

## Future Extensions (Post-Hackathon, or Gated Stretch During Event)

1. **WebSocket** — hand-rolled RFC 6455 upgrade + frame parser for true push sync. **Gate: only start this if Phase 5 (Integration & File Writer) is complete by hour 64.** If Phase 1 (HTTP) overran its budget, this is the first thing cut.
2. **Tables** — extend grammar + serializer for `| col | col |` syntax
3. **Footnotes** — `[^1]` + `[^1]: text` support
4. **Syntax highlighting** — server-side `pre` tag class injection, client-side CSS
5. **Multiple files** — sidebar file tree, tabbed editing
