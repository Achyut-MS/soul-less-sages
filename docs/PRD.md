# Product Requirements Document — Zero-Dep Markdown Viewer

> Zero Dependency Hackathon · Track B — Parsers & Data Formats  
> Aug 28–31 2026 · 72h · stdlib-only

---

## 1. Product Overview

### 1.1 What It Is
A cross-platform native desktop Markdown editor for **Windows and Linux** with **bidirectional live sync**. Launching the executable immediately initializes the local engine and displays the dual-pane desktop editor view. The left pane is a raw Markdown source editor; the right pane is a live-rendered HTML preview. The preview pane is **contenteditable** — edits made directly in the rendered HTML are serialized back to Markdown and written to the source file on disk.

### 1.2 Why It Exists
Most Markdown tools are one-way (source → preview) and desktop editors typically weigh hundreds of megabytes (Electron/Node). This project demonstrates that a lightweight, zero-dependency C23 desktop app can deliver fast, bidirectional live editing, compiler-grade caret error diagnostics, and provable correctness across Windows and Linux without a single third-party library or cloud API.

### 1.3 Target User
- Hackathon judges (primary)
- Developers on Windows and Linux who want a fast, zero-dependency Markdown desktop editor
- Anyone who prefers editing rendered text over raw syntax

---

## 2. Goals & Non-Goals

### 2.1 Goals (Must Have)
| ID | Goal | Priority |
|---|---|---|
| G1 | Parse Markdown source to valid HTML with hand-rolled recursive-descent parser | P0 |
| G2 | Serialize rendered HTML back to Markdown with scoped DOM-tag walker | P0 |
| G3 | Bidirectional sync: source edits update preview; preview edits update source | P0 |
| G4 | Compiler-style, caret-annotated line/col error reporting for malformed Markdown | P0 |
| G5 | Cross-platform desktop support: native execution on both Windows (Win32/Winsock) and Linux (POSIX) | P0 |
| G6 | Atomic, debounced file writes to the `.md` file on disk (`fsync` on Linux, `FlushFileBuffers` on Windows) | P0 |
| G7 | Test suite covering edge cases: nested blocks, overlapping inline, escapes | P0 |
| G8 | Report CommonMark spec conformance ratio (N/M applicable tests) as a verifiable correctness metric | P1 |
| G9 | Round-trip fixed-point fuzzer proving `render(html_to_md(md_to_html(x)))` converges after one pass | P1 |
| G10 | Sanitizer (ASan/UBSan) build clean across the fuzz corpus, in addition to Valgrind | P1 |
| G11 | gcov/lcov line coverage percentage reported alongside raw test count | P2 |
| G12 | Produce all required submission artifacts: `deps-proof.txt`, `.zero-dep.toml`, 5-minute demo video | P0 |

### 2.2 Non-Goals (Out of Scope for v1)
| ID | Non-Goal | Rationale |
|---|---|---|
| NG1 | Tables (`| col |`) | Complex grammar; 72h budget |
| NG2 | Footnotes (`[^1]`) | Rarely used; defer |
| NG3 | WebSocket transport | HTTP POST is imperceptible on localhost; promoted to a **gated stretch goal** — only attempted if Phase 5 completes by hour 64 |
| NG4 | General-purpose HTML→MD | Scoped to our renderer's tag vocabulary only |
| NG5 | Syntax highlighting in code blocks | Requires external lexer or massive regex |
| NG6 | Multiple file tabs | UI complexity; single-file focus protects scope |
| NG7 | TLS/HTTPS | Localhost-only; out of scope |
| NG8 | Preserving original ordered-list numbering across a round trip | Deliberately renumbered sequentially instead — see resolved open question 4 |
| NG9 | CommonMark-style delimiter precedence resolution | Overlapping inline delimiters are a defined parse error, not silently resolved — see resolved open question 6 |

---

## 3. User Stories

### US-1: Edit Source, See Preview
> As a user, I type Markdown in the left pane and immediately see the rendered HTML in the right pane, so I can verify formatting without switching contexts.

