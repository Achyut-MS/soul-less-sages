# Task Slot / Checklist — Zero-Dep Markdown Viewer

> Zero Dependency Hackathon · Track B · Aug 28–31 2026 · 72h

---

## Legend

- `[ ]` — Not started
- `[~]` — In progress
- `[x]` — Complete
- `[!]` — Blocked

---

## Phase 0: Pre-Hackathon Setup (Aug 27)

> **Hard rule**: no project code before kickoff. Design, docs, sketching, reading stdlib docs, and tuning AI prompts are explicitly fine per the event rules; any `.c`/`.h`/`.js` file committed before **Aug 28, 18:00 UTC** disqualifies the submission. Everything in this phase is docs/design only.

### Documentation & Planning
- [x] Write README.md
- [x] Write ARCHITECTURE.md
- [x] Write PRD.md
- [x] Write TASKS.md (this file)
- [x] Write SKILLS.md
- [x] Final language confirmation — **C (C23) server, no C++; vanilla browser JS client is not a second "dependency language" — it's inherent to being a web client and carries no manifest**
- [ ] Set up dev environment (Docker / VM with gcc, make, valgrind, gcov, ASan/UBSan)
- [ ] Create GitHub repo with `.gitignore` for C, OSI-approved license file
- [ ] Download `tests/commonmark/spec.json` (official CommonMark test suite) — plan to disclose as an external test corpus in STDLIB.md (data, not code, but still requires disclosure)
- [ ] Draft `.zero-dep.toml` stub — track letter `B`, one-line pitch
- [ ] Plan `deps-proof.txt` generation command (e.g., confirm `find . -name "*.c" -o -name "*.h"` plus a note that C has no package manifest by design)
- [ ] Record exact event anchor times: **kickoff Aug 28 18:00 UTC**, **code freeze Aug 31 18:00 UTC** — convert to local team timezones now

### Design
- [ ] Finalize Markdown grammar EBNF
- [ ] Design token struct + tokenizer state machine on paper
- [ ] Design AST node types
- [ ] Design HTML serializer tag mapping table
- [ ] Sketch client UI (paper or Figma — keep it minimal)
- [x] **Resolve overlapping inline delimiter behavior** — decided: hard parse error, delimiter-stack based detection (see ARCHITECTURE.md)
- [x] **Resolve list renumbering on serialize** — decided: always sequential renumbering, documented as lossy (see ARCHITECTURE.md)
- [ ] Design caret-annotated error message formatter (shared by tokenizer + parser + HTTP error responses)

---

## Phase 1: Foundation & Cross-Platform Engine (Hours 0–10, budget padded from 8 — see Risk Register)

### Platform Abstraction & HTTP Server Skeleton
- [ ] `platform.h` / `platform.c` — unified socket init (`WSAStartup` on Win32, no-op on POSIX), non-blocking flags, and atomic file replace
- [ ] Native desktop launcher: `platform_open_browser()` (`xdg-open` on Linux, `ShellExecuteA` on Windows)
- [ ] Cross-platform socket loop: `socket()`, `bind()`, `listen()`, `accept()` loop
- [ ] Request buffer reading (handle partial reads)
- [ ] Request-line parser: `METHOD URI VERSION`
- [ ] Header parser: `Key: Value` (case-insensitive keys)
- [ ] Body reader: respect `Content-Length`
- [ ] Response builder: status line + headers + body
- [ ] Router: `GET /`, `GET /static/*`, `POST /render`, `POST /serialize`
- [ ] Static file serving (read from `static/` directory)
- [ ] Basic error responses (400, 404, 500)
- [ ] **Test (Linux)**: `./mdview ./notes.md` launches desktop UI and responds 200 to `GET /`
- [ ] **Test (Windows)**: `.\mdview.exe .\notes.md` compiles with MinGW/MSVC and opens desktop view
- [ ] **Test**: `curl -X POST /render -d '{"md":"# hi"}'` returns 200
- [ ] **Checkpoint at hour 10**: if HTTP layer isn't done, cut keep-alive support first, then pipelining — do not let this bleed into Phase 2's budget

