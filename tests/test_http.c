#define _POSIX_C_SOURCE 200809L
#include "test_harness.h"
#include "../src-c/platform.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#define recv_sock(s, b, l, f) recv(s, b, (int)(l), f)
#define send_sock(s, b, l, f) send(s, b, (int)(l), f)
#define close_sock(s) closesocket(s)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define recv_sock(s, b, l, f) recv(s, b, l, f)
#define send_sock(s, b, l, f) send(s, b, l, f)
#define close_sock(s) close(s)
#endif

bool http_test_extract_json_string(const char *json, const char *key, char *out, size_t out_max);
char *http_test_extract_json_string_alloc(const char *json, const char *key);
void http_test_json_escape(const char *src, char *dest, size_t dest_max);
char *http_test_json_escape_alloc(const char *src);
void http_test_url_decode(const char *src, char *out, size_t out_max);
bool http_test_get_query_param(const char *path, const char *key, char *out, size_t out_max);
char *http_test_read_text_file_limit(const char *path, size_t limit, bool *too_large);
bool http_test_resolve_md_path(const char *user_path, char *out, size_t out_max);
void http_test_process_client(platform_socket_t client_fd);
void http_test_set_initial_file(const char *file);

TEST_INIT()

static bool make_socket_pair(platform_socket_t sv[2]) {
#ifdef _WIN32
    platform_socket_t listener = platform_bind_listen(0);
    if (listener == PLATFORM_INVALID_SOCKET) return false;
    struct sockaddr_in addr;
    int addr_len = sizeof(addr);
    if (getsockname(listener, (struct sockaddr *)&addr, &addr_len) != 0) {
        closesocket(listener);
        return false;
    }
    platform_socket_t client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == PLATFORM_INVALID_SOCKET) {
        closesocket(listener);
        return false;
    }
    if (connect(client, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        closesocket(client);
        closesocket(listener);
        return false;
    }
    platform_socket_t server = platform_accept(listener);
    closesocket(listener);
    if (server == PLATFORM_INVALID_SOCKET) {
        closesocket(client);
        return false;
    }
    sv[0] = server;
    sv[1] = client;
    return true;
#else
    return socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0;
#endif
}

static char *http_exchange(const char *request) {
    platform_socket_t sv[2];
    if (!make_socket_pair(sv)) {
        return NULL;
    }

    size_t req_len = strlen(request);
    size_t total_sent = 0;
    while (total_sent < req_len) {
        int n = send_sock(sv[1], request + total_sent, req_len - total_sent, 0);
        if (n <= 0) break;
        total_sent += (size_t)n;
    }
#ifdef _WIN32
    shutdown(sv[1], SD_SEND);
#else
    shutdown(sv[1], SHUT_WR);
#endif

    /* Process the request on the server side */
    http_test_process_client(sv[0]);

    /* Read the response from the client end */
    size_t cap = 8192;
    char *resp = malloc(cap);
    if (!resp) {
        close_sock(sv[0]);
        close_sock(sv[1]);
        return NULL;
    }

    size_t total_read = 0;
    while (1) {
        if (total_read + 2048 >= cap) {
            cap *= 2;
            char *new_resp = realloc(resp, cap);
            if (!new_resp) {
                free(resp);
                close_sock(sv[0]);
                close_sock(sv[1]);
                return NULL;
            }
            resp = new_resp;
        }
        int n = recv_sock(sv[1], resp + total_read, cap - total_read - 1, 0);
        if (n <= 0) break;
        total_read += (size_t)n;
        resp[total_read] = '\0';
        if (strstr(resp, "\r\n\r\n") != NULL) {
            const char *cl = strstr(resp, "Content-Length:");
            if (!cl) cl = strstr(resp, "content-length:");
            if (cl) {
                cl += 15;
                while (*cl == ' ' || *cl == '\t') cl++;
                size_t expected_body = (size_t)strtoul(cl, NULL, 10);
                char *body_start = strstr(resp, "\r\n\r\n") + 4;
                size_t current_body = total_read - (size_t)(body_start - resp);
                if (current_body >= expected_body) {
                    break;
                }
            }
        }
    }
    resp[total_read] = '\0';

    close_sock(sv[0]);
    close_sock(sv[1]);
    return resp;
}

