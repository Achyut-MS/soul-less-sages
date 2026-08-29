#ifndef HTTP_H
#define HTTP_H

#include <stdbool.h>

/**
 * @brief Starts and runs the raw HTTP/1.1 socket server loop.
 *
 * Implements raw socket binding, listening, client accept loop, HTTP request-line/header
 * parsing, static file routing, and routing endpoints (/render, /serialize).
 *
 * @param port Port number to bind to (e.g. 8080).
 * @param initial_file Optional path to the initial Markdown file loaded into the editor.
 * @return true if the server ran and shut down gracefully, false on critical socket error.
 */
bool http_server_run(int port, const char *initial_file);

#endif /* HTTP_H */
