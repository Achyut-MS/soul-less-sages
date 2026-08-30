#define _POSIX_C_SOURCE 200809L
/*
 * platform.h (for reference as requested):
 * =============================================================================
 * #ifndef PLATFORM_H
 * #define PLATFORM_H
 * #include <stdbool.h>
 * #include <stddef.h>
 * #ifdef _WIN32
 * #ifndef WIN32_LEAN_AND_MEAN
 * #define WIN32_LEAN_AND_MEAN
 * #endif
 * #include <winsock2.h>
 * typedef SOCKET platform_socket_t;
 * #define PLATFORM_INVALID_SOCKET INVALID_SOCKET
 * #else
 * typedef int platform_socket_t;
 * #define PLATFORM_INVALID_SOCKET (-1)
 * #endif
 * bool platform_socket_init(void);
 * void platform_socket_cleanup(void);
 * platform_socket_t platform_bind_listen(int port);
 * platform_socket_t platform_accept(platform_socket_t server_fd);
 * bool platform_set_nonblocking(platform_socket_t socket_fd);
 * bool platform_atomic_write(const char *tmp_path, const char *final_path);
 * bool platform_open_browser(const char *url);
 * #endif
 * =============================================================================
 */

#include "http.h"
#include "platform.h"
#include "md_parser.h"
#include "html_serializer.h"
#include "file_writer.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#endif
#include <limits.h>

/* Global variables for graceful shutdown and file tracking */
volatile sig_atomic_t g_keep_running = 1;
platform_socket_t g_server_fd = PLATFORM_INVALID_SOCKET;
static char g_initial_file[512] = {0};

static bool resolve_md_path(const char *user_path, char *out, size_t out_max);
static void url_decode(const char *src, char *out, size_t out_max);
static bool get_query_param(const char *path, const char *key, char *out, size_t out_max);

#define HTTP_HEADER_LIMIT 65536u
#define HTTP_BODY_LIMIT (8u * 1024u * 1024u)
#define HTTP_FILE_LIMIT (8u * 1024u * 1024u)

static char *dup_cstr(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len + 1);
    return out;
}

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#define recv_socket(s, b, l, f) recv(s, b, (int)(l), f)
#define send_socket(s, b, l, f) send(s, b, (int)(l), f)
#define close_socket(s) closesocket(s)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define recv_socket(s, b, l, f) recv(s, b, l, f)
#define send_socket(s, b, l, f) send(s, b, l, f)
#define close_socket(s) close(s)
#endif

/*
 * Senior Network Engineer Notes:
 * 1. Single-threaded synchronous server pattern is chosen for the localhost companion app.
 * 2. Memory buffers are heap-allocated per-request (64KB limits) to protect the C stack.
 * 3. Connection is closed immediately after processing the response (no keep-alive complexity).
 *    "cut here first if over hour 12 per risk register" - Keep-Alive was stripped out to meet 72h budget.
 * 4. Minimal JSON key-value extraction replaces cJSON. Does not support nested structures, arrays, or
 *    arbitrary types; strictly parses {"md": "..."} and {"html": "..."} string fields by locating key
 *    patterns and manually resolving standard backslash string escapes.
 */

/* Extracts JSON string value for a specific key. Unescapes standard JSON character escapes. */
static bool extract_json_string(const char *json, const char *key, char *out, size_t out_max) {
    if (!json || !key || !out || out_max == 0) {
        return false;
    }

    char key_pattern[64];
    int written = snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", key);
    if (written < 0 || written >= (int)sizeof(key_pattern)) {
        return false;
    }

    const char *p = strstr(json, key_pattern);
    if (!p) {
        return false;
    }
    p += strlen(key_pattern);

    /* Skip spaces, colons, and newlines before value */
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if (*p != ':') {
        return false;
    }
    p++;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;

    /* JSON string must start with double quote */
    if (*p != '"') {
        return false;
    }
    p++;

    size_t len = 0;
    while (*p) {
        if (*p == '"') {
            out[len] = '\0';
            return true;
        }

        if (*p == '\\') {
            p++;
            if (!*p) {
                break;
            }
            if (len >= out_max - 1) {
                return false;
            }

            switch (*p) {
                case '"':  out[len++] = '"';  break;
                case '\\': out[len++] = '\\'; break;
                case '/':  out[len++] = '/';  break;
                case 'b':  out[len++] = '\b'; break;
                case 'f':  out[len++] = '\f'; break;
                case 'n':  out[len++] = '\n'; break;
                case 'r':  out[len++] = '\r'; break;
                case 't':  out[len++] = '\t'; break;
                case 'u':
                    /* Skip Unicode escape sequence \uXXXX for simplicity */
                    if (strlen(p) >= 5) {
                        p += 4;
                    }
                    break;
                default:
                    out[len++] = *p;
                    break;
            }
        } else {
            if (len >= out_max - 1) {
                return false;
            }
            out[len++] = *p;
        }
        p++;
    }

    return false;
}