**Acceptance Criteria:**
- Typing `# Heading` renders as `<h1>Heading</h1>` within 300ms
- Typing `**bold**` renders as `<strong>bold</strong>`
- The preview updates without full page refresh

### US-2: Edit Preview, Update Source
> As a user, I click the preview pane and edit rendered text directly, so I don't need to remember Markdown syntax.

**Acceptance Criteria:**
- Clicking `<h1>Title</h1>` and changing it to `<h1>New Title</h1>` updates the source to `# New Title`
- Making text bold in the preview (e.g., via Ctrl+B or manual `<strong>`) updates source with `**text**`
- The source textarea updates within 300ms of stopping typing
- Ordered lists are renumbered sequentially on every serialize (documented, expected behavior — not a bug)

### US-3: Error Feedback
> As a user, when I write malformed Markdown, I see a clear, caret-annotated error message with line and column numbers, so I can fix it quickly.

**Acceptance Criteria:**
- `**unclosed bold` shows a caret-annotated message pointing at the opening `**`
- `` ```code without closing fence `` shows a caret-annotated message pointing at the fence line
- `**a *b** c*` shows a defined "overlapping inline delimiters" error, not silent misparse
- Errors display in a non-intrusive banner, not as alerts

### US-4: Safe File Writes
> As a user, my file is never corrupted even if the server crashes mid-write, so I don't lose work.

**Acceptance Criteria:**
- All writes use atomic `rename()` pattern
- A crash during write leaves the original file intact
- Rapid edits are debounced to a single write

### US-5: Zero Dependencies
> As a judge, I can verify that the project uses only the C standard library and browser built-ins, so the "zero dependency" claim holds.

**Acceptance Criteria:**
- No `package.json`, `Cargo.toml`, `requirements.txt`, or `go.mod` with external deps
- No CDN links in HTML
- Makefile only uses `gcc` and standard tools

### US-6: Verifiable Correctness (New)
> As a judge, I can rerun a conformance suite and a fuzzer myself rather than trusting a claimed test count.

**Acceptance Criteria:**
- `make commonmark` runs the parser against `tests/commonmark/spec.json` and prints an N/M pass ratio
- `make fuzz` runs the round-trip fixed-point fuzzer for a configurable duration and reports failures (if any) with the minimal reproducing input
- `make asan` and `make coverage` both build and run cleanly

---

## 4. Functional Requirements

### 4.1 Markdown Grammar Support

| Element | Syntax | HTML Output | Serializer |
|---|---|---|---|
| Heading 1 | `# text` | `<h1>text</h1>` | Yes |
| Heading 2–6 | `## text` … `###### text` | `<h2>`…`<h6>` | Yes |
| Bold | `**text**` | `<strong>text</strong>` | Yes |
| Italic | `*text*` | `<em>text</em>` | Yes |
| Bold+Italic | `***text***` | `<strong><em>text</em></strong>` | Yes |
| Unordered list | `- item` or `* item` | `<ul><li>item</li></ul>` | Yes |
| Ordered list | `1. item` | `<ol><li>item</li></ol>` | Yes (renumbered sequentially on serialize) |
| Code fence | `\`\`\`lang\ncode\n\`\`\`` | `<pre><code class="lang">code</code></pre>` | Yes |
| Inline code | `` `code` `` | `<code>code</code>` | Yes |
| Blockquote | `> text` | `<blockquote><p>text</p></blockquote>` | Yes |
| Link | `[text](url)` | `<a href="url">text</a>` | Yes |
| Paragraph | `text` | `<p>text</p>` | Yes |
| Line break | `text  \n` or `\n` | `<br>` or new `<p>` | Yes |
| Escaped char | `\*` | literal `*` | Yes |
| Overlapping inline (`**a *b** c*`) | — | — | N/A — hard parse error |

### 4.2 Parser Error Messages