### Client Skeleton
- [ ] `static/index.html` — flex layout, textarea + contenteditable div
- [ ] `static/styles.css` — basic styling, no frameworks
- [ ] `static/client.js` — `fetch()` wrappers for `/render` and `/serialize`
- [ ] Debounced input handlers (both directions)
- [ ] **Test**: Typing in textarea triggers POST to `/render`
- [ ] **Test**: Editing preview triggers POST to `/serialize`

---

## Phase 2: Tokenizer (Hours 10–18)

### Core Tokenizer
- [ ] `tokenizer_t` struct with `src`, `pos`, `line`, `col`
- [ ] `token_t` struct with `type`, `start`, `len`, `line`, `col`
- [ ] Token types enum: `HASH`, `DASH`, `STAR`, `UNDERSCORE`, `BACKTICK`, `LBRACKET`, `RBRACKET`, `LPAREN`, `RPAREN`, `GT`, `NEWLINE`, `TEXT`, `DIGIT`, `DOT`, `EOF`, `ERROR`
- [ ] `tokenizer_next()` — advances through source, returns next token
- [ ] Line/col tracking: increment line on `\n`, reset col
- [ ] Escape handling: `\*` → literal `*`, etc.
- [ ] **Test**: Tokenize simple paragraph → correct TEXT tokens
- [ ] **Test**: Tokenize heading → HASH + TEXT + NEWLINE
- [ ] **Test**: Tokenize list → DASH + TEXT + NEWLINE (×N)
- [ ] **Test**: Verify line/col accuracy after newlines

### Edge Cases
- [ ] Empty input → single EOF token
- [ ] Multiple consecutive newlines → tracked correctly
- [ ] Mixed whitespace (tabs, spaces) → normalized or preserved
- [ ] **Test fixture**: `tests/fixtures/edge_empty.md`
- [ ] **Test fixture**: `tests/fixtures/edge_newlines.md`

---

## Phase 3: Markdown → HTML Parser (Hours 18–36)

### Block-Level Parsing
- [ ] `parse_document()` → array of block nodes
- [ ] `parse_heading()` — `#` count 1–6, inline content, newline
- [ ] `parse_unordered_list()` — `- ` or `* ` items
- [ ] `parse_ordered_list()` — `N. ` items
- [ ] `parse_code_fence()` — ` ``` ` + optional lang + body + closing ` ``` `
- [ ] `parse_blockquote()` — `> ` + inline content, max 3 levels deep (error beyond)
- [ ] `parse_paragraph()` — fallback: inline content until blank line
- [ ] Blank line handling (separator between blocks)

### Inline Parsing
- [ ] `parse_inline()` — dispatch to inline parsers
- [ ] `parse_bold()` — `**text**`
- [ ] `parse_italic()` — `*text*` (and `_text_` if time)
- [ ] `parse_bold_italic()` — `***text***`
- [ ] `parse_code_inline()` — `` `code` ``
- [ ] `parse_link()` — `[text](url)`
- [ ] `parse_text()` — accumulate until inline delimiter
- [ ] Nested inline: `**bold *and italic***`
- [ ] **Overlapping inline delimiter detection**: `**a *b** c*` → delimiter-stack mismatch → defined error (design already resolved in Phase 0)

### AST → HTML Rendering
- [ ] AST node types: `DOCUMENT`, `HEADING`, `LIST`, `LIST_ITEM`, `CODE_BLOCK`, `BLOCKQUOTE`, `PARAGRAPH`, `BOLD`, `ITALIC`, `CODE_INLINE`, `LINK`, `TEXT`
- [ ] `render_html(ast)` — recursive walk, emit HTML string
- [ ] Proper HTML escaping: `&`, `<`, `>` in text nodes
- [ ] Code fence: `<pre><code class="lang">...</code></pre>`
- [ ] Inline code: `<code>...</code>`