static bool json_append_char(char **buf, size_t *len, size_t *cap, char c) {
    if (*len + 1 >= *cap) {
        size_t new_cap = (*cap < 1024) ? 1024 : (*cap * 2);
        char *new_buf = realloc(*buf, new_cap);
        if (!new_buf) return false;
        *buf = new_buf;
        *cap = new_cap;
    }
    (*buf)[(*len)++] = c;
    return true;
}

static char *extract_json_string_alloc(const char *json, const char *key) {
    char key_pattern[64];
    int written;
    const char *p;
    char *out = NULL;
    size_t len = 0;
    size_t cap = 0;

    if (!json || !key) return NULL;
    written = snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", key);
    if (written < 0 || written >= (int)sizeof(key_pattern)) return NULL;

    p = strstr(json, key_pattern);
    if (!p) return NULL;
    p += strlen(key_pattern);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if (*p != ':') return NULL;
    p++;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if (*p != '"') return NULL;
    p++;

    while (*p) {
        if (*p == '"') {
            if (!json_append_char(&out, &len, &cap, '\0')) {
                free(out);
                return NULL;
            }
            return out;
        }
        if (*p == '\\') {
            p++;
            if (!*p) break;
            switch (*p) {
                case '"':  if (!json_append_char(&out, &len, &cap, '"')) goto fail; break;
                case '\\': if (!json_append_char(&out, &len, &cap, '\\')) goto fail; break;
                case '/':  if (!json_append_char(&out, &len, &cap, '/')) goto fail; break;
                case 'b':  if (!json_append_char(&out, &len, &cap, '\b')) goto fail; break;
                case 'f':  if (!json_append_char(&out, &len, &cap, '\f')) goto fail; break;
                case 'n':  if (!json_append_char(&out, &len, &cap, '\n')) goto fail; break;
                case 'r':  if (!json_append_char(&out, &len, &cap, '\r')) goto fail; break;
                case 't':  if (!json_append_char(&out, &len, &cap, '\t')) goto fail; break;
                case 'u':
                    if (strlen(p) >= 5) p += 4;
                    break;
                default:
                    if (!json_append_char(&out, &len, &cap, *p)) goto fail;
                    break;
            }
        } else {
            if (!json_append_char(&out, &len, &cap, *p)) goto fail;
        }
        p++;
    }

fail:
    free(out);
    return NULL;
}

/* Escapes a standard C string to be written safely inside a JSON double-quoted string */
static void json_escape(const char *src, char *dest, size_t dest_max) {
    if (!src || !dest || dest_max == 0) {
        return;
    }

    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < dest_max - 1; i++) {
        switch (src[i]) {
            case '"':
                if (j + 2 >= dest_max) break;
                dest[j++] = '\\';
                dest[j++] = '"';
                break;
            case '\\':
                if (j + 2 >= dest_max) break;
                dest[j++] = '\\';
                dest[j++] = '\\';
                break;
            case '\n':
                if (j + 2 >= dest_max) break;
                dest[j++] = '\\';
                dest[j++] = 'n';
                break;
            case '\r':
                if (j + 2 >= dest_max) break;
                dest[j++] = '\\';
                dest[j++] = 'r';
                break;
            case '\t':
                if (j + 2 >= dest_max) break;
                dest[j++] = '\\';
                dest[j++] = 't';
                break;
            default:
                if ((unsigned char)src[i] >= 32) {
                    dest[j++] = src[i];
                }
                break;
        }
    }
    dest[j] = '\0';
}

static char *json_escape_alloc(const char *src) {
    char *out = NULL;
    size_t len = 0;
    size_t cap = 0;

    if (!src) src = "";
    for (size_t i = 0; src[i] != '\0'; i++) {
        switch (src[i]) {
            case '"':
                if (!json_append_char(&out, &len, &cap, '\\') || !json_append_char(&out, &len, &cap, '"')) goto fail;
                break;
            case '\\':
                if (!json_append_char(&out, &len, &cap, '\\') || !json_append_char(&out, &len, &cap, '\\')) goto fail;
                break;
            case '\n':
                if (!json_append_char(&out, &len, &cap, '\\') || !json_append_char(&out, &len, &cap, 'n')) goto fail;
                break;
            case '\r':
                if (!json_append_char(&out, &len, &cap, '\\') || !json_append_char(&out, &len, &cap, 'r')) goto fail;
                break;
            case '\t':
                if (!json_append_char(&out, &len, &cap, '\\') || !json_append_char(&out, &len, &cap, 't')) goto fail;
                break;
            default:
                if ((unsigned char)src[i] >= 32 && !json_append_char(&out, &len, &cap, src[i])) goto fail;
                break;
        }
    }
    if (!json_append_char(&out, &len, &cap, '\0')) goto fail;
    return out;

fail:
    free(out);
    return NULL;
}