static bool test_extract_json_string(void) {
    char out[256];

    /* Basic key extraction */
    ASSERT_TRUE(http_test_extract_json_string("{\"md\": \"# Hello World\"}", "md", out, sizeof(out)));
    ASSERT_STR_EQ(out, "# Hello World");

    /* Whitespace and escapes */
    const char *json_esc = "{\n  \"text\" : \"line1\\nline2\\t\\\"quotes\\\"\\\\slash\\/bell\\bform\\ffeed\\u0041\" \n}";
    ASSERT_TRUE(http_test_extract_json_string(json_esc, "text", out, sizeof(out)));
    ASSERT_TRUE(strstr(out, "line1\nline2\t\"quotes\"\\slash/bell\bform\ffeed") != NULL);

    /* Missing key */
    ASSERT_FALSE(http_test_extract_json_string("{\"html\": \"<p>Hi</p>\"}", "md", out, sizeof(out)));

    /* NULL / invalid inputs */
    ASSERT_FALSE(http_test_extract_json_string(NULL, "k", out, sizeof(out)));
    ASSERT_FALSE(http_test_extract_json_string("{}", NULL, out, sizeof(out)));
    ASSERT_FALSE(http_test_extract_json_string("{}", "k", NULL, sizeof(out)));
    ASSERT_FALSE(http_test_extract_json_string("{}", "k", out, 0));

    /* Buffer boundary */
    ASSERT_FALSE(http_test_extract_json_string("{\"k\": \"toolongforbuffer\"}", "k", out, 5));

    /* Non-string values */
    ASSERT_FALSE(http_test_extract_json_string("{\"k\": 12345}", "k", out, sizeof(out)));
    ASSERT_FALSE(http_test_extract_json_string("{\"k\": true}", "k", out, sizeof(out)));
    ASSERT_FALSE(http_test_extract_json_string("{\"k\": [1,2]}", "k", out, sizeof(out)));

    return true;
}

static bool test_extract_json_string_alloc(void) {
    char *val = http_test_extract_json_string_alloc("{\"name\": \"doc.md\"}", "name");
    ASSERT_NOT_NULL(val);
    ASSERT_STR_EQ(val, "doc.md");
    free(val);

    val = http_test_extract_json_string_alloc("{\"empty\": \"\"}", "empty");
    ASSERT_NOT_NULL(val);
    ASSERT_STR_EQ(val, "");
    free(val);

    val = http_test_extract_json_string_alloc("{\"k\": \"\\\"escaped\\\"\"}", "k");
    ASSERT_NOT_NULL(val);
    ASSERT_STR_EQ(val, "\"escaped\"");
    free(val);

    ASSERT_NULL(http_test_extract_json_string_alloc("{\"other\": 1}", "k"));
    ASSERT_NULL(http_test_extract_json_string_alloc(NULL, "k"));
    ASSERT_NULL(http_test_extract_json_string_alloc("{}", NULL));

    return true;
}

static bool test_json_escape(void) {
    char buf[256];

    http_test_json_escape("Hello \"World\"\n\r\t\\", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Hello \\\"World\\\"\\n\\r\\t\\\\");

    /* Null and empty guards */
    http_test_json_escape(NULL, buf, sizeof(buf));
    http_test_json_escape("test", NULL, sizeof(buf));
    http_test_json_escape("test", buf, 0);

    /* Alloc version */
    char *esc = http_test_json_escape_alloc("Special: \n\t\"\\");
    ASSERT_NOT_NULL(esc);
    ASSERT_STR_EQ(esc, "Special: \\n\\t\\\"\\\\");
    free(esc);

    esc = http_test_json_escape_alloc(NULL);
    ASSERT_NOT_NULL(esc);
    ASSERT_STR_EQ(esc, "");
    free(esc);

    return true;
}

static bool test_url_decode(void) {
    char out[128];

    http_test_url_decode("hello+world", out, sizeof(out));
    ASSERT_STR_EQ(out, "hello world");

    http_test_url_decode("file%20name%2Emd", out, sizeof(out));
    ASSERT_STR_EQ(out, "file name.md");

    http_test_url_decode("%41%42%43", out, sizeof(out));
    ASSERT_STR_EQ(out, "ABC");

    /* Malformed % sequences should not crash */
    http_test_url_decode("test%ZZ%2", out, sizeof(out));
    ASSERT_TRUE(strlen(out) > 0);

    return true;
}

static bool test_get_query_param(void) {
    char val[128];

    ASSERT_TRUE(http_test_get_query_param("/file?path=readme.md", "path", val, sizeof(val)));
    ASSERT_STR_EQ(val, "readme.md");

    ASSERT_TRUE(http_test_get_query_param("/file?a=1&path=my%20note.md&b=2", "path", val, sizeof(val)));
    ASSERT_STR_EQ(val, "my note.md");

    ASSERT_FALSE(http_test_get_query_param("/file?a=1&b=2", "path", val, sizeof(val)));
    ASSERT_FALSE(http_test_get_query_param("/file", "path", val, sizeof(val)));

    return true;
}