### Error Reporting
- [ ] `parser_error(line, col, message)` → formatted string
- [ ] `render_caret_snippet(src, line, col, len)` → compiler-style caret-annotated context block (shared helper, required for every error path)
- [ ] Unterminated code fence → caret-annotated `line X, col Y: unterminated code fence`
- [ ] Unmatched `**` → caret-annotated `line X, col Y: unmatched '**'`
- [ ] Invalid heading level → caret-annotated `line X, col Y: heading level must be 1-6`
- [ ] Broken link syntax → caret-annotated `line X, col Y: expected ']' after link text`
- [ ] Overlapping inline delimiters → caret-annotated `line X, col Y: overlapping inline delimiters`

### Tests
- [ ] `test_md_parser.c` — basic blocks
- [ ] `test_md_parser.c` — nested inline
- [ ] `test_md_parser.c` — overlapping inline (defined error case)
- [ ] `test_md_parser.c` — empty headings
- [ ] `test_md_parser.c` — adjacent inline code spans
- [ ] `test_md_parser.c` — nested blockquote-in-list
- [ ] `test_md_parser.c` — escaped characters
- [ ] `test_md_parser.c` — real-world fixture: `tests/fixtures/sample.md`

### CommonMark Conformance (New)
- [ ] Write `tests/commonmark/run_conformance.c` — loads `spec.json`, runs applicable subset through `md_to_html()`, diffs output
- [ ] Classify and exclude out-of-scope categories (tables, footnotes, setext headings, raw HTML blocks) with a documented list
- [ ] **Checkpoint**: record initial N/M pass ratio, iterate before end of Phase 3

---

## Phase 4: HTML → Markdown Serializer (Hours 36–48)

### DOM Tree Builder (Minimal)
- [ ] `html_node_t` struct: `tag`, `attrs`, `children`, `text`
- [ ] Parse simple HTML fragment from client (browser gives clean HTML)
- [ ] Handle self-closing tags: `<br>`
- [ ] Handle attributes: `href`, `class`
- [ ] Detect malformed/unparseable fragments → return structured error, do not attempt partial output

### Tag Walker
- [ ] `html_to_md(node)` recursive function
- [ ] `<h1>`–`<h6>` → `#` × level
- [ ] `<strong>` → `**...**`
- [ ] `<em>` → `*...*`
- [ ] `<ul>` / `<li>` → `- item` (nested indentation)
- [ ] `<ol>` / `<li>` → `N. item`, **always sequentially renumbered from 1** regardless of source
- [ ] `<pre><code class="lang">` → ` ```lang\n...\n``` `
- [ ] `<code>` (inline) → `` `...` ``
- [ ] `<blockquote>` → `> ...` (nested `> >`, max 3 levels)
- [ ] `<a href="url">` → `[text](url)`
- [ ] `<p>` → text + `\n\n`
- [ ] `<br>` → `  \n` or `\n`
- [ ] Unknown tags → strip, recurse children

### Escaping
- [ ] Escape `*`, `_`, `` ` ``, `[`, `]` in text when they would trigger MD parsing
- [ ] Escape `>` at line start to avoid accidental blockquote
- [ ] Escape `#` at line start to avoid accidental heading

### Tests
- [ ] `test_html_serializer.c` — round-trip: MD → HTML → MD ≈ original
- [ ] `test_html_serializer.c` — nested list serialization
- [ ] `test_html_serializer.c` — blockquote-in-list
- [ ] `test_html_serializer.c` — code fence with language
- [ ] `test_html_serializer.c` — link with special chars in URL
- [ ] `test_html_serializer.c` — list renumbering (`3./5./5.` → `1./2./3.`)
- [ ] `test_html_serializer.c` — malformed fragment → structured 400, no partial write

---

## Phase 5: Integration & File Writer (Hours 48–56)