static char *read_text_file_limit(const char *path, size_t limit, bool *too_large) {
    FILE *f;
    char *buf;
    long size;
    size_t n;

    if (too_large) *too_large = false;
    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    if ((unsigned long)size > limit) {
        if (too_large) *too_large = true;
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (n != (size_t)size) {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    return buf;
}

#ifdef TEST_MODE
bool http_test_extract_json_string(const char *json, const char *key, char *out, size_t out_max) {
    return extract_json_string(json, key, out, out_max);
}

char *http_test_extract_json_string_alloc(const char *json, const char *key) {
    return extract_json_string_alloc(json, key);
}

void http_test_json_escape(const char *src, char *dest, size_t dest_max) {
    json_escape(src, dest, dest_max);
}

char *http_test_json_escape_alloc(const char *src) {
    return json_escape_alloc(src);
}

void http_test_url_decode(const char *src, char *out, size_t out_max) {
    url_decode(src, out, out_max);
}

bool http_test_get_query_param(const char *path, const char *key, char *out, size_t out_max) {
    return get_query_param(path, key, out, out_max);
}

char *http_test_read_text_file_limit(const char *path, size_t limit, bool *too_large) {
    return read_text_file_limit(path, limit, too_large);
}

bool http_test_resolve_md_path(const char *user_path, char *out, size_t out_max) {
    return resolve_md_path(user_path, out, out_max);
}
#endif

/* Helper to send complete HTTP response */
static void send_response(platform_socket_t client_fd, int status_code, const char *status_text, const char *content_type, const char *content) {
    size_t content_len = content ? strlen(content) : 0;
    char header_buf[512];
    int header_len = snprintf(header_buf, sizeof(header_buf),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        status_code, status_text, content_type, content_len);

    if (header_len > 0 && header_len < (int)sizeof(header_buf)) {
        (void)send_socket(client_fd, header_buf, (size_t)header_len, 0);
    }
    if (content_len > 0) {
        (void)send_socket(client_fd, content, content_len, 0);
    }
}

/* Helper to send basic plain text error responses */
static void send_error_response(platform_socket_t client_fd, int status_code, const char *status_text) {
    char body[128];
    int written = snprintf(body, sizeof(body), "%d %s", status_code, status_text);
    if (written > 0 && written < (int)sizeof(body)) {
        send_response(client_fd, status_code, status_text, "text/plain; charset=utf-8", body);
    }
}

static bool get_exe_dir(char *out_dir, size_t max_len) {
    if (!out_dir || max_len == 0) return false;
#ifdef _WIN32
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;
    char *last_slash = strrchr(path, '\\');
    char *last_fslash = strrchr(path, '/');
    char *sep = last_slash > last_fslash ? last_slash : last_fslash;
    if (sep) {
        *sep = '\0';
        size_t slen = strlen(path);
        if (slen >= max_len) return false;
        memcpy(out_dir, path, slen + 1);
        return true;
    }
    return false;
#else
    char path[1024];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len > 0) {
        path[len] = '\0';
        char *last_slash = strrchr(path, '/');
        if (last_slash) {
            *last_slash = '\0';
            size_t slen = strlen(path);
            if (slen >= max_len) return false;
            memcpy(out_dir, path, slen + 1);
            return true;
        }
    }
    return false;
#endif
}

/* Serves files out of static/ folder. Assures no relative path traversal is allowed. */
static bool serve_static_file(platform_socket_t client_fd, const char *path) {
    /* Prevent directory traversal attacks checking for ".." */
    if (strstr(path, "..")) {
        send_error_response(client_fd, 403, "Forbidden");
        return true;
    }

    char filepath[1024];
    FILE *f = NULL;

    /* 1. Try direct path (for images / workspace files) */
    int written = snprintf(filepath, sizeof(filepath), "%s", path);
    if (written > 0 && written < (int)sizeof(filepath)) {
        f = fopen(filepath, "rb");
    }
    /* 2. Try relative src-c/static/ */
    if (!f) {
        written = snprintf(filepath, sizeof(filepath), "src-c/static/%s", path);
        if (written > 0 && written < (int)sizeof(filepath)) {
            f = fopen(filepath, "rb");
        }
    }
    /* 3. Try relative static/ */
    if (!f) {
        written = snprintf(filepath, sizeof(filepath), "static/%s", path);
        if (written > 0 && written < (int)sizeof(filepath)) {
            f = fopen(filepath, "rb");
        }
    }
    /* 4. Try directory of the running binary */
    char exe_dir[512];
    if (!f && get_exe_dir(exe_dir, sizeof(exe_dir))) {
        written = snprintf(filepath, sizeof(filepath), "%s/src-c/static/%s", exe_dir, path);
        if (written > 0 && written < (int)sizeof(filepath)) {
            f = fopen(filepath, "rb");
        }
        if (!f) {
            written = snprintf(filepath, sizeof(filepath), "%s/static/%s", exe_dir, path);
            if (written > 0 && written < (int)sizeof(filepath)) {
                f = fopen(filepath, "rb");
            }
        }
        if (!f) {
            written = snprintf(filepath, sizeof(filepath), "%s/%s", exe_dir, path);
            if (written > 0 && written < (int)sizeof(filepath)) {
                f = fopen(filepath, "rb");
            }
        }
    }

    if (!f) {
        send_error_response(client_fd, 404, "Not Found");
        return true;
    }

    const char *ext = strrchr(path, '.');
    const char *content_type = "application/octet-stream";
    if (ext) {
        if (strcmp(ext, ".html") == 0) content_type = "text/html; charset=utf-8";
        else if (strcmp(ext, ".css") == 0) content_type = "text/css";
        else if (strcmp(ext, ".js") == 0) content_type = "application/javascript";
        else if (strcmp(ext, ".png") == 0) content_type = "image/png";
        else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) content_type = "image/jpeg";
        else if (strcmp(ext, ".gif") == 0) content_type = "image/gif";
        else if (strcmp(ext, ".webp") == 0) content_type = "image/webp";
        else if (strcmp(ext, ".svg") == 0) content_type = "image/svg+xml";
        else if (strcmp(ext, ".ico") == 0) content_type = "image/x-icon";
        else if (strcmp(ext, ".bmp") == 0) content_type = "image/bmp";
    }

    /* Calculate file size to populate Content-Length header */
    (void)fseek(f, 0, SEEK_END);
    long size = ftell(f);
    (void)fseek(f, 0, SEEK_SET);

    char header_buf[256];
    int header_len = snprintf(header_buf, sizeof(header_buf),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n",
        content_type, size);

    if (header_len > 0 && header_len < (int)sizeof(header_buf)) {
        (void)send_socket(client_fd, header_buf, (size_t)header_len, 0);
    }

    char file_buf[4096];
    size_t bytes_read;
    while ((bytes_read = fread(file_buf, 1, sizeof(file_buf), f)) > 0) {
        (void)send_socket(client_fd, file_buf, bytes_read, 0);
    }

    fclose(f);
    return true;
}

