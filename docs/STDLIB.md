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

Below is the complete, de-duplicated ledger of 15 unique package substitutions designed and implemented across all components.

### 1. `marked` / `markdown-it` / `cmark` (Markdown Parser)
*   **Typical Weekly Downloads:** ~45,000,000 / week (npm)
*   **Replaced With:** Hand-rolled Recursive-Descent Parser ([`src-c/tokenizer.c`](../src-c/tokenizer.c), [`src-c/md_parser.c`](../src-c/md_parser.c)) using `<ctype.h>`, `<string.h>`, and `<stdlib.h>`.
*   **Rationale & Implementation:** Rather than importing a 30,000-line parser, we built a streaming tokenizer and recursive-descent parser that preserves line and column numbers on every token to provide compiler-grade caret-diagnostics.
*   **Honest Limitations Disclosure:** Excludes raw HTML blocks and link reference definitions.
*   **Contributors:** Member 1 (Lead), Member 2.

### 2. `turndown` / `html2markdown` (HTML to Markdown Serializer)
*   **Typical Weekly Downloads:** ~2,500,000 / week (npm)
*   **Replaced With:** Scoped DOM-Tag Fragment Parser and Context-Aware Reverse Walker ([`src-c/html_serializer.c`](../src-c/html_serializer.c)) using `<stdlib.h>`, `<string.h>`, and `<ctype.h>`.
*   **Rationale & Implementation:** General-purpose HTML-to-MD engines are overly complex. Our serializer is strictly scoped to the exact HTML vocabulary emitted by our Markdown renderer (`h1-h6`, `strong`, `em`, `ul/ol`, `code`, `blockquote`, `a`, `img`, `p`, `hr`, `br`), ensuring predictable, lossless bidirectional transformations.
*   **Honest Limitations Disclosure:** Esoteric HTML tags are walked to extract plain text rather than converted; ordered lists are normalized sequentially.
*   **Contributors:** Member 2 (Lead).

### 3. `express` / `libmicrohttpd` / `cpp-httplib` (HTTP / Socket Server)
*   **Typical Weekly Downloads:** ~32,000,000 / week (npm)
*   **Replaced With:** Cross-Platform Raw TCP Sockets ([`src-c/http.c`](../src-c/http.c), [`src-c/platform.c`](../src-c/platform.c)) using `<sys/socket.h>` on Linux and Winsock2 (`<winsock2.h>`) on Windows.
*   **Rationale & Implementation:** Implemented a direct `socket()`, `bind()`, `listen()`, `accept()` loop with a hand-rolled HTTP/1.1 request-line, header, and `Content-Length` parser in C.
*   **Honest Limitations Disclosure:** Immediately closes connections (`Connection: close`); single-threaded blocking synchronous accept loop.
*   **Contributors:** Member 3 (Lead).

### 4. `electron` / `tauri` / `neutralinojs` (Desktop App Frameworks)
*   **Typical Downloads:** Millions of desktop app builds
*   **Replaced With:** Custom OS desktop protocol wrapper ([`src-c/platform.c`](../src-c/platform.c), [`src-c/main.c`](../src-c/main.c)) using POSIX `fork()` + `execvp()` on Linux / Win32 `ShellExecuteA()` on Windows.
*   **Rationale & Implementation:** Direct OS system shell execution launches the default browser, completely bypassing the need for a bloated 150MB Electron/Tauri bundle size.
*   **Honest Limitations Disclosure:** No styling or sizing control over browser windows; closing window does not auto-terminate the background server task.
*   **Contributors:** Member 3 (Lead).

### 5. `chalk` / `colord` / `ansi-colors` (Terminal Styling)
*   **Typical Weekly Downloads:** ~320,000,000 / week (npm)
*   **Replaced With:** Direct ANSI Escape Sequences via `<stdio.h>` with `isatty()` & `NO_COLOR` support.
*   **Rationale & Implementation:** Zero-cost static string escapes (`\033[1;32m`, `\033[0m`) wired into server logs and test suite runners, fully honoring the `NO_COLOR` specification and `<unistd.h>` `isatty()`.
*   **Honest Limitations Disclosure:** Supports standard ANSI 8/16 colors only (no custom true-color parsing).
*   **Contributors:** Member 1 (Lead).