### Wire Everything Together
- [ ] `POST /render` → read JSON body → `md_to_html()` → return HTML (or caret-annotated 400)
- [ ] `POST /serialize` → read JSON body → `html_to_md()` → return JSON `{md: "..."}` (or structured 400)
- [ ] JSON parser (minimal): extract `"md"` and `"html"` values (or use simple string split)
- [ ] **Test**: End-to-end: type in textarea → see preview update
- [ ] **Test**: End-to-end: edit preview → see textarea update

### File Writer
- [ ] Debounce timer (200ms) per client connection
- [ ] Atomic write: `fopen(".tmp.md", "w")` → `fwrite()` → `fclose()` → `rename()`
- [ ] Error handling: disk full, permission denied → 500
- [ ] **Test**: Rapid keystrokes → single write, not N writes
- [ ] **Test**: SIGKILL mid-write → original file intact

### Client Polish
- [ ] Sync cursor/scroll position (basic)
- [ ] Visual feedback: loading indicator on sync
- [ ] Error display: red banner for parser errors with line/col **and monospace caret snippet**
- [ ] **Test**: Parser error in source → error banner shows line 14, col 3 with caret snippet rendered correctly

### PHASE 5 CHECKPOINT — Hour 64 Gate
- [ ] **Gate check**: is Phase 5 fully complete? If yes → WebSocket stretch goal (Innovation Bonus section) is unlocked. If no → skip straight to Phase 6, do not start WebSocket work.

---

## Phase 6: Correctness Harness & Edge Cases (Hours 56–64)

### Round-Trip Fixed-Point Fuzzer (New)
- [ ] `tests/fuzz_roundtrip.c` — byte-mutation + grammar-aware random MD generator
- [ ] Assert `render(html_to_md(md_to_html(x))) == render(md_to_html(x)))` (second round trip is a no-op)
- [ ] Run for a default 5-minute budget as part of `make test`; support a longer overnight run via `make fuzz DURATION=3600`
- [ ] On failure, print minimal reproducing input
- [ ] Wire list-renumbering behavior into fuzzer's expected-idempotence logic (not treated as a failure)

### Sanitizer Build (New)
- [ ] `make asan` — build with `-fsanitize=address,undefined`
- [ ] Run full test suite + fuzz corpus against ASan/UBSan build
- [ ] Record: 0 findings, or document/fix any found

### Coverage (New)
- [ ] `make coverage` — build with `--coverage`, run full suite, generate `lcov` HTML report
- [ ] Record line coverage % for README