All errors must include:
- **Line number** (1-indexed)
- **Column number** (1-indexed, in characters)
- **Descriptive message**
- **Caret-annotated context snippet — required, not optional**

Example (canonical format):
```
error: unterminated code fence (expected ```)
  --> notes.md:14:3
   |
14 | ```python
   |    ^^^^^^ fence opened here, never closed
```

Other required error cases:
```
line 7, col 12: unmatched '**' — no closing delimiter found
line 22, col 1: invalid heading level (max 6, got 8)
line 5, col 8: expected ']' after link text, found '('
line 10, col 1: list item indentation inconsistent with previous item
line 3, col 9: overlapping inline delimiters — '*' closed before matching '**' opened at col 3
```

### 4.3 HTTP API

#### `POST /render`
Request:
```json
{
  "md": "# Hello\n\nThis is **bold**."
}
```

Response (200 OK):
```html
<h1>Hello</h1>
<p>This is <strong>bold</strong>.</p>
```

Error (400 Bad Request):
```json
{
  "error": "line 3, col 5: unmatched '**'",
  "snippet": "  3 | some **bold text\n      |      ^^ opened here"
}
```

#### `POST /serialize`
Request:
```json
{
  "html": "<h1>Hello</h1><p>World</p>"
}
```

Response (200 OK):
```json
{
  "md": "# Hello\n\nWorld\n"
}
```

Error (400 Bad Request) — malformed/unparseable fragment:
```json
{
  "error": "could not parse preview content as a known tag structure"
}
```

### 4.4 File Writer Behavior

- **Debounce interval**: 150–300ms (configurable at compile time)
- **Write strategy**: atomic rename
- **Temp file naming**: `.{filename}.tmp` in same directory
- **fsync**: call `fsync()` on temp file before rename (if POSIX available)
- **Concurrency**: single-threaded writes; queue or drop overlapping requests

---

## 5. Non-Functional Requirements

### 5.1 Performance
- **Parse time**: < 10ms for a 1000-line document on modern hardware
- **Render round-trip**: < 50ms localhost (source → preview)
- **Serialize round-trip**: < 50ms localhost (preview → source)
- **Memory**: no unbounded growth; free all allocations per request

### 5.2 Correctness
- **Round-trip fidelity**: MD → HTML → MD should produce semantically equivalent Markdown (whitespace and list numbering may differ — list renumbering is a documented, deliberate exception, not a fidelity bug)
- **Round-trip convergence**: a second round trip must be a no-op relative to the first (fixed point) — proven by the fuzzer in G9, not just asserted
- **HTML validity**: all emitted HTML is valid (closed tags, escaped entities)
- **No silent failures**: all parser errors are reported, never swallowed
- **Spec alignment**: CommonMark conformance ratio reported and reproducible (G8)

### 5.3 Security
- **XSS prevention**: HTML-escape all text content in renderer
- **No arbitrary file access**: static file serving restricted to `static/` directory
- **No shell injection**: file paths sanitized, no `system()` calls
- **Memory safety**: Valgrind clean AND ASan/UBSan clean across the fuzz corpus (G10)

### 5.4 Portability
- **Target OS**: Linux (primary), macOS (secondary)
- **Compiler**: GCC or Clang with C23 support
- **No POSIX extensions** beyond sockets and file operations

---

## 6. UI/UX Requirements

### 6.1 Layout
```
┌─────────────────────────────────────────────────────────────┐
│  Zero-Dep Markdown Viewer                    [status: sync] │
├──────────────────────────┬──────────────────────────────────┤
│  # Hello                 │  Hello                           │
│                          │  ─────────────────────────────── │
│  This is **bold**.       │  This is bold.                   │
│                          │                                  │
│  - item 1                │  • item 1                        │
│  - item 2                │  • item 2                        │
│                          │                                  │
│  [textarea]              │  [contenteditable div]           │
│  monospace font          │  proportional font, styled       │
│  line numbers (optional) │  hover: subtle border            │
├──────────────────────────┴──────────────────────────────────┤
│  line 14, col 3: unterminated code fence          [ dismiss ] │
└─────────────────────────────────────────────────────────────┘
```

