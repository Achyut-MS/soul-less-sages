/*
 * launcher.c — Universal, user-friendly launcher for the SoulessSages
 * Zero-Dep Markdown Editor (replaces run.sh / run.bat).
 *
 * Designed for non-technical users:
 *   * Double-click friendly  — the window never vanishes; every exit
 *     pauses with "Press Enter to close" so messages can be read.
 *   * Self-explaining        — a clear welcome screen shows what is
 *     happening at every step.
 *   * Smart build            — auto-detects make/mingw32-make, reports
 *     a friendly message if no compiler is found.
 *   * Helpful defaults       — opens test_files/notes.md when run with
 *     no argument; `mdlaunch --help` explains usage.
 *   * Gentle failures        — a port-in-use hint, a compiler hint and
 *     a "where to get help" line instead of a bare exit code.
 *
 * Build once, then run from anywhere:
 *   gcc -O2 -o mdlaunch launcher.c        (Linux/macOS)
 *   gcc -O2 -o mdlaunch.exe launcher.c    (Windows / MinGW)
 *
 * Zero dependencies: standard C library only.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define chdir _chdir
#define PATH_SEP '\\'
#define MDVIEW_BIN "src-c\\mdview.exe"
#else
#include <unistd.h>
#define PATH_SEP '/'
#define MDVIEW_BIN "src-c/mdview"
#endif

#define MAX_PATH_LEN 1024

/* ------------------------------------------------------------------ */
/* Tiny console helpers                                                */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
/* Enable ANSI colors on modern Windows terminals; harmless if unsupported. */
static void enable_colors(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        SetConsoleMode(h, mode | 0x0004); /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */
    }
}
#else
static void enable_colors(void) { /* ANSI works out of the box */ }
#endif

#define C_RESET "\033[0m"
#define C_BOLD  "\033[1m"
#define C_OK    "\033[32m"
#define C_INFO  "\033[36m"
#define C_WARN  "\033[33m"
#define C_ERR   "\033[31m"

static void print_banner(void) {
    printf("%s", C_BOLD);
    printf("\n  ============================================\n");
    printf("   SoulessSages - Markdown Editor Launcher\n");
    printf("  ============================================\n");
    printf("%s", C_RESET);
}

static void step(const char *msg)  { printf("  %s[*]%s %s\n", C_INFO,  C_RESET, msg); }
static void step_ok(const char *msg){ printf("  %s[ok]%s %s\n", C_OK,    C_RESET, msg); }
static void warn(const char *msg)  { printf("  %s[!]%s %s\n",  C_WARN,  C_RESET, msg); }
static void fail(const char *msg)  { printf("  %s[x]%s %s\n",  C_ERR,   C_RESET, msg); }

/* Pause so double-clicked windows don't disappear. */
static void pause_for_user(void) {
    printf("\n  Press %sEnter%s to close this window...", C_BOLD, C_RESET);
    int c;
    while ((c = getchar()) != '\n' && c != EOF) { }
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Resolves the directory containing this launcher, so relative paths
 * (src-c/...) work no matter where it is invoked or double-clicked from. */
static bool get_launcher_dir(char *out, size_t out_max) {
#ifdef _WIN32
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;
#else
    char path[MAX_PATH_LEN];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0) return false;
    path[len] = '\0';
#endif
    char *sep = strrchr(path, '/');
#ifdef _WIN32
    char *sep2 = strrchr(path, '\\');
    if (sep2 > sep) sep = sep2;
#endif
    if (!sep) return false;
    *sep = '\0';
    if (strlen(path) >= out_max) return false;
    snprintf(out, out_max, "%s", path);
    return true;
}

/* Returns the name of an available build tool, or NULL. */
static const char *find_build_tool(void) {
#ifdef _WIN32
    if (system("where mingw32-make >nul 2>nul") == 0) return "mingw32-make";
    if (system("where make >nul 2>nul") == 0)         return "make";
#else
    if (system("command -v make >/dev/null 2>&1") == 0) return "make";
#endif
    return NULL;
}