### 6. `chokidar` / `nodemon` (File Watcher & Synchronizer)
*   **Typical Weekly Downloads:** ~40,000,000 / week (npm)
*   **Replaced With:** Client-side Debounce (`setTimeout` in [`src-c/static/client.js`](../src-c/static/client.js)) + Server-side Atomic Write ([`src-c/file_writer.c`](../src-c/file_writer.c)).
*   **Rationale & Implementation:** Replaced background filesystem daemon polling with client-side input debouncing combined with server-side atomic file replacement (`.notes.md.tmp` -> `fsync()` -> `rename()`), ensuring zero file corruption without background watcher packages.
*   **Honest Limitations Disclosure:** Unidirectional push; if the file is modified on disk by an external editor, the client preview will not automatically update.
*   **Contributors:** Member 3 (Lead).

### 7. `left-pad` / `lodash.padStart` (String Padding)
*   **Typical Weekly Downloads:** ~5,000,000 / week (npm)
*   **Replaced With:** Standard `<string.h>` `memset()` + `snprintf()`.
*   **Rationale & Implementation:** Replaced the canonical package-dependency anti-pattern with standard C memory initialization and formatting primitives.
*   **Honest Limitations Disclosure:** Fixed-buffer constraints in formatting.
*   **Contributors:** Member 2 (Lead).

### 8. `minimist` / `clap` / `commander` (CLI Argument Parsing)
*   **Typical Weekly Downloads:** ~80,000,000 / week (npm / Rust)
*   **Replaced With:** POSIX `<unistd.h>` `getopt()` & `argc`/`argv` validation inside [`src-c/main.c`](../src-c/main.c).
*   **Rationale & Implementation:** POSIX standard flag parsing allows robust options handling without importing custom external command parsers.
*   **Honest Limitations Disclosure:** Does not support long flags (e.g. `--port 8080`), requiring short flags exclusively (`-p 8080`).
*   **Contributors:** Member 3 (Lead).

### 9. `Unity` / `GoogleTest` / `Criterion` (Unit Testing Framework)
*   **Typical Downloads:** Widely used C/C++ test suites
*   **Replaced With:** Hand-rolled Macro Test Harness ([`tests/test_harness.h`](../tests/test_harness.h)) using `<stdio.h>`, `<assert.h>`, and `<stdarg.h>`.
*   **Rationale & Implementation:** Self-contained assertion macros (`ASSERT_EQ`, `ASSERT_STR_EQ`, `ASSERT_NOT_NULL`) with colorized pass/fail summaries and test execution statistics.
*   **Honest Limitations Disclosure:** Runs in a single process without `fork()` crash isolation; an uncaught segfault terminates the entire test binary.
*   **Contributors:** Member 2 (Lead).

### 10. `libFuzzer` / `AFL` (Fuzzing Engine)
*   **Typical Downloads:** External LLVM / binary instrumentation frameworks
*   **Replaced With:** Hand-rolled Grammar-Aware Mutation Fuzzer ([`tests/fuzz_roundtrip.c`](../tests/fuzz_roundtrip.c)) using `<stdlib.h>` (`rand()`) and `<time.h>`.
*   **Rationale & Implementation:** Custom fuzzer generating byte-mutations and AST permutations to verify the bidirectional fixed-point theorem: `render(html_to_md(md_to_html(x))) == render(md_to_html(x))`.
*   **Honest Limitations Disclosure:** Uses PRNG byte mutation across a seed corpus rather than hardware/branch coverage feedback.
*   **Contributors:** Member 2 (Lead).

### 11. `dotenv` / `godotenv` (Environment Variable Loader)
*   **Typical Weekly Downloads:** ~40,000,000 / week (npm / Go)
*   **Replaced With:** Standard `<stdlib.h>` `getenv()` + single-pass key-value file reader.
*   **Rationale & Implementation:** Reads configuration directly from system environment variables without external configuration libraries.
*   **Honest Limitations Disclosure:** Simple syntax with no variable expansions.
*   **Contributors:** Member 3 (Lead).