### 6.2 Interaction Details
- **Tab key**: inserts 2 spaces in textarea (not browser focus change)
- **Preview click**: places cursor in approximate position (best effort)
- **Error banner**: auto-dismisses on next successful parse; manual dismiss button; renders the caret-annotated snippet in monospace
- **Status indicator**: small dot (green = synced, yellow = pending, red = error)
- **Scroll sync** (stretch): scrolling source scrolls preview proportionally

### 6.3 Visual Design
- **Color scheme**: light mode default, clean minimal aesthetic
- **Fonts**: monospace for source (system default), sans-serif for preview
- **No external assets**: all CSS inline or in `static/styles.css`

---

## 7. Test Requirements

### 7.1 Unit Tests
| Component | Min Tests | Key Cases |
|---|---|---|
| Tokenizer | 10 | empty, newlines, escapes, line/col tracking |
| MD Parser | 20 | all block types, nested inline, overlapping (error case), errors |
| HTML Serializer | 15 | all tag types, nesting, escaping, round-trip, list renumbering, malformed fragment |
| HTTP Layer | 5 | routing, static files, malformed requests |
| File Writer | 3 | debounce, atomicity, error handling |

### 7.2 Integration Tests
| Scenario | Expected Result |
|---|---|
| Type `# Hi` in source | Preview shows `<h1>Hi</h1>` within 300ms |
| Edit preview h1 to `<h1>Bye</h1>` | Source updates to `# Bye` within 300ms |
| Type `**x` in source | Caret-annotated error banner shows `line 1, col 1: unmatched '**'` |
| Rapid typing 50 chars/sec | File written ≤ 3 times (debounced) |
| Kill server mid-write | Original `.md` file unchanged |
| Round trip twice | Second round trip is a no-op relative to the first |

### 7.3 Correctness Harness (New)
| Tool | Target | Reported As |
|---|---|---|
| CommonMark conformance runner | `tests/commonmark/spec.json`, applicable subset | N/M pass ratio in STDLIB.md + README |
| Round-trip fixed-point fuzzer | `tests/fuzz_roundtrip.c`, 5+ min default budget | Pass/fail + minimal repro on failure |
| ASan/UBSan build | Full test suite + fuzz corpus | Clean / findings count |
| gcov/lcov | Full test run | Line coverage % |

### 7.4 Edge Case Fixtures
- `fixtures/empty.md` — empty file
- `fixtures/only_whitespace.md` — spaces and newlines only
- `fixtures/nested_blockquote_list.md` — blockquote containing a list
- `fixtures/overlapping_inline.md` — `**a *b** c*` (defined error case)
- `fixtures/escaped_chars.md` — `\*`, `\[`, `\``
- `fixtures/code_fence_no_close.md` — unterminated fence
- `fixtures/adjacent_code.md` — `` `a` `b` ``
- `fixtures/deep_nesting.md` — list → blockquote → list → paragraph
- `fixtures/list_renumber.md` — `3. / 5. / 5.` → confirms sequential renumber on serialize

---

## 8. Innovation & Scoring

### 8.1 Package Killer Narrative (STDLIB.md)
The following "instead of X, we hand-rolled Y" entries are targeted:

| Standard Approach | Our Implementation | STDLIB.md Weight |
|---|---|---|
| `express` / `net/http` | Raw POSIX socket HTTP/1.1 server | High |
| `marked` / `markdown-it` | Recursive-descent MD parser with line/col tracking, CommonMark-conformance-tested | High |
| `turndown` / `html-to-markdown` | Scoped DOM-tag walker (only our tags) | High |
| `cJSON` / `jsmn` / `encoding/json` | Minimal JSON body extraction | Medium |
| AFL / libFuzzer setups | Hand-rolled round-trip fixed-point fuzzer | Medium |
| `ws` / `socket.io` | (Gated bonus) Hand-rolled RFC 6455 WebSocket | Medium |