static void print_help(const char *exe) {
    printf("\n  Usage: %s [options] [file.md]\n\n", exe);
    printf("  Options:\n");
    printf("    file.md      Open a specific markdown file (relative or absolute)\n");
    printf("    --help, -h   Show this help\n");
    printf("    --rebuild    Force a fresh build before starting\n");
    printf("\n  With no file argument, the editor opens test_files/notes.md\n");
    printf("  (or starts empty if it does not exist).\n\n");
    printf("  The editor UI opens automatically in your web browser.\n");
    printf("  Close this window or press Ctrl+C to stop the editor.\n\n");
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    enable_colors();
    print_banner();

    const char *exe_name = argv[0];
    bool force_rebuild = false;
    const char *user_file = NULL;

    /* ---- Parse arguments (user friendly, unknown flags never crash) */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help(exe_name);
            return 0;
        } else if (strcmp(argv[i], "--rebuild") == 0) {
            force_rebuild = true;
        } else if (argv[i][0] == '-') {
            warn("Unknown option ignored:");
            printf("       %s (run with --help to see usage)\n", argv[i]);
        } else {
            user_file = argv[i];
        }
    }

    /* ---- Resolve repository root */
    char root[MAX_PATH_LEN];
    if (!get_launcher_dir(root, sizeof(root))) {
        if (!getcwd(root, sizeof(root))) {
            fail("Could not determine the project folder.");
            pause_for_user();
            return 1;
        }
        warn("Could not detect launcher location; using current folder.");
    }
    if (chdir(root) != 0) {
        fail("Could not enter the project folder:");
        printf("       %s\n", root);
        pause_for_user();
        return 1;
    }
    step_ok("Project folder found");

    /* ---- Build (only when needed, unless --rebuild) */
    if (force_rebuild || !file_exists(MDVIEW_BIN)) {
        const char *tool = find_build_tool();
        if (!tool) {
            fail("No build tool found on this computer.");
            printf("\n");
            printf("  To fix this:\n");
#ifdef _WIN32
            printf("    1. Install MinGW-w64 (https://www.mingw-w64.org)\n");
            printf("    2. Make sure mingw32-make is on your PATH\n");
#else
            printf("    1. Install the build tools, e.g.:\n");
            printf("         sudo apt install build-essential    (Ubuntu/Debian)\n");
            printf("         sudo dnf install gcc make           (Fedora)\n");
#endif
            printf("    2. Run this launcher again\n");
            pause_for_user();
            return 1;
        }

        char build_cmd[128];
        snprintf(build_cmd, sizeof(build_cmd), "%s -C src-c", tool);
        step("Building the editor (first run takes a few seconds)...");
        int rc = system(build_cmd);
        if (rc != 0 || !file_exists(MDVIEW_BIN)) {
            fail("The build did not succeed.");
            printf("       Please make sure a C compiler (gcc/clang) is installed.\n");
            pause_for_user();
            return 1;
        }
        step_ok("Build complete");
    } else {
        step_ok("Editor is ready (already built)");
    }

    /* ---- Decide which file to open */
    char default_target[MAX_PATH_LEN];
    const char *target = user_file;
    if (!target) {
        snprintf(default_target, sizeof(default_target),
                 "test_files%cnotes.md", PATH_SEP);
        if (file_exists(default_target)) {
            target = default_target;
        }
    }
    if (target && !file_exists(target)) {
        warn("File not found — starting with an empty note instead:");
        printf("       %s\n", target);
        target = NULL;
    }

    /* ---- Launch */
    printf("\n");
    step("Starting the editor...");
    printf("  %sThe editor will open in your browser automatically.%s\n", C_INFO, C_RESET);
    printf("  %sKeep this window open. Press Ctrl+C (or close it) to quit.%s\n", C_INFO, C_RESET);
    printf("\n");

    char cmd[MAX_PATH_LEN * 2];
    if (target) {
        snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", MDVIEW_BIN, target);
    } else {
        snprintf(cmd, sizeof(cmd), "\"%s\"", MDVIEW_BIN);
    }

    int rc = system(cmd);

    printf("\n");
    if (rc == 0) {
        step_ok("Editor closed. Thanks for using SoulessSages!");
        pause_for_user();
        return 0;
    }

    /* Gentle failure explanations for the two common cases */
    fail("The editor exited unexpectedly.");
    printf("\n");
    printf("  Common causes:\n");
    printf("    - Another program is already using port 8080.\n");
    printf("      Close it, or run the editor with a different port:\n");
    printf("          %s -p 8081\n", MDVIEW_BIN);
    printf("    - The browser could not open automatically.\n");
    printf("      You can still open http://127.0.0.1:8080 manually.\n");
    pause_for_user();
    return 1;
}