/* =====================================================================
 * File browsing helpers (zero-dependency, standard library only)
 * ===================================================================== */

/* Decodes %XX and '+' escapes in a URL query component. */
static void url_decode(const char *src, char *out, size_t out_max) {
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < out_max - 1; i++) {
        if (src[i] == '%' && src[i + 1] != '\0' && src[i + 2] != '\0') {
            char hex[3] = { src[i + 1], src[i + 2], '\0' };
            char *end = NULL;
            long val = strtol(hex, &end, 16);
            if (end && *end == '\0') {
                out[j++] = (char)val;
                i += 2;
                continue;
            }
        } else if (src[i] == '+') {
            out[j++] = ' ';
            continue;
        }
        out[j++] = src[i];
    }
    out[j] = '\0';
}

/* Extracts the value of a query parameter (e.g. "path" in "/file?path=x"). */
static bool get_query_param(const char *path, const char *key, char *out, size_t out_max) {
    const char *q = strchr(path, '?');
    if (!q) return false;
    q++;
    size_t key_len = strlen(key);
    while (*q) {
        if (strncmp(q, key, key_len) == 0 && q[key_len] == '=') {
            const char *val = q + key_len + 1;
            const char *end = strchr(val, '&');
            size_t len = end ? (size_t)(end - val) : strlen(val);
            char raw[512];
            if (len >= sizeof(raw)) return false;
            memcpy(raw, val, len);
            raw[len] = '\0';
            url_decode(raw, out, out_max);
            return true;
        }
        const char *amp = strchr(q, '&');
        if (!amp) break;
        q = amp + 1;
    }
    return false;
}

/*
 * Resolves a user-supplied markdown path to a safe local file.
 * Rules:
 *  - Rejects absolute paths and any '..' component (traversal guard).
 *  - Rejects anything not ending in .md / .markdown.
 *  - Paths are interpreted relative to the server working directory,
 *    which is where the app was launched (like an IDE opening a project).
 */
static bool resolve_md_path(const char *user_path, char *out, size_t out_max) {
    if (!user_path || user_path[0] == '\0') return false;
    if (user_path[0] == '/' || user_path[0] == '\\') return false;
    if (isalpha((unsigned char)user_path[0]) && user_path[1] == ':') return false;
    if (strstr(user_path, "..")) return false;
    size_t len = strlen(user_path);
    bool is_md = (len > 3 && strcmp(user_path + len - 3, ".md") == 0) ||
                 (len > 9 && strcmp(user_path + len - 9, ".markdown") == 0);
    if (!is_md) return false;
    int written = snprintf(out, out_max, "%s", user_path);
    return written > 0 && (size_t)written < out_max;
}