### 12. `zlog` / `spdlog` (Logging Library)
*   **Typical Downloads:** Heavy logging dependencies
*   **Replaced With:** Variadic Logger using `<stdarg.h>`, `<stdio.h>`, and `<time.h>`.
*   **Rationale & Implementation:** Structured timestamped log output written directly to standard I/O.
*   **Honest Limitations Disclosure:** Synchronous blocking stdout writes.
*   **Contributors:** Member 3 (Lead).

### 13. `uuid` / `libuuid` (Unique Identifier Generator)
*   **Typical Weekly Downloads:** ~120,000,000 / week (npm / C)
*   **Replaced With:** POSIX `/dev/urandom` reader via `<fcntl.h>`, `<unistd.h>`.
*   **Rationale & Implementation:** Secure tempfile suffix and transaction token generation without external UUID libraries.
*   **Honest Limitations Disclosure:** Scoped strictly to tempfile collision-free tokens.
*   **Contributors:** Member 3 (Lead).

### 14. `cJSON` / `jsmn` / `nlohmann-json` (JSON Parser & Serializer)
*   **Typical Weekly Downloads:** ~15,000,000 / week (C/C++ packages combined)
*   **Replaced With:** Scoped string-scanner and character unescaper ([`src-c/http.c`](../src-c/http.c)) using `<string.h>` and `<stdlib.h>`.
*   **Rationale & Implementation:** Hand-rolled string search and manual escape mapping avoided compiling a full JSON AST parser when we only needed to extract single root keys.
*   **Honest Limitations Disclosure:** Does not support nested JSON objects, JSON arrays, numeric/boolean type validation, or whitespaces inside keys.
*   **Contributors:** Member 3 (Lead).

### 15. `icu4c` / `iconv` (Unicode & HTML Entity Decoder)
*   **Typical Weekly Downloads:** ~50,000,000 / week (system libraries)
*   **Replaced With:** Hand-rolled HTML named/numeric/hex entity decoder and pure C UTF-8 multi-byte encoder in [`src-c/html_serializer.c`](../src-c/html_serializer.c).
*   **Rationale & Implementation:** Direct codepoint math transforms entities (`&quot;`, `&#39;`, `&#x1F30D;`) into valid UTF-8 sequences without multi-megabyte Unicode database dependencies.
*   **Honest Limitations Disclosure:** Esoteric named entities (e.g. `&therefore;`) pass through untouched.
*   **Contributors:** Member 2 (Lead).

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
- **CommonMark Conformance Ratio:** `177/652 passed (27.15%)`

#### Section Breakdown:
* **Blank lines:** 1/1 passed (100.00%)
* **Inlines:** 1/1 passed (100.00%)
* **Precedence:** 1/1 passed (100.00%)
* **Soft line breaks:** 2/2 passed (100.00%)
* **Paragraphs:** 6/8 passed (75.00%)
* **Thematic breaks:** 14/19 passed (73.68%)
* **Textual content:** 2/3 passed (66.67%)
* **ATX headings:** 11/18 passed (61.11%)
* **Emphasis and strong emphasis:** 63/132 passed (47.73%)
* **Autolinks:** 8/19 passed (42.11%)
* **Code spans:** 8/22 passed (36.36%)
* **Setext headings:** 10/27 passed (37.04%)
* **Raw HTML:** 5/20 passed (25.00%)
* **Fenced code blocks:** 7/29 passed (24.14%)
* **Hard line breaks:** 3/15 passed (20.00%)
* **Tabs:** 2/11 passed (18.18%)
* **Backslash escapes:** 2/13 passed (15.38%)
* **List items:** 6/48 passed (12.50%)
* **Lists:** 3/26 passed (11.54%)
* **Entity and numeric character references:** 4/17 passed (23.53%)
* **Images:** 2/22 passed (9.09%)
* **Indented code blocks:** 1/12 passed (8.33%)
* **Links:** 7/90 passed (7.78%)
* **Block quotes:** 8/25 passed (32.00%)
* **HTML blocks:** 0/44 passed (0.00%)
* **Link reference definitions:** 0/27 passed (0.00%)

#### Honest Limitations:
The parser conforms strictly to standard subset rendering. Complex block quotes, nested links/HTML blocks, and non-flat list styles are outside of the v1 editor companion scope.
