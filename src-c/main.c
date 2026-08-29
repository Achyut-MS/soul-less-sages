#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "platform.h"
#include "http.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/*
 * Senior Systems Engineer Notes:
 * 1. CLI Parsing leverages POSIX standard getopt() for clean option extraction (-p port, -h help).
 * 2. Signal handlers are registered for SIGINT/SIGTERM (Linux) and SetConsoleCtrlHandler (Windows)
 *    to trigger graceful shutdown by unblocking the blocking accept() call.
 */

extern volatile sig_atomic_t g_keep_running;
extern platform_socket_t g_server_fd;

#ifdef _WIN32
static BOOL WINAPI handle_win_signal(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT || ctrl_type == CTRL_CLOSE_EVENT) {
        g_keep_running = 0;
        if (g_server_fd != PLATFORM_INVALID_SOCKET) {
            (void)closesocket(g_server_fd);
            g_server_fd = PLATFORM_INVALID_SOCKET;
        }
        return TRUE;
    }
    return FALSE;
}
#else
static void handle_signal(int sig) {
    (void)sig;
    g_keep_running = 0;
    if (g_server_fd != PLATFORM_INVALID_SOCKET) {
        (void)close(g_server_fd);
        g_server_fd = PLATFORM_INVALID_SOCKET;
    }
}
#endif

int main(int argc, char **argv) {
    int port = 8080;
    const char *target_file = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "p:h")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 'h':
                printf("Zero-Dep Markdown Viewer - Native Desktop Engine\n");
                printf("Usage: %s [-p port] [file.md]\n", argv[0]);
                return 0;
            default:
                fprintf(stderr, "Usage: %s [-p port] [file.md]\n", argv[0]);
                return 1;
        }
    }

    if (optind < argc) {
        target_file = argv[optind];
    }

    printf("Starting Zero-Dep Markdown Viewer...\n");
    if (!platform_socket_init()) {
        fprintf(stderr, "Error: Network subsystem initialization failed.\n");
        return 1;
    }

    /* Register signal handlers for graceful shutdown */
#ifdef _WIN32
    if (!SetConsoleCtrlHandler(handle_win_signal, TRUE)) {
        fprintf(stderr, "Warning: Could not set console control handler.\n");
    }
#else
    struct sigaction sa = {
        .sa_handler = handle_signal,
        .sa_flags = 0,
    };
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        fprintf(stderr, "Warning: Could not register SIGINT handler.\n");
    }
    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        fprintf(stderr, "Warning: Could not register SIGTERM handler.\n");
    }
#endif

    /* Running the server handles binding, browser launch, and accept loop */
    bool success = http_server_run(port, target_file);

    platform_socket_cleanup();
    return success ? 0 : 1;
}