/* Lists all .md / .markdown files in the current working directory as JSON. */
#define FILES_JSON_MAX 65536
static void handle_list_files(platform_socket_t client_fd) {
    char *resp_json = malloc(FILES_JSON_MAX);
    if (!resp_json) {
        send_error_response(client_fd, 500, "Internal Server Error");
        return;
    }
    size_t j = 0;
    resp_json[j++] = '[';

#ifdef _WIN32
    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA("./*", &find_data);
    if (hFind != INVALID_HANDLE_VALUE) {
        bool first = true;
        do {
            if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                const char *name = find_data.cFileName;
                size_t nlen = strlen(name);
                bool is_md = false;
                if (nlen > 3 && strcmp(name + nlen - 3, ".md") == 0) is_md = true;
                if (nlen > 9 && strcmp(name + nlen - 9, ".markdown") == 0) is_md = true;
                if (!is_md) continue;

                ULARGE_INTEGER size;
                size.LowPart = find_data.nFileSizeLow;
                size.HighPart = find_data.nFileSizeHigh;

                ULARGE_INTEGER ull;
                ull.LowPart = find_data.ftLastWriteTime.dwLowDateTime;
                ull.HighPart = find_data.ftLastWriteTime.dwHighDateTime;
                time_t mtime = (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);

                char esc_name[1024];
                json_escape(name, esc_name, sizeof(esc_name));
                int written = snprintf(resp_json + j, FILES_JSON_MAX - j,
                    "%s{\"name\":\"%s\",\"size\":%ld,\"mtime\":%ld}",
                    first ? "" : ",", esc_name, (long)size.QuadPart, (long)mtime);
                if (written < 0) break;
                j += (size_t)written;
                first = false;
            }
        } while (FindNextFileA(hFind, &find_data) && j < FILES_JSON_MAX - 512);
        FindClose(hFind);
    }
#else
    DIR *dir = opendir(".");
    if (dir) {
        struct dirent *entry;
        bool first = true;
        while ((entry = readdir(dir)) != NULL && j < FILES_JSON_MAX - 512) {
            const char *name = entry->d_name;
            size_t nlen = strlen(name);
            bool is_md = false;
            if (nlen > 3 && strcmp(name + nlen - 3, ".md") == 0) is_md = true;
            if (nlen > 9 && strcmp(name + nlen - 9, ".markdown") == 0) is_md = true;
            if (!is_md) continue;

            struct stat st;
            if (stat(name, &st) != 0 || !S_ISREG(st.st_mode)) continue;

            char esc_name[1024];
            json_escape(name, esc_name, sizeof(esc_name));
            int written = snprintf(resp_json + j, FILES_JSON_MAX - j,
                "%s{\"name\":\"%s\",\"size\":%ld,\"mtime\":%ld}",
                first ? "" : ",", esc_name, (long)st.st_size, (long)st.st_mtime);
            if (written < 0) break;
            j += (size_t)written;
            first = false;
        }
        closedir(dir);
    }
#endif

    if (j < FILES_JSON_MAX - 2) {
        resp_json[j++] = ']';
        resp_json[j] = '\0';
    } else {
        resp_json[0] = '[';
        resp_json[1] = ']';
        resp_json[2] = '\0';
    }
    send_response(client_fd, 200, "OK", "application/json", resp_json);
    free(resp_json);
}

/* GET /file?path=note.md — loads a specific markdown file from the working dir.
 * With no path, falls back to the initial CLI file (existing behavior). */
static void handle_get_file(platform_socket_t client_fd, const char *path) {
    char user_path[512];
    char resolved[512];

    bool has_explicit = get_query_param(path, "path", user_path, sizeof(user_path));
    const char *target = g_initial_file;

    if (has_explicit && user_path[0] != '\0') {
        if (!resolve_md_path(user_path, resolved, sizeof(resolved))) {
            send_response(client_fd, 403, "Forbidden", "application/json",
                "{\"error\":\"invalid or unsafe markdown path\"}");
            return;
        }
        target = resolved;
    }

    char *file_content = dup_cstr("");
    char *esc_content = NULL;
    char *esc_fname = NULL;
    char *resp_json = NULL;
    bool too_large = false;

    if (!file_content) {
        send_error_response(client_fd, 500, "Internal Server Error");
        return;
    }
    if (target && target[0] != '\0') {
        char *loaded = read_text_file_limit(target, HTTP_FILE_LIMIT, &too_large);
        if (too_large) {
            free(file_content);
            send_response(client_fd, 413, "Payload Too Large", "application/json",
                "{\"error\":\"markdown file exceeds 8 MiB limit\"}");
            return;
        }
        if (loaded) {
            free(file_content);
            file_content = loaded;
        }
    }

    esc_content = json_escape_alloc(file_content);
    esc_fname = json_escape_alloc(target ? target : "");
    if (!esc_content || !esc_fname) {
        send_error_response(client_fd, 500, "Internal Server Error");
        free(file_content); free(esc_content); free(esc_fname);
        return;
    }

    size_t resp_len = strlen(esc_content) + strlen(esc_fname) + 32;
    resp_json = malloc(resp_len);
    if (!resp_json) {
        send_error_response(client_fd, 500, "Internal Server Error");
        free(file_content); free(esc_content); free(esc_fname);
        return;
    }
    (void)snprintf(resp_json, resp_len, "{\"content\":\"%s\",\"filename\":\"%s\"}", esc_content, esc_fname);
    send_response(client_fd, 200, "OK", "application/json", resp_json);

    free(file_content); free(esc_content); free(esc_fname); free(resp_json);
}

