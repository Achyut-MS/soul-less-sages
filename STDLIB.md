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

## 3. Standard C (`libc`) & POSIX Headers Utilized

For a complete technical reference table of every header and function used across all modules, refer to [Docs/LIBRARIES_AND_HEADERS.md](file:///home/aaarya/Desktop/Projects/SoulessSages/Docs/LIBRARIES_AND_HEADERS.md).

- **ISO C23 Standard Headers:** `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<ctype.h>`, `<stdbool.h>`, `<stdint.h>`, `<stddef.h>`, `<errno.h>`, `<stdarg.h>`, `<assert.h>`, `<time.h>`, `<limits.h>`, `<signal.h>`.
- **POSIX.1-2008 System Headers:** `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<unistd.h>`, `<fcntl.h>`, `<sys/stat.h>`, `<sys/types.h>`, `<sys/select.h>`, `<poll.h>`, `<dirent.h>`.
- **Vanilla Browser Web APIs:** DOM Level 4, `contenteditable`, WHATWG Fetch API, Selection/Range API, HTML5 Timers, CSS3 Flexbox.

---

## 4. Disclosed External Test Corpus

Per hackathon rules ([zerodepshack.com/#rules](https://zerodepshack.com/#rules)), test data (not code) used to verify conformance is disclosed:
- `tests/commonmark/spec.json`: The official CommonMark specification test corpus (JSON data file only, processed by our zero-dep test runner). No third-party code is included.
