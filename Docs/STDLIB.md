# STDLIB.md — Standard Library Substitution Ledger

> **Zero Dependency Hackathon** · [zerodepshack.com](https://zerodepshack.com)  
> **Track B — Parsers & Data Formats** (with Track C Network craft)  
> **Project:** Zero-Dep Markdown Viewer / Visualizer (`SoulessSages`)  
> **Language:** C (ISO C23 standard) + POSIX.1-2008 + Vanilla Browser Web APIs  
> **Manifest Status:** Empty (`0` third-party runtime dependencies)

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

This ledger documents the standard-library and hand-rolled replacements for packages that developers typically install from npm, crates.io, or package managers:

### 1. `marked` / `markdown-it` / `cmark` (Markdown Parser)
- **Typical Weekly Downloads:** ~45,000,000 / week (npm)
- **Replaced With:** Hand-rolled Recursive-Descent Parser (`tokenizer.c`, `md_parser.c`) using `<ctype.h>`, `<string.h>`, `<stdlib.h>`.
- **Rationale & Implementation:** Rather than importing a 30,000-line third-party parser, we constructed a streaming tokenizer and recursive-descent parser that preserves line and column numbers on every token to provide compiler-grade, caret-annotated error reporting (`notes.md:14:3: unterminated code fence`).

### 2. `turndown` / `html2markdown` (HTML to Markdown Serializer)
- **Typical Weekly Downloads:** ~2,500,000 / week (npm)
- **Replaced With:** Scoped DOM-Tag Walker (`html_serializer.c`) using `<string.h>`, `<stdio.h>`.
- **Rationale & Implementation:** General-purpose HTML-to-MD engines are overly complex because they attempt to handle arbitrary web markup. Our serializer is strictly scoped to the exact HTML vocabulary emitted by our Markdown renderer (`h1-h6`, `strong`, `em`, `ul/ol`, `code`, `blockquote`, `a`, `p`), ensuring predictable, lossless bidirectional transformations.

### 3. `express` / `libmicrohttpd` / `cpp-httplib` (HTTP / Socket Server)
- **Typical Weekly Downloads:** ~30,000,000 / week (npm / C)
- **Replaced With:** Cross-Platform Raw Sockets (`http.c`, `platform.c`) using `<sys/socket.h>` on Linux and `<winsock2.h>` on Windows.
- **Rationale & Implementation:** Implemented a direct `socket()`, `bind()`, `listen()`, `accept()` loop with a hand-rolled HTTP/1.1 request-line, header, and `Content-Length` parser in C. Zero external server libraries required.

### 4. `electron` / `tauri` / `neutralinojs` (Desktop App Frameworks)
- **Typical Downloads:** Millions of desktop app builds
- **Replaced With:** Hand-rolled C23 Desktop Runner (`platform.c`, `main.c`) using POSIX `xdg-open` / Win32 `ShellExecuteA()`.
- **Rationale & Implementation:** Instead of shipping a 150MB Electron runtime with hundreds of npm packages, the C executable directly serves the local dumb-terminal UI and automatically spawns the desktop view natively with zero third-party dependencies.

### 5. `chalk` / `colord` / `ansi-colors` (Terminal Styling)
- **Typical Weekly Downloads:** ~320,000,000 / week (npm)
- **Replaced With:** Direct ANSI Escape Sequences via `<stdio.h>` with `isatty()` & `NO_COLOR` support.
- **Rationale & Implementation:** Zero-cost static string escapes (`\033[1;32m`, `\033[0m`) wired into server logs and test suite runners, fully honoring the `NO_COLOR` specification and `<unistd.h>` `isatty(STDOUT_FILENO)`.

### 6. `chokidar` / `nodemon` (File Watcher & Synchronizer)
- **Typical Weekly Downloads:** ~40,000,000 / week (npm)
- **Replaced With:** Client-side Debounce (`setTimeout`) + Atomic `fsync()` / `rename()` (`<stdio.h>`, `<unistd.h>`).
- **Rationale & Implementation:** Replaced background filesystem daemon polling with 200ms client-side input debouncing combined with server-side atomic file replacement (`.notes.md.tmp` -> `fsync()` -> `rename()`), ensuring zero file corruption without background watcher packages.

### 7. `left-pad` / `lodash.padStart` (String Padding)
- **Typical Weekly Downloads:** ~5,000,000 / week (npm)
- **Replaced With:** Standard `<string.h>` `memset()` + `snprintf()`.
- **Rationale & Implementation:** Replaced the canonical package-dependency anti-pattern with standard C memory initialization and formatting primitives in 2 lines of code.

### 8. `minimist` / `clap` / `commander` (CLI Argument Parsing)
- **Typical Weekly Downloads:** ~80,000,000 / week (npm / Rust)
- **Replaced With:** POSIX `<unistd.h>` `getopt()` & `argc`/`argv` validation in `main.c`.
- **Rationale & Implementation:** Implemented clean command-line flag handling (`-p <port>`, `-h`, `-v`, `<file>`) using the standard POSIX `getopt()` facility.

### 9. `Unity` / `GoogleTest` / `Criterion` (Unit Testing Framework)
- **Typical Downloads:** Widely used C/C++ test suites
- **Replaced With:** Hand-rolled Macro Test Harness (`tests/test_harness.h`) using `<stdio.h>`, `<assert.h>`, `<stdarg.h>`.
- **Rationale & Implementation:** Self-contained assertion macros (`ASSERT_EQ`, `ASSERT_STR_EQ`, `ASSERT_NOT_NULL`) with colorized pass/fail summaries and test execution statistics.

### 10. `libFuzzer` / `AFL` (Fuzzing Engine)
- **Typical Downloads:** External LLVM / binary instrumentation frameworks
- **Replaced With:** Hand-rolled Grammar-Aware Mutation Fuzzer (`tests/fuzz_roundtrip.c`) using `<stdlib.h>` (`rand()`), `<time.h>`.
- **Rationale & Implementation:** Custom fuzzer generating byte-mutations and AST permutations to verify the bidirectional fixed-point theorem: `render(html_to_md(md_to_html(x))) == render(md_to_html(x))`.

### 11. `dotenv` / `godotenv` (Environment Variable Loader)
- **Typical Weekly Downloads:** ~40,000,000 / week (npm / Go)
- **Replaced With:** Standard `<stdlib.h>` `getenv()` + single-pass key-value file reader.
- **Rationale & Implementation:** Reads configuration directly from system environment variables without external configuration libraries.

### 12. `zlog` / `spdlog` (Logging Library)
- **Typical Downloads:** Heavy logging dependencies
- **Replaced With:** Variadic Logger using `<stdarg.h>`, `<stdio.h>`, `<time.h>`.
- **Rationale & Implementation:** Structured timestamped log output (`[2026-08-29 11:20:00] [INFO] HTTP GET / 200 OK`) written directly to standard I/O.

### 13. `uuid` / `libuuid` (Unique Identifier Generator)
- **Typical Weekly Downloads:** ~120,000,000 / week (npm / C)
- **Replaced With:** POSIX `/dev/urandom` reader via `<fcntl.h>`, `<unistd.h>`.
- **Rationale & Implementation:** Secure tempfile suffix and transaction token generation without external UUID libraries.

---

## 3. Systems & Frontend Lead Substitutions (Member 3 Ledger)

This section documents standard-library and native API substitutions developed by the Systems & Frontend Lead. In compliance with the hackathon's honesty rules, all functional and performance limitations compared to the industry-standard packages are explicitly disclosed below:

### 1. `express` / `libmicrohttpd` / `cpp-httplib` (HTTP/1.1 Companion Server)
*   **Weekly Downloads:** ~32,000,000 / week (npm) | ~1,200,000 / week (C/C++ libraries combined)
*   **Replaced With:** Direct loopback TCP socket server ([`src-c/http.c`](file:///home/aaarya/Desktop/Projects/SoulessSages/src-c/http.c), [`src-c/platform.c`](file:///home/aaarya/Desktop/Projects/SoulessSages/src-c/platform.c)) using raw POSIX sockets (`<sys/socket.h>`) on Linux and Winsock2 (`<winsock2.h>`) on Windows.
*   **One-Line Rationale:** Hand-rolled raw socket binds and request-line parsing avoided pulling in a multi-thousand-line web server package or compiler dependency.
*   **Honest Limitations Disclosure:** 
    *   *No Keep-Alive:* The socket is immediately closed after responding (`Connection: close`), resulting in lower throughput and higher latency for repeated asset loading compared to Express.
    *   *Single-Threaded Blocking Loop:* Client handling is fully blocking and synchronous; concurrent requests are queued by the OS TCP buffer instead of processed concurrently in a thread pool.

### 2. `electron` / `tauri` (Desktop Application Framework & Browser Launcher)
*   **Weekly Downloads:** ~3,500,000 / week (npm) | ~200,000 / week (crates.io)
*   **Replaced With:** Custom OS desktop protocol wrapper ([`src-c/platform.c`](file:///home/aaarya/Desktop/Projects/SoulessSages/src-c/platform.c), [`src-c/main.c`](file:///home/aaarya/Desktop/Projects/SoulessSages/src-c/main.c)) invoking `ShellExecuteA` on Windows and `fork()` + `execvp()` on Linux with graceful stdout fallback degradation.
*   **One-Line Rationale:** Direct OS system shell execution launches the native default browser, completely bypassing the need for a bloated 150MB browser wrapper bundle.
*   **Honest Limitations Disclosure:**
    *   *No Sandboxed Window Control:* We cannot enforce window styling, frame size, or inject custom desktop menus; we are entirely dependent on the user's host web browser configuration.
    *   *Process Decoupling:* Spawning the system browser decouples the GUI lifecycle from the server; closing the browser window does not automatically terminate the background server task.

### 3. `chokidar` / `nodemon` (Live Sync & File Watcher)
*   **Weekly Downloads:** ~45,000,000 / week (npm)
*   **Replaced With:** Client-side editor debouncer ([`src-c/static/client.js`](file:///home/aaarya/Desktop/Projects/SoulessSages/src-c/static/client.js)) + server-side atomic replacement ([`src-c/file_writer.c`](file:///home/aaarya/Desktop/Projects/SoulessSages/src-c/file_writer.c)).
*   **One-Line Rationale:** Debounced input triggers direct HTTP save requests, rendering disk-watching daemons redundant and reducing filesystem wear.
*   **Honest Limitations Disclosure:**
    *   *Unidirectional Notification:* Replaced real-time background file change polling with client-driven HTTP write pushes; if the file is modified on disk by an external editor, the client preview will not automatically update.
    *   *Thread-Sleep Debouncing:* Background saving spawns a worker thread that sleeps for 200ms increments, which is slightly less reactive than real-time OS file notifications (e.g. `inotify` or `ReadDirectoryChangesW`).

### 4. `cJSON` / `jsmn` / `nlohmann-json` (JSON Parser & Serializer)
*   **Weekly Downloads:** ~15,000,000 / week (C/C++ packages combined)
*   **Replaced With:** Scoped string-scanner and character unescaper ([`src-c/http.c`](file:///home/aaarya/Desktop/Projects/SoulessSages/src-c/http.c)) using `<string.h>` and `<stdlib.h>`.
*   **One-Line Rationale:** Hand-rolled string search and manual escape mapping avoided compiling a full JSON AST parser when we only needed to extract single root keys.
*   **Honest Limitations Disclosure:**
    *   *No AST Parsing / Validation:* The parser does not support nested JSON objects, JSON arrays, numeric/boolean type validation, or whitespaces inside keys (it strictly expects flat, contiguous string-value matches like `"key":"value"`).
    *   *Slower Lookup Complexity:* Key extraction is $O(N)$ via `strstr` string scanning for every lookup rather than $O(1)$ hashtable hashing or pre-indexed AST tree traversals.

### 5. `minimist` / `clap` (Command-Line Argument Parser)
*   **Weekly Downloads:** ~80,000,000 / week (npm) | ~10,000,000 / week (crates.io)
*   **Replaced With:** Standard POSIX `<unistd.h>` `getopt()` interface inside [`src-c/main.c`](file:///home/aaarya/Desktop/Projects/SoulessSages/src-c/main.c).
*   **One-Line Rationale:** POSIX standard flag parsing allows robust options handling without importing custom external command parsers.
*   **Honest Limitations Disclosure:**
    *   *No Long-Flag Support:* The standard `getopt` implementation does not support long flags (e.g. `--port 8080`), requiring the user to use short flags exclusively (`-p 8080`).
    *   *No Auto-Generated Help:* Help messages and usage guidelines must be manually maintained and printed via `printf()` rather than dynamically generated from CLI attributes.

---

## 4. Serializer & Correctness Lead Substitutions (Member 2 Ledger)

This section documents standard-library and hand-rolled algorithms developed by the Serializer & Correctness Lead (`html_serializer.c`, `fuzz_roundtrip.c`, `test_html_serializer.c`), complete with honest limitations disclosures:

### 1. `turndown` / `html2markdown` (HTML to Markdown Reverse Serializer)
*   **Weekly Downloads:** ~2,500,000 / week (npm)
*   **Replaced With:** Scoped DOM-tag fragment parser and context-aware reverse walker in [`src-c/html_serializer.c`](file:///home/aaarya/Desktop/Projects/SoulessSages/src-c/html_serializer.c) using ISO C23 standard library (`<stdlib.h>`, `<string.h>`, `<ctype.h>`).
*   **One-Line Rationale:** Scoping the reverse serializer strictly to our renderer's known tag vocabulary (`h1-h6`, `strong`, `em`, `code`, `pre`, `blockquote`, `ul`, `ol`, `li`, `a`, `img`, `p`, `hr`, `br`) guarantees deterministic, lossless round-trip transformations.
*   **Honest Limitations Disclosure:**
    *   *Scoped Tag Vocabulary Only:* Arbitrary web elements (e.g. `<table>`, `<video>`, `<canvas>`, `<svg>`, `<iframe>`) are transparently walked to extract plain text rather than converted into specialized Markdown extensions.
    *   *Sequential Ordered List Normalization:* Ordered lists are systematically renumbered sequentially (`1.`, `2.`, `3.`) rather than preserving non-sequential original source markers (e.g. `3.`, `5.`, `5.`).

### 2. `libFuzzer` / `AFL` (Grammar-Aware & Mutation Fuzzing Engine)
*   **Weekly Downloads:** Widely used LLVM/binary instrumentation tooling
*   **Replaced With:** Time-budgeted grammar-corpus and byte-mutation round-trip fuzzer in [`tests/fuzz_roundtrip.c`](file:///home/aaarya/Desktop/Projects/SoulessSages/tests/fuzz_roundtrip.c) using a custom `xorshift32` PRNG and `clock_gettime(CLOCK_MONOTONIC)`.
*   **One-Line Rationale:** Direct algorithmic verification of the bidirectional fixed-point invariant `render(html_to_md(render(x))) == render(x)` running millions of cycles per minute with zero compiler instrumentation dependencies.
*   **Honest Limitations Disclosure:**
    *   *No Code-Coverage Guided Branch Feedback:* Uses PRNG byte mutation across a seed corpus rather than hardware edge/branch coverage feedback (AFL bitmap/SanitizerCoverage).

### 3. `Unity` / `GoogleTest` / `Criterion` (C Unit Testing Framework)
*   **Weekly Downloads:** Standard C/C++ unit test suites
*   **Replaced With:** Zero-dependency macro test harness in [`tests/test_harness.h`](file:///home/aaarya/Desktop/Projects/SoulessSages/tests/test_harness.h) with ANSI colorized pass/fail summaries.
*   **One-Line Rationale:** Header-only test assertions (`ASSERT_TRUE`, `ASSERT_STR_EQ`, `ASSERT_NOT_NULL`) compile instantly without external build artifacts.
*   **Honest Limitations Disclosure:**
    *   *No Fork/Crash Isolation:* Test execution runs in a single process without `fork()` crash isolation; an uncaught segfault terminates the entire test binary.

### 4. `icu4c` / `iconv` (Unicode & HTML Entity Decoder)
*   **Weekly Downloads:** ~50,000,000 / week (system libraries)
*   **Replaced With:** Hand-rolled HTML named/numeric/hex entity decoder and pure C UTF-8 multi-byte encoder in [`src-c/html_serializer.c`](file:///home/aaarya/Desktop/Projects/SoulessSages/src-c/html_serializer.c).
*   **One-Line Rationale:** Direct codepoint math transforms entities (`&quot;`, `&#39;`, `&#x1F30D;`) into valid UTF-8 sequences without multi-megabyte Unicode database dependencies.
*   **Honest Limitations Disclosure:**
    *   *Core Named Entities Scoped:* Supports XML/HTML5 standard core entities (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&apos;`, `&#39;`, `&nbsp;`) and all decimal/hex numeric codepoints; esoteric named entities (e.g. `&therefore;`, `&backprime;`) pass through untouched.

---

## 5. Standard C (`libc`) & POSIX Headers Utilized

For a complete technical reference table of every header and function used across all modules, refer to [Docs/LIBRARIES_AND_HEADERS.md](file:///home/aaarya/Desktop/Projects/SoulessSages/Docs/LIBRARIES_AND_HEADERS.md).

- **ISO C23 Standard Headers:** `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<ctype.h>`, `<stdbool.h>`, `<stdint.h>`, `<stddef.h>`, `<errno.h>`, `<stdarg.h>`, `<assert.h>`, `<time.h>`, `<limits.h>`, `<signal.h>`.
- **POSIX.1-2008 System Headers:** `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<unistd.h>`, `<fcntl.h>`, `<sys/stat.h>`, `<sys/types.h>`, `<sys/select.h>`, `<poll.h>`, `<dirent.h>`.
- **Vanilla Browser Web APIs:** DOM Level 4, `contenteditable`, WHATWG Fetch API, Selection/Range API, HTML5 Timers, CSS3 Flexbox.

---

## 6. Disclosed External Test Corpus

Per hackathon rules ([zerodepshack.com/#rules](https://zerodepshack.com/#rules)), test data (not code) used to verify conformance is disclosed:
- `tests/commonmark/spec.json`: The official CommonMark specification test corpus (JSON data file only, processed by our zero-dep test runner). No third-party code is included.