static bool test_resolve_md_path(void) {
    char out[256];

    ASSERT_TRUE(http_test_resolve_md_path("test.md", out, sizeof(out)));
    ASSERT_STR_EQ(out, "test.md");

    ASSERT_TRUE(http_test_resolve_md_path("folder/nested.markdown", out, sizeof(out)));
    ASSERT_STR_EQ(out, "folder/nested.markdown");

    ASSERT_FALSE(http_test_resolve_md_path("/abs/path.md", out, sizeof(out)));
    ASSERT_FALSE(http_test_resolve_md_path("\\win\\path.md", out, sizeof(out)));
    ASSERT_FALSE(http_test_resolve_md_path("C:\\win.md", out, sizeof(out)));
    ASSERT_FALSE(http_test_resolve_md_path("d:/win.md", out, sizeof(out)));
    ASSERT_FALSE(http_test_resolve_md_path("path/../secret.md", out, sizeof(out)));
    ASSERT_FALSE(http_test_resolve_md_path("image.png", out, sizeof(out)));
    ASSERT_FALSE(http_test_resolve_md_path("text.txt", out, sizeof(out)));
    ASSERT_FALSE(http_test_resolve_md_path("", out, sizeof(out)));
    ASSERT_FALSE(http_test_resolve_md_path(NULL, out, sizeof(out)));

    return true;
}

static bool test_read_text_file_limit(void) {
    const char *test_path = "test_temp_limit.md";
    FILE *f = fopen(test_path, "wb");
    ASSERT_NOT_NULL(f);
    fwrite("Hello World\n", 1, 12, f);
    fclose(f);

    bool too_large = false;
    char *content = http_test_read_text_file_limit(test_path, 100, &too_large);
    ASSERT_NOT_NULL(content);
    ASSERT_FALSE(too_large);
    ASSERT_STR_EQ(content, "Hello World\n");
    free(content);

    /* Test file exceeding limit */
    content = http_test_read_text_file_limit(test_path, 5, &too_large);
    ASSERT_NULL(content);
    ASSERT_TRUE(too_large);

    /* Test nonexistent file */
    content = http_test_read_text_file_limit("nonexistent_path_xyz.md", 100, &too_large);
    ASSERT_NULL(content);
    ASSERT_FALSE(too_large);

    (void)remove(test_path);
    return true;
}

