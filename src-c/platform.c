#define _POSIX_C_SOURCE 200809L
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

/*
 * Senior Systems Engineer Note:
 * This module implements a clean platform abstraction layer.
 * Non-obvious platform operations (such as Winsock startup, non-blocking ioctl,
 * FlushFileBuffers vs fsync, and atomic shell executions) are fully documented.
 */

bool platform_socket_init(void) {
#ifdef _WIN32
    WSADATA wsa_data;
    /* Winsock2 requires explicit application initialization via WSAStartup */
    return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
#else
    /* POSIX systems share standard file descriptor namespaces; no init needed */
    return true;
#endif
}

void platform_socket_cleanup(void) {
#ifdef _WIN32
    /* Clean up all active socket descriptors and dll registrations on Windows */
    (void)WSACleanup();
#endif
}

platform_socket_t platform_bind_listen(int port) {
    /* Initialize socket over IPv4 TCP STREAM protocol */
    platform_socket_t fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == PLATFORM_INVALID_SOCKET) {
        return PLATFORM_INVALID_SOCKET;
    }

    int opt = 1;
#ifdef _WIN32
    /* SO_REUSEADDR prevents address conflicts during fast server restarts */
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#else
    /* POSIX options use void* pointer for setsockopt values */
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

#ifdef _WIN32
    /* S_un.S_addr is the Winsock struct field to bind loopback IP */
    addr.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);
#else
    /* s_addr is the standard POSIX struct field to bind loopback IP */
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#endif

    /* Bind to local loopback interface for local desktop security */
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
        return PLATFORM_INVALID_SOCKET;
    }

    /* Start listening with standard OS connection backlog queue */
    if (listen(fd, SOMAXCONN) < 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
        return PLATFORM_INVALID_SOCKET;
    }

    return fd;
}

platform_socket_t platform_accept(platform_socket_t server_fd) {
    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);

#ifdef _WIN32
    /* Winsock2 accept uses int* for the address length parameter */
    platform_socket_t client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
#else
    /* POSIX accept uses socklen_t* for the address length parameter */
    platform_socket_t client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&addr_len);
#endif

    return client_fd;
}

bool platform_set_nonblocking(platform_socket_t socket_fd) {
#ifdef _WIN32
    u_long mode = 1;
    /* ioctlsocket with FIONBIO flag is Winsock's method to set non-blocking mode */
    return ioctlsocket(socket_fd, FIONBIO, &mode) == 0;
#else
    /* POSIX retrieves and flags socket file status using fcntl commands */
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool platform_atomic_write(const char *tmp_path, const char *final_path) {
    if (!tmp_path || !final_path) {
        return false;
    }

#ifdef _WIN32
    /* Open the temp file to flush its buffered contents to physical disk */
    HANDLE handle = CreateFileA(tmp_path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    /* FlushFileBuffers forces cached write bytes to physical storage on Windows */
    if (!FlushFileBuffers(handle)) {
        CloseHandle(handle);
        return false;
    }
    CloseHandle(handle);

    /* MoveFileExA with REPLACE and WRITE_THROUGH provides atomic transaction writes on Windows */
    return MoveFileExA(tmp_path, final_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    /* POSIX uses standard open and fsync system calls to sync file contents */
    int fd = open(tmp_path, O_WRONLY);
    if (fd < 0) {
        return false;
    }

    /* fsync forces kernel page caches containing data blocks to sync to disk */
    if (fsync(fd) < 0) {
        close(fd);
        return false;
    }
    close(fd);

    /* rename() is guaranteed to be atomic under POSIX-compliant filesystems */
    return rename(tmp_path, final_path) == 0;
#endif
}

bool platform_open_browser(const char *url) {
    if (!url) {
        return false;
    }

#ifdef _WIN32
    /* ShellExecuteA launches the Windows system default handler for URLs */
    INT_PTR res = (INT_PTR)ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
    if (res > 32) {
        return true;
    }
    printf("Please open this URL manually: %s\n", url);
    return false;
#else
    pid_t pid = fork();
    if (pid < 0) {
        printf("Please open this URL manually: %s\n", url);
        return false;
    } else if (pid == 0) {
        char *args[] = { "xdg-open", (char *)url, NULL };
        (void)execvp("xdg-open", args);
        /* If execvp fails, print fallback message and exit */
        printf("\n=========================================\n");
        printf("Notice: xdg-open could not be executed.\n");
        printf("Please open this URL manually in your browser:\n");
        printf("  %s\n", url);
        printf("=========================================\n\n");
        _exit(127);
    }
    return true;
#endif
}