/* Receives and routes a client connection */
static void process_client(platform_socket_t client_fd) {
    size_t req_cap = 8192;
    char *req_buf = malloc(req_cap);
    if (!req_buf) {
        send_error_response(client_fd, 500, "Internal Server Error");
        return;
    }

    size_t total_read = 0;
    size_t header_end_idx = 0;
    bool has_header_end = false;

    /* Read header bytes until we encounter the header-end sequence \r\n\r\n */
    while (total_read < HTTP_HEADER_LIMIT) {
        if (total_read + 4096 + 1 > req_cap) {
            size_t new_cap = req_cap * 2;
            char *new_buf;
            if (new_cap > HTTP_HEADER_LIMIT + 1) new_cap = HTTP_HEADER_LIMIT + 1;
            new_buf = realloc(req_buf, new_cap);
            if (!new_buf) {
                send_error_response(client_fd, 500, "Internal Server Error");
                free(req_buf);
                return;
            }
            req_buf = new_buf;
            req_cap = new_cap;
        }
        int n = recv_socket(client_fd, req_buf + total_read, req_cap - total_read - 1, 0);
        if (n <= 0) {
            break;
        }
        total_read += (size_t)n;
        req_buf[total_read] = '\0';

        char *end = strstr(req_buf, "\r\n\r\n");
        if (end) {
            header_end_idx = (size_t)(end - req_buf);
            has_header_end = true;
            break;
        }
    }

    if (!has_header_end) {
        send_error_response(client_fd, 400, "Bad Request (Header Missing Delimiters)");
        free(req_buf);
        return;
    }

    /* Extract Content-Length to determine if a body payload must be fetched */
    size_t content_len = 0;
    char temp_char = req_buf[header_end_idx];
    req_buf[header_end_idx] = '\0'; /* Temporarily terminate headers to scan safely */

    const char *cl_ptr = strstr(req_buf, "Content-Length:");
    if (!cl_ptr) cl_ptr = strstr(req_buf, "content-length:");
    if (!cl_ptr) cl_ptr = strstr(req_buf, "Content-length:");
    if (cl_ptr) {
        char *endptr = NULL;
        cl_ptr += 15;
        while (*cl_ptr == ' ' || *cl_ptr == '\t') cl_ptr++;
        content_len = (size_t)strtoull(cl_ptr, &endptr, 10);
        if (endptr == cl_ptr) {
            send_error_response(client_fd, 400, "Bad Request (Invalid Content-Length)");
            free(req_buf);
            return;
        }
    }
    req_buf[header_end_idx] = temp_char; /* Restore original byte */

    if (content_len > HTTP_BODY_LIMIT || header_end_idx + 4 + content_len + 1 < content_len) {
        send_error_response(client_fd, 413, "Request Entity Too Large");
        free(req_buf);
        return;
    }

    size_t expected_total = header_end_idx + 4 + content_len;
    if (expected_total + 1 > req_cap) {
        char *new_buf = realloc(req_buf, expected_total + 1);
        if (!new_buf) {
            send_error_response(client_fd, 500, "Internal Server Error");
            free(req_buf);
            return;
        }
        req_buf = new_buf;
        req_cap = expected_total + 1;
    }

    /* Read remainder of request body bytes if they weren't read in the header scan */
    while (total_read < expected_total) {
        int n = recv_socket(client_fd, req_buf + total_read, expected_total - total_read, 0);
        if (n <= 0) {
            break;
        }
        total_read += (size_t)n;
    }
    req_buf[total_read] = '\0';

    char method[16] = {0};
    char path[256] = {0};
    char version[16] = {0};
    if (sscanf(req_buf, "%15s %255s %15s", method, path, version) < 2) {
        send_error_response(client_fd, 400, "Bad Request (Malformed HTTP Request Line)");
        free(req_buf);
        return;
    }

    const char *body = req_buf + header_end_idx + 4;

    /* Endpoint Router */
    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/") == 0) {
            serve_static_file(client_fd, "index.html");
        } else if (strcmp(path, "/files") == 0) {
            handle_list_files(client_fd);
            free(req_buf);
            return;
        } else if (strncmp(path, "/file", 5) == 0 && (path[5] == '\0' || path[5] == '?')) {
            handle_get_file(client_fd, path);
            free(req_buf);
            return;
        } else if (strncmp(path, "/static/", 8) == 0) {
            serve_static_file(client_fd, path + 8);
        } else if (path[0] == '/' && path[1] != '\0' && !strstr(path, "..")) {
            const char *clean_path = path + 1;
            const char *q = strchr(clean_path, '?');
            char file_only[512];
            if (q) {
                size_t flen = (size_t)(q - clean_path);
                if (flen < sizeof(file_only)) {
                    memcpy(file_only, clean_path, flen);
                    file_only[flen] = '\0';
                    clean_path = file_only;
                }
            }
            serve_static_file(client_fd, clean_path);
        } else {
            send_error_response(client_fd, 404, "Not Found");
        }
    } else if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/render") == 0) {
            char *md_buf = extract_json_string_alloc(body, "md");
            if (!md_buf) {
                send_response(client_fd, 400, "Bad Request", "application/json", "{\"error\":\"invalid or missing 'md' field in JSON\"}");
                free(req_buf);
                return;
            }

            if (g_initial_file[0] != '\0') {
                file_writer_schedule_save(g_initial_file, md_buf, strlen(md_buf));
            }

            md_parse_result_t res = md_to_html(md_buf, strlen(md_buf));
            if (res.success) {
                send_response(client_fd, 200, "OK", "text/html; charset=utf-8", res.html);
            } else {
                char *esc_err = malloc(65536);
                char *esc_snip = malloc(65536);
                char *resp_json = malloc(131072);

                if (esc_err && esc_snip && resp_json) {
                    json_escape(res.error_msg ? res.error_msg : "", esc_err, 65536);
                    json_escape(res.caret_snippet ? res.caret_snippet : "", esc_snip, 65536);
                    (void)snprintf(resp_json, 131072, "{\"error\":\"%s\",\"snippet\":\"%s\"}", esc_err, esc_snip);
                    send_response(client_fd, 400, "Bad Request", "application/json", resp_json);
                } else {
                    send_error_response(client_fd, 500, "Internal Server Error");
                }

                free(esc_err);
                free(esc_snip);
                free(resp_json);
            }

            md_parse_result_free(&res);
            free(md_buf);
        } else if (strcmp(path, "/serialize") == 0) {
            char *html_buf = extract_json_string_alloc(body, "html");
            if (!html_buf) {
                send_response(client_fd, 400, "Bad Request", "application/json", "{\"error\":\"invalid or missing 'html' field in JSON\"}");
                free(req_buf);
                return;
            }

            html_serialize_result_t res = html_to_md(html_buf, strlen(html_buf));
            if (res.success) {
                if (res.markdown && g_initial_file[0] != '\0') {
                    file_writer_schedule_save(g_initial_file, res.markdown, strlen(res.markdown));
                }

                char *esc_md = json_escape_alloc(res.markdown ? res.markdown : "");
                size_t resp_len = esc_md ? strlen(esc_md) + 10 : 0;
                char *resp_json = esc_md ? malloc(resp_len) : NULL;
                if (esc_md && resp_json) {
                    (void)snprintf(resp_json, resp_len, "{\"md\":\"%s\"}", esc_md);
                    send_response(client_fd, 200, "OK", "application/json", resp_json);
                } else {
                    send_error_response(client_fd, 500, "Internal Server Error");
                }
                free(esc_md);
                free(resp_json);
            } else {
                char *esc_err = malloc(65536);
                char *resp_json = malloc(65536);
                if (esc_err && resp_json) {
                    json_escape(res.error_msg ? res.error_msg : "", esc_err, 65536);
                    (void)snprintf(resp_json, 65536, "{\"error\":\"%s\"}", esc_err);
                    send_response(client_fd, 400, "Bad Request", "application/json", resp_json);
                } else {
                    send_error_response(client_fd, 500, "Internal Server Error");
                }
                free(esc_err);
                free(resp_json);
            }

            html_serialize_result_free(&res);
            free(html_buf);
        } else if (strcmp(path, "/open-file") == 0) {
            char open_path[512];
            if (!extract_json_string(body, "path", open_path, sizeof(open_path)) ||
                !resolve_md_path(open_path, g_initial_file, sizeof(g_initial_file))) {
                send_response(client_fd, 403, "Forbidden", "application/json",
                    "{\"error\":\"invalid or unsafe markdown path\"}");
                free(req_buf);
                return;
            }
            printf("[HTTP] Active markdown file switched to: %s\n", g_initial_file);
            {
                char esc_open_name[1024];
                char open_resp[1280];
                json_escape(g_initial_file, esc_open_name, sizeof(esc_open_name));
                (void)snprintf(open_resp, sizeof(open_resp),
                    "{\"status\":\"ok\",\"filename\":\"%s\"}", esc_open_name);
                send_response(client_fd, 200, "OK", "application/json", open_resp);
            }
            free(req_buf);
            return;
        } else if (strcmp(path, "/upload-file") == 0) {
            char up_name[256];
            char *up_content = NULL;
            char *resp_json = malloc(1024);

            if (!resp_json) {
                send_error_response(client_fd, 500, "Internal Server Error");
                free(resp_json);
                free(req_buf);
                return;
            }

            if (!extract_json_string(body, "name", up_name, sizeof(up_name))) {
                send_response(client_fd, 400, "Bad Request", "application/json",
                    "{\"error\":\"missing 'name' field\"}");
                free(resp_json);
                free(req_buf);
                return;
            }
            up_content = extract_json_string_alloc(body, "content");
            if (!up_content) {
                send_response(client_fd, 400, "Bad Request", "application/json",
                    "{\"error\":\"missing or invalid 'content' field\"}");
                free(resp_json);
                free(req_buf);
                return;
            }

            /* Only keep the base name (strip any folder components) and
             * validate it is a safe markdown filename. */
            const char *base = up_name;
            for (const char *p = up_name; *p; p++) {
                if (*p == '/' || *p == '\\') base = p + 1;
            }
            if (!resolve_md_path(base, g_initial_file, sizeof(g_initial_file))) {
                send_response(client_fd, 403, "Forbidden", "application/json",
                    "{\"error\":\"only .md / .markdown files can be uploaded\"}");
                free(up_content); free(resp_json);
                free(req_buf);
                return;
            }

            /* Save the uploaded file into the launch directory and make it
             * the active file so subsequent edits autosave to it. */
            file_writer_schedule_save(g_initial_file, up_content, strlen(up_content));
            printf("[HTTP] Uploaded file saved as: %s (%zu bytes)\n",
                g_initial_file, strlen(up_content));

            (void)snprintf(resp_json, 1024, "{\"status\":\"ok\",\"filename\":\"%s\"}", base);
            send_response(client_fd, 200, "OK", "application/json", resp_json);
            free(up_content);
            free(resp_json);
            free(req_buf);
            return;
        } else if (strcmp(path, "/save") == 0) {
            char *save_buf = extract_json_string_alloc(body, "content");
            if (!save_buf) {
                send_response(client_fd, 400, "Bad Request", "application/json", "{\"error\":\"invalid or missing 'content' field in JSON\"}");
                free(req_buf);
                return;
            }

            if (g_initial_file[0] != '\0') {
                file_writer_schedule_save(g_initial_file, save_buf, strlen(save_buf));
            }

            send_response(client_fd, 200, "OK", "application/json", "{\"status\":\"ok\"}");
            free(save_buf);
#ifdef TEST_MODE
        } else if (strcmp(path, "/shutdown") == 0) {
            g_keep_running = 0;
            send_response(client_fd, 200, "OK", "application/json", "{\"status\":\"shutdown\"}");
#endif
        } else {
            send_error_response(client_fd, 404, "Not Found");
        }
    } else {
        send_error_response(client_fd, 400, "Bad Request (Method Not Supported)");
    }

    free(req_buf);
}