**STDLIB Log bonus (+3) requires ≥10 real, non-trivial substitutions**, each with a one-line rationale — the table above lists 6 headline entries; smaller substitutions (manual JSON string escaping, ANSI-free error output, `system()` ban, manual UTF-8 pass-through, custom hash map for tag lookups if used, hand-rolled base64 if needed anywhere) should be enumerated too to comfortably clear 10.

**Disclosure note**: `tests/commonmark/spec.json` is an external test **corpus** (data, not code), sourced from the official CommonMark project rather than written this weekend. Per the event's "no vendoring without disclosure" rule, this must be explicitly called out in `STDLIB.md` as a third-party test fixture, distinct from the hand-written conformance *runner* that consumes it.

### 8.2 Innovation Points
- **Bidirectional sync** is rare in Markdown tools; most are source→preview only
- **Provable convergence**: the round-trip fixed-point fuzzer proves the bidirectional claim rather than just demoing it
- **Error reporting with line/col and caret annotation** is uncommon in lightweight parsers
- **Zero dependencies** makes the entire stack auditable in < 30 minutes

---

## 9. Open Questions — RESOLVED

1. ~~Language confirmation: C or Go?~~ **Resolved: C (C23), confirmed.** No C++ mixed in — keeps the memory-management and Package Killer narrative pure-C.
2. ~~JSON parsing strategy?~~ **Resolved: hand-rolled minimal key-value extraction** (`"md":`/`"html":`), not a full parser — matches the low-risk mitigation in SKILLS.md.
3. ~~Blockquote nesting depth limit?~~ **Resolved: 3 levels.** Beyond that, parser emits a "nesting too deep" error rather than silently truncating.
4. ~~List numbering on serialize: preserve or renumber?~~ **Resolved: always renumber sequentially.** Documented as a deliberate lossy transform (NG8) so it isn't a surprise against round-trip fidelity goals.
5. ~~Preview cursor position on serialize: map DOM cursor back to textarea line/col?~~ **Resolved: best-effort only** — approximate character-offset mapping, not exact; not a P0 goal.
6. **New — overlapping inline delimiters (`**a *b** c*`)**: **Resolved as a hard, defined parse error** (not CommonMark-style precedence resolution) to keep Phase 3 scope bounded. See ARCHITECTURE.md.

---

## 10. Success Criteria

The project is considered successful if:

- [ ] All P0 goals (G1–G7) are implemented and tested
- [ ] CommonMark conformance ratio is measured and reported (G8)
- [ ] Round-trip fixed-point fuzzer passes with no failures found in its budgeted run (G9)
- [ ] Round-trip sync works for all supported grammar elements
- [ ] Error messages include correct line, column, and a caret-annotated snippet
- [ ] File writes are atomic and debounced
- [ ] No external dependencies (verified by `find . -name "*.c" -o -name "*.h"` only)
- [ ] ≥ 50 unit/integration tests pass, with line coverage % reported (G11)
- [ ] Valgrind reports 0 leaks in steady-state operation
- [ ] ASan/UBSan report 0 findings across the fuzz corpus (G10)
- [ ] `make single` produces a working single-file build
- [ ] STDLIB.md is compelling, factually accurate, and lists ≥10 substitutions (STDLIB Log bonus eligibility)
- [ ] `tests/commonmark/spec.json` explicitly disclosed as an external test corpus in STDLIB.md
- [ ] `deps-proof.txt` present and shows zero third-party runtime deps
- [ ] `.zero-dep.toml` present with track letter `B` and one-line pitch
- [ ] 5-minute demo video recorded showing the tool working and the manifest/deps-proof being empty
- [ ] No project code committed before kickoff (Aug 28, 18:00 UTC) — design/docs before that are fine, code is not
- [ ] Submitted before code freeze (Aug 31, 18:00 UTC)
