# Libraries & Headers Reference Guide

> **Project:** Zero-Dep Markdown Viewer / Visualizer (`SoulessSages`)  
> **Track:** Track B — Parsers & Data Formats (with Track C Network craft)  
> **Event Reference:** [Zero Dependency Hackathon (zerodepshack.com)](https://zerodepshack.com) · Aug 28–31, 2026 · 72h  
> **Target Platform:** Linux / POSIX (C23 standard library + POSIX.1-2008)  
> **Rule Compliance:** 100% Standard Library & POSIX System Headers Only. **Zero External / Cloud APIs.**

---

## 1. Absolute Prohibition on Third-Party & Cloud APIs

Per the official **Zero Dependency Hackathon** brief ([zerodepshack.com/#rules](https://zerodepshack.com/#rules)):

> *"Projects that need a running third-party service (a database server, a cloud API) to do anything are strictly out of scope / disqualified."*

- **NO External / Cloud APIs**: No OpenAI, Google Cloud, AWS, REST APIs, or third-party web services.
- **NO Third-Party C Libraries**: No Boost, `fmt`, `abseil`, `nlohmann/json`, `cJSON`, `stb_*`, `libcurl`, `libevent`, `libmicrohttpd`.
- **NO npm / CDN Packages**: No npm dependencies (`package.json` is empty/absent), no external `<script src="...">` tags, no external stylesheets, fonts, or CDNs.
- **100% Offline & Self-Contained**: The entire application runs strictly on `localhost` using standard C23 headers, POSIX OS primitives, and browser native built-in syntax.

---

## 2. Standard C Library Headers (`libc` / ISO C23)

These are the core ISO C standard library headers that ship directly with the compiler (`gcc` / `clang`). Every function used comes from these headers:

| C Header | Standard | Exact Project Role | Functions & Types Used | Module |
|---|---|---|---|---|
| `<stdio.h>` | C89/C99/C23 | File I/O, atomic file replacement, formatting, output | `FILE*`, `fopen()`, `fclose()`, `fread()`, `fwrite()`, `fseek()`, `ftell()`, `fflush()`, `snprintf()`, `fprintf()`, `rename()` *(atomic swap)*, `remove()` | `file_writer.c`, `http.c`, `main.c`, `tests/` |
| `<stdlib.h>` | C89/C99/C23 | Dynamic memory allocation, process exit, number parsing, PRNG | `malloc()`, `calloc()`, `realloc()`, `free()`, `exit()`, `EXIT_SUCCESS`, `EXIT_FAILURE`, `strtol()`, `atoi()`, `rand()`, `srand()`, `getenv()` | `tokenizer.c`, `md_parser.c`, `html_serializer.c`, `fuzz_roundtrip.c` |
| `<string.h>` | C89/C99/C23 | String inspection, token scanning, memory manipulation | `strlen()`, `strcmp()`, `strncmp()`, `strchr()`, `strrchr()`, `strstr()`, `strspn()`, `strcspn()`, `strdup()`, `strndup()`, `memcpy()`, `memmove()`, `memset()`, `memcmp()` | All C modules |
| `<ctype.h>` | C89/C99/C23 | Character classification (spaces, digits, punct, alpha) | `isalnum()`, `isalpha()`, `isdigit()`, `isspace()`, `ispunct()`, `isprint()`, `tolower()`, `toupper()` | `tokenizer.c`, `md_parser.c`, `http.c` |
| `<stdbool.h>` | C99/C23 | Standard boolean types | `bool`, `true`, `false` | All C modules |
| `<stdint.h>` | C99/C23 | Fixed-width integer types for predictable sizes | `int8_t`, `uint8_t`, `int32_t`, `uint32_t`, `int64_t`, `uint64_t`, `size_t`, `uintptr_t` | All C modules |
| `<stddef.h>` | C89/C99/C23 | Standard type definitions and offset macros | `size_t`, `ptrdiff_t`, `NULL`, `offsetof()` | All C modules |
| `<errno.h>` | C89/C99/C23 | System and socket error numbers | `errno`, `EINTR`, `EAGAIN`, `EWOULDBLOCK`, `ECONNRESET`, `EPIPE` | `http.c`, `file_writer.c` |
| `<stdarg.h>` | C89/C99/C23 | Variadic arguments for compiler-style error reporting | `va_list`, `va_start()`, `va_arg()`, `va_end()`, `vsnprintf()` | `tokenizer.c`, `md_parser.c`, `test_harness.h` |
| `<assert.h>` | C89/C99/C23 | Runtime diagnostic assertions and invariants | `assert()` | `tests/*`, AST sanity checks |
| `<time.h>` | C89/C99/C23 | Clock timers, HTTP Date headers, PRNG seeding | `time_t`, `struct tm`, `time()`, `clock()`, `strftime()`, `gmtime_r()`, `timespec_get()` | `http.c`, `fuzz_roundtrip.c`, `main.c` |
| `<limits.h>` | C89/C99/C23 | Architecture limits | `PATH_MAX`, `INT_MAX`, `SIZE_MAX` | `file_writer.c`, `main.c` |
| `<signal.h>` | C89/C99/C23 | Signal handling for graceful shutdown & socket pipe safety | `signal()`, `sigaction()`, `SIGINT`, `SIGTERM`, `SIGPIPE`, `SIG_IGN` | `main.c`, `http.c` |

---

## 3. Platform Operating System Headers (Linux POSIX & Windows Win32)

To run as a native desktop application on both **Linux** and **Windows** with **zero third-party dependencies** (no Electron, no Tauri, no external frameworks), we use standard platform system headers:

### A. Linux System Headers (POSIX.1-2008)
| POSIX Header | Standard | Project Purpose | Functions & Structures Used | Module |
|---|---|---|---|---|
| `<sys/socket.h>` | POSIX.1-2008 | Raw Berkeley socket creation and TCP connection handling | `socket()`, `bind()`, `listen()`, `accept()`, `send()`, `recv()`, `setsockopt()`, `SO_REUSEADDR` | `http.c`, `platform.c` |
| `<netinet/in.h>` | POSIX.1-2008 | Internet address structures | `struct sockaddr_in`, `struct in_addr`, `IPPROTO_TCP`, `INADDR_ANY` | `http.c` |
| `<arpa/inet.h>` | POSIX.1-2008 | Byte-order conversion & IP formatting | `htons()`, `ntohs()`, `htonl()`, `ntohl()`, `inet_pton()`, `inet_ntop()` | `http.c` |
| `<unistd.h>` | POSIX.1-2008 | Low-level descriptor I/O, atomic sync, CLI options | `read()`, `write()`, `close()`, `fsync()` *(data durability)*, `getopt()`, `isatty()` | `http.c`, `file_writer.c`, `main.c` |
| `<fcntl.h>` | POSIX.1-2008 | Non-blocking socket configuration & temp file flags | `fcntl()`, `open()`, `O_RDONLY`, `O_WRONLY`, `O_CREAT`, `O_TRUNC`, `O_NONBLOCK`, `F_SETFL` | `http.c`, `file_writer.c` |
| `<sys/stat.h>` | POSIX.1-2008 | Static file inspection for `Content-Length` | `stat()`, `fstat()`, `struct stat`, `S_ISREG()`, `st_size` | `http.c` |
| `<sys/types.h>` | POSIX.1-2008 | POSIX system data types | `ssize_t`, `off_t`, `mode_t`, `pid_t` | `http.c`, `file_writer.c` |
| `<sys/select.h>` / `<poll.h>` | POSIX.1-2008 | Socket connection multiplexing & timeout loops | `select()`, `poll()`, `fd_set`, `struct pollfd` | `http.c` |
| `<dirent.h>` | POSIX.1-2008 | Directory stream reading for test fixture files | `opendir()`, `readdir()`, `closedir()` | `tests/commonmark/run_conformance.c` |
| `<pthread.h>` | POSIX.1-2008 | Multi-threading synchronization for debounced file writes | `pthread_create()`, `pthread_detach()`, `pthread_mutex_t`, `pthread_mutex_init()`, `pthread_mutex_lock()`, `pthread_mutex_unlock()` | `file_writer.c` |


### B. Windows System Headers (Win32 / MSVC / MinGW Built-in)
| Windows Header | Standard SDK | Project Purpose | Functions & Structures Used | Module |
|---|---|---|---|---|
| `<winsock2.h>` | Windows SDK (`ws2_32`) | Windows raw socket networking | `WSAStartup()`, `WSACleanup()`, `socket()`, `bind()`, `listen()`, `accept()`, `closesocket()`, `ioctlsocket()` | `platform.c`, `http.c` |
| `<ws2tcpip.h>` | Windows SDK | Modern TCP/IP addressing on Windows | `sockaddr_in`, `inet_pton()`, `inet_ntop()`, `getaddrinfo()` | `platform.c`, `http.c` |
| `<windows.h>` | Windows SDK | Native Windows desktop app launcher & file persistence | `ShellExecuteA()` *(auto-launch UI window)*, `MoveFileExA(..., MOVEFILE_REPLACE_EXISTING)` *(atomic swap)*, `FlushFileBuffers()` | `platform.c`, `main.c`, `file_writer.c` |
| `<io.h>` / `<direct.h>` | C Runtime on Windows | Low-level file handle checks & directory paths | `_isatty()`, `_fileno()`, `_getcwd()` | `main.c`, `platform.c` |

---

## 4. Browser Native Built-ins (Vanilla Local Frontend)

The client running in the browser uses **zero external libraries and zero remote services**. It relies solely on native browser built-in language features:

- **Local DOM Elements**: `document.getElementById()`, `textarea.value`, `div.innerHTML`, `element.textContent`
- **Native Browser Fetch**: `window.fetch()` communicating **only with `localhost:8080`** (no external networks)
- **Local Timers**: `setTimeout()`, `clearTimeout()` for 200ms input debouncing
- **Native JSON Serialization**: Built-in `JSON.stringify()` and `JSON.parse()`
- **Local Cursor Tracking**: Built-in `window.getSelection()` and `document.createRange()`

---

## 5. Package Killer Substitutions (STDLIB Log)

Every library that developers typically install from third parties is replaced with hand-rolled code using only the standard C library and POSIX headers:

| Typical Third-Party Library | Hand-Rolled C / POSIX Replacement | Standard Headers Used |
|---|---|---|
| `marked` / `cmark` (Markdown parser) | Recursive-descent parser with line/col tracking | `<ctype.h>`, `<string.h>`, `<stdlib.h>` |
| `turndown` / `html2markdown` (HTML to MD) | Scoped DOM-tag walker | `<string.h>`, `<stdio.h>` |
| `express` / `libmicrohttpd` (HTTP server) | Raw Berkeley socket server + HTTP/1.1 parser | `<sys/socket.h>`, `<netinet/in.h>`, `<unistd.h>` |
| `chalk` / `colord` (Terminal colors) | Raw ANSI escape strings (`\033[...]`) + `isatty()` | `<stdio.h>`, `<unistd.h>` |
| `chokidar` / `nodemon` (File watcher) | Debounced HTTP POST + atomic `fsync()` / `rename()` | `<stdio.h>`, `<unistd.h>` |
| `left-pad` (String padding) | Standard memory padding | `<string.h>` (`memset()`), `<stdio.h>` (`snprintf()`) |
| `minimist` / `clap` (CLI argument parser) | Standard POSIX option parser | `<unistd.h>` (`getopt()`) |
| `Unity` / `GoogleTest` (Unit test framework) | Self-contained C assertion macros | `<stdio.h>`, `<assert.h>`, `<stdarg.h>` |
| `libFuzzer` / `AFL` (Fuzzing engine) | Hand-rolled grammar mutation fuzzer | `<stdlib.h>` (`rand()`), `<time.h>` |
| `dotenv` (Environment config) | Standard process environment reader | `<stdlib.h>` (`getenv()`) |

---

## 6. Prohibited Libraries & Techniques

- ❌ **No External / Cloud APIs:** Any network request leaving `localhost` is forbidden.
- ❌ **No 3rd-Party C Libraries:** Boost, `fmt`, `abseil`, `nlohmann/json`, `cJSON`, `stb_*`, `libcurl`, `libevent`.
- ❌ **No Vendoring in `src/`:** Copying outside library source code into `src/` is prohibited.
- ❌ **No Shell Subprocesses:** Calling `system()`, `popen()`, or `exec*()` to run external tools (`pandoc`, `curl`, `node`, `sed`) at runtime is prohibited.
- ❌ **No NPM Dependencies or CDNs:** `package.json` dependencies is `{}` / absent; no CDN `<script>` or `<link>` tags.
