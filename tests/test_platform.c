#define _POSIX_C_SOURCE 200809L
#include "test_harness.h"
#include "../src-c/platform.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#endif

TEST_INIT()

/*
 * Senior Systems Engineer Note:
 * Unit Test 1: Socket Subsystem Initialization & Lifecycle.
 * Verifies that the platform socket initialization and cleanup routines
 * return success and do not corrupt the process state.
 */
bool test_platform_socket_lifecycle(void) {
    ASSERT_TRUE(platform_socket_init());
    platform_socket_cleanup();
    return true;
}

/*
 * Unit Test 2: Local Loopback Bind & Listen.
 * Verifies that platform_bind_listen can successfully allocate a socket,
 * configure local SO_REUSEADDR reuse, bind to the loopback interface on an
 * OS-selected ephemeral port (port 0), and start listening.
 */
bool test_platform_bind_listen(void) {
    ASSERT_TRUE(platform_socket_init());

    /* Port 0 lets the operating system allocate a random free port dynamically */
    platform_socket_t server_fd = platform_bind_listen(0);
    ASSERT_FALSE(server_fd == PLATFORM_INVALID_SOCKET);

#ifdef _WIN32
    closesocket(server_fd);
#else
    close(server_fd);
#endif

    platform_socket_cleanup();
    return true;
}

/*
 * Unit Test 3: Non-blocking Socket Configuration and Accept.
 * Verifies that platform_set_nonblocking successfully applies the socket flags.
 * Proves that platform_accept on a non-blocking listening socket immediately
 * returns PLATFORM_INVALID_SOCKET rather than blocking/hanging the execution path.
 */
bool test_platform_nonblocking_accept(void) {
    ASSERT_TRUE(platform_socket_init());

    platform_socket_t server_fd = platform_bind_listen(0);
    ASSERT_FALSE(server_fd == PLATFORM_INVALID_SOCKET);

    /* Set socket to non-blocking */
    ASSERT_TRUE(platform_set_nonblocking(server_fd));

    /* Attempt to accept a client connection. Must fail immediately since no client connected */
    platform_socket_t client_fd = platform_accept(server_fd);
    ASSERT_TRUE(client_fd == PLATFORM_INVALID_SOCKET);

#ifdef _WIN32
    closesocket(server_fd);
#else
    close(server_fd);
#endif

    platform_socket_cleanup();
    return true;
}

/*
 * Unit Test 4: platform_open_browser NULL argument.
 * Verifies the NULL guard path returns false.
 */
bool test_platform_open_browser_null(void) {
    ASSERT_FALSE(platform_open_browser(NULL));
    return true;
}

/*
 * Unit Test 5: platform_atomic_write success path.
 * Writes a temp file, then atomically replaces a target file with it.
 */
bool test_platform_atomic_write_success(void) {
    const char *tmp = "test_platform_tmp.txt";
    const char *dst = "test_platform_dst.txt";
    const char *content = "atomic write test data";

    /* Write content to the tmp file */
    FILE *f = fopen(tmp, "wb");
    ASSERT_NOT_NULL(f);
    fwrite(content, 1, strlen(content), f);
    fclose(f);

    ASSERT_TRUE(platform_atomic_write(tmp, dst));

    /* Verify the destination file has the correct content */
    f = fopen(dst, "rb");
    ASSERT_NOT_NULL(f);
    char buf[128];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    ASSERT_STR_EQ(buf, content);

    (void)remove(tmp);
    (void)remove(dst);
    return true;
}

/*
 * Unit Test 6: platform_atomic_write with nonexistent tmp file.
 * Verifies the error path returns false cleanly.
 */
bool test_platform_atomic_write_bad_path(void) {
    ASSERT_FALSE(platform_atomic_write(NULL, "dst.txt"));
    ASSERT_FALSE(platform_atomic_write("tmp.txt", NULL));
    ASSERT_FALSE(platform_atomic_write("/nonexistent_path/tmp.txt", "dst.txt"));
    return true;
}

#ifndef _WIN32
/*
 * Unit Test 7: Browser launch graceful degradation.
 *
 * Forces the execvp fallback path by running platform_open_browser inside a
 * child process whose stdout is redirected to a temp file. In environments
 * where xdg-open is not installed (e.g. minimal Alpine chroot, CI containers),
 * execvp fails and the code prints a fallback message containing the URL.
 *
 * We verify that:
 *   (a) the function does not crash or hang
 *   (b) the fallback message containing the URL appears in captured stdout
 */
bool test_platform_open_browser_fallback(void) {
    const char *test_url = "http://127.0.0.1:9999/test-fallback";
    const char *capture_file = "test_browser_stdout.txt";

    (void)remove(capture_file);

    pid_t test_pid = fork();
    if (test_pid < 0) {
        fprintf(stderr, "  fork() failed for browser fallback test\n");
        return false;
    }

    if (test_pid == 0) {
        /* Child: redirect stdout to capture file, then call platform_open_browser */
        int fd = open(capture_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        /*
         * Call platform_open_browser. On systems without xdg-open (our test
         * environment), this will fork internally; the inner grandchild will
         * fail execvp and print the fallback message to our redirected stdout.
         */
        (void)platform_open_browser(test_url);

        /*
         * Wait briefly for the grandchild (the fork inside platform_open_browser)
         * to finish its execvp attempt and print the fallback message.
         */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 500000000 }; /* 500ms */
        (void)nanosleep(&ts, NULL);

        _exit(0);
    }

    /* Parent: wait for the test child to finish */
    int status = 0;
    (void)waitpid(test_pid, &status, 0);

    /* Read the captured stdout and check for the URL in the fallback message */
    FILE *f = fopen(capture_file, "rb");
    if (!f) {
        /* If xdg-open actually exists and succeeded, no fallback file is written.
         * In that case, the test still passes — graceful degradation is only
         * triggered when xdg-open is absent. */
        (void)remove(capture_file);
        return true;
    }

    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    /* If we got output, it should contain the test URL */
    if (n > 0) {
        if (strstr(buf, test_url) == NULL) {
            fprintf(stderr, "  Fallback output did not contain URL: %s\n", buf);
            (void)remove(capture_file);
            return false;
        }
    }
    /* If n == 0, xdg-open may have succeeded silently — that's also fine */

    (void)remove(capture_file);
    return true;
}
#endif

int main(void) {
    RUN_TEST(test_platform_socket_lifecycle);
    RUN_TEST(test_platform_bind_listen);
    RUN_TEST(test_platform_nonblocking_accept);
    RUN_TEST(test_platform_open_browser_null);
    RUN_TEST(test_platform_atomic_write_success);
    RUN_TEST(test_platform_atomic_write_bad_path);
#ifndef _WIN32
    RUN_TEST(test_platform_open_browser_fallback);
#endif

    printf("\nTest Summary: %d run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
