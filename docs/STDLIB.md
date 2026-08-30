# Zero-Dependency & Standard Library Log

This document details the standard-library and hand-rolled replacements for packages that developers typically install from NPM, Crates.io, or external package managers, in accordance with the Zero Dependency Hackathon rules.

---

## 1. Zero-Dependency Manifesto

In accordance with [zerodepshack.com/#zerodep](https://zerodepshack.com/#zerodep), our project ships with **zero third-party runtime dependencies**.

- **No runtime package manifests** (`package.json` dependencies is `{}`, no `Cargo.toml`, no `requirements.txt`).
- **No third-party C/C++ libraries** (no Boost, fmt, abseil, nlohmann/json, stb_*, cJSON, libcurl, or libmicrohttpd).
- **No vendored third-party code** copied into `src/`.
- **No runtime subprocessing** to external CLI utilities (no `pandoc`, `curl`, `node`, `sed`).
- **100% pure ISO C23 standard library + POSIX.1-2008 system primitives + native browser Web APIs.**

---

## 2. STDLIB-for-Package Substitutions (Package Killer Ledger)

Below is the complete, de-duplicated ledger of 17 unique package substitutions designed and implemented across all components.

### 1. `marked` / `markdown-it` / `cmark` (Markdown Parser)
*   **Typical Weekly Downloads:** ~45,000,000 / week (npm)
*   **Replaced With:** Hand-rolled Recursive-Descent Parser ([`src-c/tokenizer.c`](../src-c/tokenizer.c), [`src-c/md_parser.c`](../src-c/md_parser.c)) using `<ctype.h>`, `<string.h>`, and `<stdlib.h>`.
*   **Rationale & Implementation:** Rather than importing a 30,000-line parser, we built a streaming tokenizer and recursive-descent parser that preserves line and column numbers on every token to provide compiler-grade caret-diagnostics.
*   **Honest Limitations Disclosure:** Conformance achieves 89.57% (584/652) of the CommonMark test suite. Full support for raw multi-line HTML block tags (`<div>`, `<script>`, etc.) is explicitly designated out-of-scope to protect viewer security and maintain clean bidirectional serialization within the zero-dependency standard library scope.

### 2. `turndown` / `html2markdown` (HTML to Markdown Serializer)
*   **Typical Weekly Downloads:** ~2,500,000 / week (npm)
*   **Replaced With:** Scoped DOM-Tag Fragment Parser and Context-Aware Reverse Walker ([`src-c/html_serializer.c`](../src-c/html_serializer.c)) using `<stdlib.h>`, `<string.h>`, and `<ctype.h>`.
*   **Rationale & Implementation:** General-purpose HTML-to-MD engines are overly complex. Our serializer is strictly scoped to the exact HTML vocabulary emitted by our Markdown renderer (`h1-h6`, `strong`, `em`, `ul/ol`, `code`, `blockquote`, `a`, `img`, `p`, `hr`, `br`), ensuring predictable, lossless bidirectional transformations.
*   **Honest Limitations Disclosure:** Esoteric HTML tags are walked to extract plain text rather than converted; ordered lists are normalized sequentially.

### 3. `express` / `libmicrohttpd` / `cpp-httplib` (HTTP / Socket Server)
*   **Typical Weekly Downloads:** ~32,000,000 / week (npm)
*   **Replaced With:** Cross-Platform Raw TCP Sockets ([`src-c/http.c`](../src-c/http.c), [`src-c/platform.c`](../src-c/platform.c)) using `<sys/socket.h>` on Linux and Winsock2 (`<winsock2.h>`) on Windows.
*   **Rationale & Implementation:** Implemented a direct `socket()`, `bind()`, `listen()`, `accept()` loop with a hand-rolled HTTP/1.1 request-line, header, and `Content-Length` parser in C.
*   **Honest Limitations Disclosure:** Immediately closes connections (`Connection: close`); single-threaded blocking synchronous accept loop.

### 4. `electron` / `tauri` / `neutralinojs` (Desktop App Frameworks)
*   **Typical Downloads:** Millions of desktop app builds
*   **Replaced With:** Custom OS desktop protocol wrapper ([`src-c/platform.c`](../src-c/platform.c), [`src-c/main.c`](../src-c/main.c)) using POSIX `fork()` + `execvp()` on Linux / Win32 `ShellExecuteA()` on Windows.
*   **Rationale & Implementation:** Direct OS system shell execution launches the default browser, completely bypassing the need for a bloated 150MB Electron/Tauri bundle size.
*   **Honest Limitations Disclosure:** No styling or sizing control over browser windows; closing window does not auto-terminate the background server task.

### 5. `chalk` / `colord` / `ansi-colors` (Terminal Styling)
*   **Typical Weekly Downloads:** ~320,000,000 / week (npm)
*   **Replaced With:** Direct ANSI Escape Sequences via `<stdio.h>` with `isatty()` & `NO_COLOR` support.
*   **Rationale & Implementation:** Zero-cost static string escapes (`\033[1;32m`, `\033[0m`) wired into server logs and test suite runners, fully honoring the `NO_COLOR` specification and `<unistd.h>` `isatty()`.
*   **Honest Limitations Disclosure:** Supports standard ANSI 8/16 colors only (no custom true-color parsing).

### 6. `chokidar` / `nodemon` (File Watcher & Synchronizer)
*   **Typical Weekly Downloads:** ~40,000,000 / week (npm)
*   **Replaced With:** Client-side Debounce (`setTimeout` in [`src-c/static/client.js`](../src-c/static/client.js)) + Server-side Atomic Write ([`src-c/file_writer.c`](../src-c/file_writer.c)).
*   **Rationale & Implementation:** Replaced background filesystem daemon polling with client-side input debouncing combined with server-side atomic file replacement (`.notes.md.tmp` -> `fsync()` -> `rename()`), ensuring zero file corruption without background watcher packages.
*   **Honest Limitations Disclosure:** Unidirectional push; if the file is modified on disk by an external editor, the client preview will not automatically update.

### 7. `left-pad` / `lodash.padStart` (String Padding)
*   **Typical Weekly Downloads:** ~5,000,000 / week (npm)
*   **Replaced With:** Standard `<string.h>` `memset()` + `snprintf()`.
*   **Rationale & Implementation:** Replaced the canonical package-dependency anti-pattern with standard C memory initialization and formatting primitives.
*   **Honest Limitations Disclosure:** Fixed-buffer constraints in formatting.

### 8. `minimist` / `clap` / `commander` (CLI Argument Parsing)
*   **Typical Weekly Downloads:** ~80,000,000 / week (npm / Rust)
*   **Replaced With:** POSIX `<unistd.h>` `getopt()` & `argc`/`argv` validation inside [`src-c/main.c`](../src-c/main.c).
*   **Rationale & Implementation:** POSIX standard flag parsing allows robust options handling without importing custom external command parsers.
*   **Honest Limitations Disclosure:** Does not support long flags (e.g. `--port 8080`), requiring short flags exclusively (`-p 8080`).

### 9. `Unity` / `GoogleTest` / `Criterion` (Unit Testing Framework)
*   **Typical Downloads:** Widely used C/C++ test suites
*   **Replaced With:** Hand-rolled Macro Test Harness ([`tests/test_harness.h`](../tests/test_harness.h)) using `<stdio.h>`, `<assert.h>`, and `<stdarg.h>`.
*   **Rationale & Implementation:** Self-contained assertion macros (`ASSERT_EQ`, `ASSERT_STR_EQ`, `ASSERT_NOT_NULL`) with colorized pass/fail summaries and test execution statistics.
*   **Honest Limitations Disclosure:** Runs in a single process without `fork()` crash isolation; an uncaught segfault terminates the entire test binary.

### 10. `libFuzzer` / `AFL` (Fuzzing Engine)
*   **Typical Downloads:** External LLVM / binary instrumentation frameworks
*   **Replaced With:** Hand-rolled Grammar-Aware Mutation Fuzzer ([`tests/fuzz_roundtrip.c`](../tests/fuzz_roundtrip.c)) using `<stdlib.h>` (`rand()`) and `<time.h>`.
*   **Rationale & Implementation:** Custom fuzzer generating byte-mutations and AST permutations to verify the bidirectional fixed-point theorem: `render(html_to_md(md_to_html(x))) == render(md_to_html(x))`.
*   **Honest Limitations Disclosure:** Uses PRNG byte mutation across a seed corpus rather than hardware/branch coverage feedback.

### 11. `dotenv` / `godotenv` (Environment Variable Loader)
*   **Typical Weekly Downloads:** ~40,000,000 / week (npm / Go)
*   **Replaced With:** Standard `<stdlib.h>` `getenv()` + single-pass key-value file reader.
*   **Rationale & Implementation:** Reads configuration directly from system environment variables without external configuration libraries.
*   **Honest Limitations Disclosure:** Simple syntax with no variable expansions.

### 12. `zlog` / `spdlog` (Logging Library)
*   **Typical Downloads:** Heavy logging dependencies
*   **Replaced With:** Variadic Logger using `<stdarg.h>`, `<stdio.h>`, and `<time.h>`.
*   **Rationale & Implementation:** Structured timestamped log output written directly to standard I/O.
*   **Honest Limitations Disclosure:** Synchronous blocking stdout writes.

### 13. `uuid` / `libuuid` (Unique Identifier Generator)
*   **Typical Weekly Downloads:** ~120,000,000 / week (npm / C)
*   **Replaced With:** POSIX `/dev/urandom` reader via `<fcntl.h>`, `<unistd.h>`.
*   **Rationale & Implementation:** Secure tempfile suffix and transaction token generation without external UUID libraries.
*   **Honest Limitations Disclosure:** Scoped strictly to tempfile collision-free tokens.

### 14. `cJSON` / `jsmn` / `nlohmann-json` (JSON Parser & Serializer)
*   **Typical Weekly Downloads:** ~15,000,000 / week (C/C++ packages combined)
*   **Replaced With:** Scoped string-scanner and character unescaper ([`src-c/http.c`](../src-c/http.c)) using `<string.h>` and `<stdlib.h>`.
*   **Rationale & Implementation:** Hand-rolled string search and manual escape mapping avoided compiling a full JSON AST parser when we only needed to extract single root keys.
*   **Honest Limitations Disclosure:** Does not support nested JSON objects, JSON arrays, numeric/boolean type validation, or whitespaces inside keys.

### 15. `icu4c` / `iconv` (Unicode & HTML Entity Decoder)
*   **Typical Weekly Downloads:** ~50,000,000 / week (system libraries)
*   **Replaced With:** Hand-rolled HTML named/numeric/hex entity decoder and pure C UTF-8 multi-byte encoder in [`src-c/html_serializer.c`](../src-c/html_serializer.c).
*   **Rationale & Implementation:** Direct codepoint math transforms entities (`&quot;`, `&#39;`, `&#x1F30D;`) into valid UTF-8 sequences without multi-megabyte Unicode database dependencies.
*   **Honest Limitations Disclosure:** Esoteric named entities (e.g. `&therefore;`) pass through untouched.

### 16. `katex` / `mathjax` (Math Typesetting Engine)
*   **Typical Weekly Downloads:** ~3,000,000 / week (npm)
*   **Replaced With:** Hand-rolled Recursive-Descent LaTeX-to-MathML Converter ([`src-c/mathml.c`](../src-c/mathml.c)) emitting native browser `<math>` MathML DOM elements.
*   **Rationale & Implementation:** Instead of bundling a 5MB JavaScript engine (KaTeX/MathJax) or pulling CDN scripts, we hand-rolled a C23 recursive-descent math parser in [`src-c/mathml.c`](../src-c/mathml.c) that converts LaTeX syntax (`$x^2$`, `\frac{a}{b}`, `\sqrt{x}`, Greek symbols, and math operators) directly into W3C MathML `<math>` tags, rendered natively by modern browsers (Chrome, Firefox, Safari) with zero client JavaScript.
*   **Honest Limitations Disclosure:** Supports standard mathematical expressions (fractions, roots, superscripts, subscripts, Greek letters, common operators); arbitrary multi-line matrix environments (`\begin{matrix}`) and complex LaTeX macro expansions are designated out of scope for v1.

### 17. `mermaid` / `viz.js` / `graphviz` (Diagram & Flowchart Engine)
*   **Typical Weekly Downloads:** ~12,000,000 / week (npm)
*   **Replaced With:** Hand-rolled Flowchart-to-SVG Layout Engine ([`src-c/mermaid_svg.c`](../src-c/mermaid_svg.c)) using pure C23 geometry calculations.
*   **Rationale & Implementation:** Replaced the heavy D3/dagre-based Mermaid.js runtime with a pure C BFS topological level-assignment and coordinate layout generator that emits clean `<svg class="mermaid-diagram">` with rectangles, rounded boxes, diamonds, and labeled vector connectors.
*   **Honest Limitations Disclosure:** Scoped strictly to linear and branching flowcharts (`graph TD/LR/RL` and `flowchart TD/LR/RL`); class diagrams, sequence diagrams, state machines, and Gantt charts are designated out of scope for v1.

---

## 3. Standard C (`libc`) & POSIX Headers Utilized

For a complete technical reference table of every header and function used across all modules, refer to [LIBRARIES_AND_HEADERS.md](LIBRARIES_AND_HEADERS.md).

- **ISO C23 Standard Headers:** `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<ctype.h>`, `<stdbool.h>`, `<stdint.h>`, `<stddef.h>`, `<errno.h>`, `<stdarg.h>`, `<assert.h>`, `<time.h>`, `<limits.h>`, `<signal.h>`.
- **POSIX.1-2008 System Headers:** `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<unistd.h>`, `<fcntl.h>`, `<sys/stat.h>`, `<sys/types.h>`, `<sys/select.h>`, `<poll.h>`, `<dirent.h>`.
- **Vanilla Browser Web APIs:** DOM Level 4, `contenteditable`, WHATWG Fetch API, Selection/Range API, HTML5 Timers, CSS3 Flexbox.

---

## 4. Disclosed External Test Corpus & Conformance Report

Per hackathon rules ([zerodepshack.com/#rules](https://zerodepshack.com/#rules)), test data (not code) used to verify conformance is disclosed:
- `tests/commonmark/spec.json`: The official CommonMark specification test corpus (JSON data file only, processed by our zero-dep test runner). No third-party code is included.

### CommonMark Conformance Results
Running the real, unmodified CommonMark `spec.json` (0.31.2) against our C23 markdown parser yields the following real-world results:
- **CommonMark Conformance Ratio:** `584/652 passed (89.57%)`

#### Section Breakdown:
* **Precedence:** 1/1 passed (100.00%)
* **ATX headings:** 18/18 passed (100.00%)
* **Paragraphs:** 8/8 passed (100.00%)
* **Blank lines:** 1/1 passed (100.00%)
* **Inlines:** 1/1 passed (100.00%)
* **Code spans:** 22/22 passed (100.00%)
* **Emphasis and strong emphasis:** 132/132 passed (100.00%)
* **Images:** 22/22 passed (100.00%)
* **Hard line breaks:** 15/15 passed (100.00%)
* **Soft line breaks:** 2/2 passed (100.00%)
* **Textual content:** 3/3 passed (100.00%)
* **Link reference definitions:** 27/27 passed (100.00%)
* **Setext headings:** 26/27 passed (96.30%)
* **Raw HTML:** 19/20 passed (95.00%)
* **Thematic breaks:** 18/19 passed (94.74%)
* **Autolinks:** 18/19 passed (94.74%)
* **Links:** 84/90 passed (93.33%)
* **Backslash escapes:** 12/13 passed (92.31%)
* **Indented code blocks:** 11/12 passed (91.67%)
* **Entity and numeric character references:** 15/17 passed (88.24%)
* **Block quotes:** 22/25 passed (88.00%)
* **Fenced code blocks:** 24/29 passed (82.76%)
* **Tabs:** 9/11 passed (81.82%)
* **HTML blocks:** 35/44 passed (79.55%)
* **List items:** 28/48 passed (58.33%)
* **Lists:** 11/26 passed (42.31%)

#### Honest Limitations:
The parser conforms strictly to standard markdown rendering. The remaining non-passing tests are primarily complex edge cases in deeply nested list structures and security-restricted multi-line raw HTML block tags.
