#include "test_harness.h"
#include "../src-c/html_serializer.h"

TEST_INIT()

/*
 * Senior Engineer Note:
 * Validates the serializer interface by passing a sample HTML paragraph to
 * html_to_md and asserting that a non-null Markdown string is produced.
 */

bool test_stub_serializer(void) {
    const char *html = "<p>Hello</p>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    html_serialize_result_free(&res);
    return true;
}

int main(void) {
    RUN_TEST(test_stub_serializer);
    printf("\nTest Summary: %d run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
