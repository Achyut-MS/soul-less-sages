#include "test_harness.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *http_test_extract_json_string_alloc(const char *json, const char *key);
char *http_test_json_escape_alloc(const char *src);
char *http_test_read_text_file_limit(const char *path, size_t limit, bool *too_large);

TEST_INIT()

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

int main(void) {
    RUN_TEST(test_large_json_round_trip);
    RUN_TEST(test_large_file_read_is_not_truncated);
    printf("\nTest Summary: %d run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