### Parser Edge Cases
- [ ] Empty document → empty HTML
- [ ] Document with only whitespace → empty HTML
- [ ] Heading with no space after `#` → treat as text
- [ ] List item with no content → `<li></li>`
- [ ] Code fence with no closing fence → error at EOF
- [ ] Nested lists (2+ levels) → correct `<ul>` nesting
- [ ] Blockquote containing list → `<blockquote><ul>...</ul></blockquote>`
- [ ] Inline code containing backtick → `` ` `` (skip for v1 if complex)

### Serializer Edge Cases
- [ ] Empty preview → empty MD
- [ ] Browser adds `<div>` wrappers → strip or handle
- [ ] Copy-paste from external source → graceful degradation, structured error if unparseable
- [ ] Unicode/emoji → pass through unchanged

### Test Coverage Target
- [ ] ≥ 20 parser test cases
- [ ] ≥ 15 serializer test cases
- [ ] ≥ 10 tokenizer test cases
- [ ] ≥ 5 integration / end-to-end tests
- [ ] CommonMark conformance ratio recorded (N/M)
- [ ] Fuzzer clean run recorded (duration + result)
- [ ] Coverage % recorded

---

## Phase 7: STDLIB.md & Polish (Hours 64–70)

### STDLIB.md (Package Killer Narrative)
- [ ] Document: raw socket HTTP server (vs. express/net/http)
- [ ] Document: hand-rolled recursive-descent MD parser (vs. marked/markdown-it) — include CommonMark conformance ratio
- [ ] Document: scoped DOM-tag walker (vs. turndown)
- [ ] Document: hand-rolled JSON body extraction (vs. cJSON/jsmn)
- [ ] Document: round-trip fixed-point fuzzer (vs. AFL/libFuzzer harness setups)
- [ ] Word count target: 500–800 words, concrete comparisons

### README Polish
- [ ] Screenshots or ASCII demo
- [ ] Build instructions (Makefile) — including `make single`, `make asan`, `make coverage`
- [ ] Test runner instructions
- [ ] CommonMark conformance ratio, fuzzer result, Valgrind/ASan results, coverage % all listed
- [ ] Known limitations (tables, footnotes, list renumbering, overlapping-delimiter error behavior)

### Code Quality
- [ ] No compiler warnings with `-Wall -Wextra -Werror`
- [ ] Valgrind: no memory leaks
- [ ] ASan/UBSan: no findings across fuzz corpus
- [ ] Static analysis: `cppcheck` or `clang --analyze`
- [ ] Consistent naming conventions
- [ ] Header guards on all `.h` files
- [ ] `make single` build verified working end-to-end

---

## Phase 8: Buffer & Submission (Hours 70–72)

- [ ] Final end-to-end demo run
- [ ] Generate final `deps-proof.txt` (build log / command output showing zero third-party runtime deps)
- [ ] Finalize `.zero-dep.toml` (track `B`, one-line pitch)
- [ ] **Record 5-minute demo video — required, not optional.** Must show: (1) the tool actually working end-to-end, (2) the empty manifest / `deps-proof.txt` on screen
- [ ] Confirm STDLIB.md has ≥10 disclosed substitutions (STDLIB Log bonus) and explicitly discloses `tests/commonmark/spec.json` as an external corpus
- [ ] Confirm repo is public with OSI-approved license before Aug 31, 18:00 UTC
- [ ] Submit before code freeze (Aug 31, 18:00 UTC) — not just "before deadline" generically
- [ ] Post-hackathon: celebrate, sleep

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| C raw sockets take > 10h | Medium | High | Budget already padded to 10–12h; hour-10 checkpoint cuts keep-alive/pipelining first |
| Parser edge cases explode scope | Medium | High | Strict v1 grammar cutoff; overlapping-delimiter and renumbering behavior already resolved on paper |
| HTML serializer scope creep | Low | Medium | Enforce "only our tags" rule ruthlessly |
| Memory leaks in C | Medium | Medium | Valgrind after every major component; ASan/UBSan build in Phase 6 |
| 72h fatigue → sloppy errors | High | Medium | Sleep 6h/night; no all-nighters |
| JSON parsing too complex | Low | Medium | Use simple key-value extraction instead of full parser |
| CommonMark/fuzzer harness eats core parser time | Medium | Medium | Scheduled in Phase 3/6 with defined checkpoints, not open-ended |
| WebSocket stretch goal cannibalizes core phases | Medium | High | Hard-gated behind hour-64 Phase 5 completion check — no exceptions |

---

## Innovation Bonus Ideas (Gated — only after Hour 64 checkpoint passes)

- [ ] **WebSocket**: hand-rolled RFC 6455 upgrade + frame parser (primary stretch pick — see README/PRD)
- [ ] **Reproducible Build (+5 bonus)**: build twice, diff for byte-identical output, publish both hashes in README — evaluate before Single File if time is tight, since the Makefile discipline is already most of the way there
- [ ] **Live collaborative cursors**: show other users' selections (over WS) — only if WebSocket lands with time to spare
- [ ] **Diff view**: side-by-side source diff on serialize
- [ ] **Export**: `GET /export/pdf` via `wkhtmltopdf` (probably violates zero-dep — likely drop)
- [ ] **Themes**: dark mode toggle (pure CSS) — cheapest of the bunch, good filler if others don't fit
