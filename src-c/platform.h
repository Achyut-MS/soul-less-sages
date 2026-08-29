#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Senior Systems Engineer Note:
 * Cross-platform socket and system API abstraction for a zero-dependency architecture.
 *
 * Windows uses Winsock2 where sockets are typed as SOCKET (uintptr_t) and errors are
 * retrieved via WSAGetLastError(). POSIX uses standard file descriptors (int) and errno.
 */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
typedef SOCKET platform_socket_t;
#define PLATFORM_INVALID_SOCKET INVALID_SOCKET
#else
typedef int platform_socket_t;
#define PLATFORM_INVALID_SOCKET (-1)
#endif

/**
 * @brief Initializes the network socket subsystem.
 * @return true on success, false on failure.
 */
bool platform_socket_init(void);

/**
 * @brief Cleans up the network socket subsystem.
 */
void platform_socket_cleanup(void);

/**
 * @brief Binds to the specified port on loopback (127.0.0.1) and starts listening.
 * @param port Port number to bind.
 * @return The server socket handle, or PLATFORM_INVALID_SOCKET on failure.
 */
platform_socket_t platform_bind_listen(int port);

/**
 * @brief Accepts an incoming connection on the listening socket.
 * @param server_fd The listening socket.
 * @return The accepted client socket handle, or PLATFORM_INVALID_SOCKET on failure.
 */
platform_socket_t platform_accept(platform_socket_t server_fd);

/**
 * @brief Sets a socket descriptor to non-blocking mode.
 * @param socket_fd The socket to modify.
 * @return true on success, false on failure.
 */
bool platform_set_nonblocking(platform_socket_t socket_fd);

/**
 * @brief Atomically replaces final_path with tmp_path.
 *
 * Guarantees that final_path is updated durably and atomically. The tmp_path
 * file is flushed to physical storage using OS sync calls before replacement.
 *
 * @param tmp_path Path to the temporary source file.
 * @param final_path Path to the target file to be atomically replaced.
 * @return true on success, false on failure.
 */
bool platform_atomic_write(const char *tmp_path, const char *final_path);

/**
 * @brief Natively opens the system web browser pointing to the URL.
 * @param url The target URL.
 * @return true if launched successfully, false on error.
 */
bool platform_open_browser(const char *url);

#endif /* PLATFORM_H */
