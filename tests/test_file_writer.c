#define _POSIX_C_SOURCE 200809L
#include "test_harness.h"
#include "../src-c/file_writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define sleep_ms(ms) Sleep((DWORD)(ms))
#else
#include <unistd.h>
#include <time.h>
#define sleep_ms(ms) do { \
    struct timespec ts; \
    ts.tv_sec = (ms) / 1000; \
    ts.tv_nsec = ((ms) % 1000) * 1000000; \
    (void)nanosleep(&ts, NULL); \
} while (0)
#endif

TEST_INIT()

extern int g_write_count;
extern bool g_crash_mid_write;

static const char *test_file = "test_atomic.txt";
static const char *orig_data = "ORIGINAL_DATA";
static const char *new_data = "NEW_CRASHED_DATA";

/* Helper to read file content */
static bool read_file_content(const char *path, char *buf, size_t max_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, max_len - 1, f);
    buf[n] = '\0';
    fclose(f);
    return true;
}

/* Helper to write initial content */
static bool write_file_content(const char *path, const char *data) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t len = strlen(data);
    size_t n = fwrite(data, 1, len, f);
    fclose(f);
    return n == len;
}

/* Test 1: Rapid schedule calls collapse into a single write */
bool test_collapse_writes(void) {
    g_write_count = 0;
    (void)remove(test_file);

    /* Trigger 5 rapid saves */
    for (int i = 0; i < 5; i++) {
        file_writer_schedule_save(test_file, "collapse_test", 13);
        sleep_ms(5); /* Sleep slightly to simulate rapid typing */
    }

    /* Wait for debounce timer (up to 1000ms max) to settle */
    int waited = 0;
    while (g_write_count < 1 && waited < 1000) {
        sleep_ms(10);
        waited += 10;
    }

    /* Assert that all 5 calls collapsed into exactly 1 physical write */
    ASSERT_INT_EQ(g_write_count, 1);

    char buf[128];
    ASSERT_TRUE(read_file_content(test_file, buf, sizeof(buf)));
    ASSERT_STR_EQ(buf, "collapse_test");

    (void)remove(test_file);
    return true;
}

/* Test 2: Crash mid-write ensures original file is untouched */
bool test_atomicity_crash(const char *argv0) {
    (void)remove(test_file);
    
    /* Write original data */
    ASSERT_TRUE(write_file_content(test_file, orig_data));

    /* Normalize path separators for Windows cmd.exe compat if needed */
    char norm_argv0[512];
    size_t i = 0;
    for (; argv0[i] != '\0' && i < sizeof(norm_argv0) - 1; i++) {
#ifdef _WIN32
        if (argv0[i] == '/') {
            norm_argv0[i] = '\\';
        } else {
            norm_argv0[i] = argv0[i];
        }
#else
        norm_argv0[i] = argv0[i];
#endif
    }
    norm_argv0[i] = '\0';

    /* Spawn child process with crash flag */
    char cmd[512];
    int written = snprintf(cmd, sizeof(cmd), "\"%s\" --child-crash", norm_argv0);
    ASSERT_TRUE(written > 0 && written < (int)sizeof(cmd));

    /* Run child process. It should exit with status 99 or crash (non-zero) */
    int status = system(cmd);
    ASSERT_TRUE(status != 0);

    /* Verify that the original file was NOT modified/corrupted */
    char buf[128];
    ASSERT_TRUE(read_file_content(test_file, buf, sizeof(buf)));
    ASSERT_STR_EQ(buf, orig_data);

    /* Clean up the temporary file left by the crashed child process */
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), ".%s.tmp", test_file);
    (void)remove(tmp_path);

    (void)remove(test_file);
    return true;
}

/* Test 3: Writing to invalid directories returns false */
bool test_error_path(void) {
    /* Invalid empty path */
    ASSERT_FALSE(file_writer_atomic_write("", "content", 7));
    
    /* Nonexistent directory path */
    ASSERT_FALSE(file_writer_atomic_write("/nonexistent_dir/test.txt", "content", 7));
    
    return true;
}

int main(int argc, char **argv) {
    /* If run with --child-crash, execute a crashed write and exit */
    if (argc > 1 && strcmp(argv[1], "--child-crash") == 0) {
        g_crash_mid_write = true;
        /* Call atomic write; this will trigger partial write and _exit(99) */
        (void)file_writer_atomic_write(test_file, new_data, strlen(new_data));
        return 0;
    }

    RUN_TEST(test_collapse_writes);
    
    /* Pass argv[0] to spawn child process */
    printf("Running test_atomicity_crash... ");
    g_tests_run++;
    if (test_atomicity_crash(argv[0])) {
        printf("\033[1;32mPASSED\033[0m\n");
    } else {
        printf("\033[1;31mFAILED\033[0m\n");
        g_tests_failed++;
    }

    RUN_TEST(test_error_path);

    printf("\nTest Summary: %d run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