static bool test_http_endpoint_router(void) {
    char *resp;

    /* Create temporary markdown file for endpoint testing */
    const char *temp_md = "test_http_temp.md";
    FILE *f = fopen(temp_md, "wb");
    if (f) {
        fwrite("# Temp Heading\n\nSome body text.\n", 1, 32, f);
        fclose(f);
    }
    http_test_set_initial_file(temp_md);

    /* 1. GET / */
    resp = http_exchange("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "HTTP/1.1 200 OK") != NULL);
    free(resp);

    /* 2. GET /static/styles.css */
    resp = http_exchange("GET /static/styles.css HTTP/1.1\r\nHost: localhost\r\n\r\n");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "HTTP/1.1 200 OK") != NULL);
    ASSERT_TRUE(strstr(resp, "text/css") != NULL);
    free(resp);

    /* 3. GET /static/client.js */
    resp = http_exchange("GET /static/client.js HTTP/1.1\r\nHost: localhost\r\n\r\n");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "HTTP/1.1 200 OK") != NULL);
    free(resp);

    /* 4. GET /static/nonexistent.xyz (404) */
    resp = http_exchange("GET /static/nonexistent.xyz HTTP/1.1\r\n\r\n");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "404 Not Found") != NULL);
    free(resp);

    /* 5. GET /static/../secret (403 Directory Traversal) */
    resp = http_exchange("GET /static/../secret HTTP/1.1\r\n\r\n");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "403 Forbidden") != NULL);
    free(resp);

    /* 6. GET /files */
    resp = http_exchange("GET /files HTTP/1.1\r\n\r\n");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "HTTP/1.1 200 OK") != NULL);
    ASSERT_TRUE(strstr(resp, "application/json") != NULL);
    free(resp);

    /* 7. GET /file (fallback to initial file) */
    resp = http_exchange("GET /file HTTP/1.1\r\n\r\n");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "HTTP/1.1 200 OK") != NULL);
    ASSERT_TRUE(strstr(resp, "test_http_temp.md") != NULL);
    free(resp);

    /* 8. GET /file?path=test_http_temp.md */
    resp = http_exchange("GET /file?path=test_http_temp.md HTTP/1.1\r\n\r\n");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "HTTP/1.1 200 OK") != NULL);
    ASSERT_TRUE(strstr(resp, "Temp Heading") != NULL);
    free(resp);

    /* 9. GET /file?path=../bad.md (403) */
    resp = http_exchange("GET /file?path=../bad.md HTTP/1.1\r\n\r\n");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "403 Forbidden") != NULL);
    free(resp);

    /* 10. GET /nonexistent (404) */
    resp = http_exchange("GET /nonexistent HTTP/1.1\r\n\r\n");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "404 Not Found") != NULL);
    free(resp);

    /* 11. POST /render (valid) */
    const char *post_render = "POST /render HTTP/1.1\r\nContent-Length: 18\r\n\r\n{\"md\":\"# Header\"}";
    resp = http_exchange(post_render);
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "HTTP/1.1 200 OK") != NULL);
    ASSERT_TRUE(strstr(resp, "<h1>Header</h1>") != NULL);
    free(resp);

    /* 12. POST /render (parser error snippet) */
    const char *post_err = "POST /render HTTP/1.1\r\nContent-Length: 29\r\n\r\n{\"md\":\"```c\\nunclosed fence\"}";
    resp = http_exchange(post_err);
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "400 Bad Request") != NULL);
    ASSERT_TRUE(strstr(resp, "unterminated code fence") != NULL);
    free(resp);

    /* 13. POST /render (missing md field) */
    resp = http_exchange("POST /render HTTP/1.1\r\nContent-Length: 13\r\n\r\n{\"wrong\":\"x\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "400 Bad Request") != NULL);
    free(resp);

    /* 14. POST /serialize (valid) */
    const char *post_ser = "POST /serialize HTTP/1.1\r\nContent-Length: 26\r\n\r\n{\"html\":\"<p>Paragraph</p>\"}";
    resp = http_exchange(post_ser);
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "HTTP/1.1 200 OK") != NULL);
    ASSERT_TRUE(strstr(resp, "Paragraph") != NULL);
    free(resp);

    /* 15. POST /serialize (missing html field) */
    resp = http_exchange("POST /serialize HTTP/1.1\r\nContent-Length: 13\r\n\r\n{\"wrong\":\"x\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "400 Bad Request") != NULL);
    free(resp);

    /* 16. POST /open-file (valid) */
    resp = http_exchange("POST /open-file HTTP/1.1\r\nContent-Length: 29\r\n\r\n{\"path\":\"test_http_temp.md\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "HTTP/1.1 200 OK") != NULL);
    free(resp);

    /* 17. POST /open-file (invalid path 403) */
    resp = http_exchange("POST /open-file HTTP/1.1\r\nContent-Length: 22\r\n\r\n{\"path\":\"../bad.md\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "403 Forbidden") != NULL);
    free(resp);

    /* 18. POST /upload-file (valid) */
    const char *up_req = "POST /upload-file HTTP/1.1\r\nContent-Length: 48\r\n\r\n{\"name\":\"up_test.md\",\"content\":\"# Upload Test\"}";
    resp = http_exchange(up_req);
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "HTTP/1.1 200 OK") != NULL);
    free(resp);
    (void)remove("up_test.md");

    /* 19. POST /upload-file (missing content 400) */
    resp = http_exchange("POST /upload-file HTTP/1.1\r\nContent-Length: 20\r\n\r\n{\"name\":\"up_test.md\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "400 Bad Request") != NULL);
    free(resp);

    /* 20. POST /upload-file (invalid extension 403) */
    resp = http_exchange("POST /upload-file HTTP/1.1\r\nContent-Length: 35\r\n\r\n{\"name\":\"bad.txt\",\"content\":\"hi\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "403 Forbidden") != NULL);
    free(resp);

    /* 21. POST /save (valid) */
    resp = http_exchange("POST /save HTTP/1.1\r\nContent-Length: 25\r\n\r\n{\"content\":\"Saved Text\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "HTTP/1.1 200 OK") != NULL);
    free(resp);

    /* 22. POST /save (missing content 400) */
    resp = http_exchange("POST /save HTTP/1.1\r\nContent-Length: 13\r\n\r\n{\"wrong\":\"x\"}");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "400 Bad Request") != NULL);
    free(resp);

    /* 23. POST /shutdown */
    resp = http_exchange("POST /shutdown HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "HTTP/1.1 200 OK") != NULL);
    free(resp);

    /* 24. Unsupported HTTP Method (PUT 400) */
    resp = http_exchange("PUT /render HTTP/1.1\r\n\r\n");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "400 Bad Request") != NULL);
    free(resp);

    /* 25. Malformed Request Line (400) */
    resp = http_exchange("GARBAGE_LINE\r\n\r\n");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "400 Bad Request") != NULL);
    free(resp);

    /* 26. Invalid Content-Length (400) */
    resp = http_exchange("POST /render HTTP/1.1\r\nContent-Length: invalid\r\n\r\n");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "400 Bad Request") != NULL);
    free(resp);

    /* 27. Payload Too Large (413) */
    resp = http_exchange("POST /render HTTP/1.1\r\nContent-Length: 99999999\r\n\r\n");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "413") != NULL);
    free(resp);

    /* 28. Missing Header Delimiters (400) */
    resp = http_exchange("GET / NO_END_DELIMITERS");
    ASSERT_NOT_NULL(resp);
    ASSERT_TRUE(strstr(resp, "400 Bad Request") != NULL);
    free(resp);

    (void)remove(temp_md);
    return true;
}

