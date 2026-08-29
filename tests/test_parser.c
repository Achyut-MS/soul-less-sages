#include "test_harness.h"
#include "../src-c/md_parser.h"

TEST_INIT()

/*
 * Senior Engineer Note:
 * Validates the parser interface by passing a sample markdown header to
 * md_to_html and asserting that a non-null HTML string is produced.
 */

bool test_stub_parser(void) {
    const char *md = "# Hello";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    md_parse_result_free(&res);
    return true;
}

int main(void) {
    RUN_TEST(test_stub_parser);
    printf("\nTest Summary: %d run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