#ifdef TEST_MODE
void http_test_process_client(platform_socket_t client_fd) {
    process_client(client_fd);
}

void http_test_set_initial_file(const char *file) {
    if (file) {
        (void)snprintf(g_initial_file, sizeof(g_initial_file), "%s", file);
    } else {
        g_initial_file[0] = '\0';
    }
}
#endif

bool http_server_run(int port, const char *initial_file) {
    if (initial_file) {
        (void)snprintf(g_initial_file, sizeof(g_initial_file), "%s", initial_file);
    } else {
        g_initial_file[0] = '\0';
    }

    platform_socket_t server_fd = platform_bind_listen(port);
    if (server_fd == PLATFORM_INVALID_SOCKET) {
        fprintf(stderr, "Error: HTTP Server failed to bind to port %d\n", port);
        return false;
    }

    /* If port 0 was passed, query the OS-allocated dynamic ephemeral port */
    int actual_port = port;
    if (port == 0) {
        struct sockaddr_in addr;
#ifdef _WIN32
        int addr_len = sizeof(addr);
#else
        socklen_t addr_len = sizeof(addr);
#endif
        if (getsockname(server_fd, (struct sockaddr *)&addr, &addr_len) == 0) {
            actual_port = ntohs(addr.sin_port);
        }
    }

    char url[64];
    (void)snprintf(url, sizeof(url), "http://127.0.0.1:%d", actual_port);
    printf("[HTTP] Raw socket server running at %s\n", url);
    if (initial_file) {
        printf("[HTTP] Working on Markdown file: %s\n", initial_file);
    }

    /* Auto-launch system browser pointing to the editor companion server */
    if (!platform_open_browser(url)) {
        printf("Notice: Could not automatically launch default browser. Please open manually.\n");
    }

    g_server_fd = server_fd;

    while (g_keep_running) {
        platform_socket_t client_fd = platform_accept(server_fd);
        if (client_fd == PLATFORM_INVALID_SOCKET) {
            /* Check if accept failed due to signal interruption or socket shutdown */
            if (!g_keep_running) {
                break;
            }
            continue;
        }

        process_client(client_fd);
        close_socket(client_fd);
    }

#ifdef _WIN32
    closesocket(server_fd);
#else
    close(server_fd);
#endif
    g_server_fd = PLATFORM_INVALID_SOCKET;

    printf("[HTTP] Server shut down gracefully.\n");
    return true;
}