static bool test_large_json_round_trip(void) {
    const size_t payload_len = 70000;
    char *payload = malloc(payload_len + 1);
    char *json = NULL;
    char *escaped = NULL;
    char *decoded = NULL;
    size_t json_len;

    ASSERT_NOT_NULL(payload);
    for (size_t i = 0; i < payload_len; i++) {
        payload[i] = (i % 64 == 63) ? '\n' : (char)('a' + (i % 26));
    }
    payload[payload_len] = '\0';

    escaped = http_test_json_escape_alloc(payload);
    ASSERT_NOT_NULL(escaped);
    json_len = strlen(escaped) + 16;
    json = malloc(json_len);
    ASSERT_NOT_NULL(json);
    (void)snprintf(json, json_len, "{\"content\":\"%s\"}", escaped);

    decoded = http_test_extract_json_string_alloc(json, "content");
    ASSERT_NOT_NULL(decoded);
    ASSERT_INT_EQ((int)strlen(decoded), (int)payload_len);
    ASSERT_STR_EQ(decoded, payload);

    free(decoded);
    free(json);
    free(escaped);
    free(payload);
    return true;
}

static bool test_large_file_read_is_not_truncated(void) {
    const char *path = "test_large_http.md";
    const size_t payload_len = 70000;
    FILE *f = fopen(path, "wb");
    char *read_back;
    bool too_large = false;

    ASSERT_NOT_NULL(f);
    for (size_t i = 0; i < payload_len; i++) {
        fputc((int)('0' + (i % 10)), f);
    }
    fclose(f);

    read_back = http_test_read_text_file_limit(path, 8u * 1024u * 1024u, &too_large);
    ASSERT_FALSE(too_large);
    ASSERT_NOT_NULL(read_back);
    ASSERT_INT_EQ((int)strlen(read_back), (int)payload_len);
    ASSERT_TRUE(read_back[0] == '0');
    char expected_last = (char)('0' + ((payload_len - 1) - ((payload_len - 1) / 10) * 10));
    ASSERT_TRUE(read_back[payload_len - 1] == expected_last);

    free(read_back);
    (void)remove(path);
    return true;
}

static bool test_resolve_md_path_windows_drive(void) {
    char out[256];
    ASSERT_FALSE(http_test_resolve_md_path("C:\\evil\\path.md", out, sizeof(out)));
    ASSERT_FALSE(http_test_resolve_md_path("d:/evil/path.md", out, sizeof(out)));
    ASSERT_FALSE(http_test_resolve_md_path("Z:\\another.markdown", out, sizeof(out)));

    ASSERT_TRUE(http_test_resolve_md_path("notes.md", out, sizeof(out)));
    ASSERT_STR_EQ(out, "notes.md");
    ASSERT_TRUE(http_test_resolve_md_path("subdir/file.markdown", out, sizeof(out)));
    ASSERT_STR_EQ(out, "subdir/file.markdown");
    return true;
}

int main(void) {
    RUN_TEST(test_extract_json_string);
    RUN_TEST(test_extract_json_string_alloc);
    RUN_TEST(test_json_escape);
    RUN_TEST(test_url_decode);
    RUN_TEST(test_get_query_param);
    RUN_TEST(test_resolve_md_path);
    RUN_TEST(test_read_text_file_limit);
    RUN_TEST(test_http_endpoint_router);
    RUN_TEST(test_large_json_round_trip);
    RUN_TEST(test_large_file_read_is_not_truncated);
    RUN_TEST(test_resolve_md_path_windows_drive);
    printf("\nTest Summary: %d run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
