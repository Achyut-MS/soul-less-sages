#include "test_harness.h"
#include "../src-c/md_parser.h"
#include <string.h>

TEST_INIT()

bool test_parser_headings(void) {
    const char *md = "# Header 1\n## Header 2\n### Header 3";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<h1>Header 1</h1>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<h2>Header 2</h2>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<h3>Header 3</h3>") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_bold_and_italic(void) {
    const char *md = "**bold text** and *italic text*";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<strong>bold text</strong>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<em>italic text</em>") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_inline_code(void) {
    const char *md = "Code `int x = 42;` example";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<code>int x = 42;</code>") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_code_block(void) {
    const char *md = "```c\nint main() { return 0; }\n```";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<pre><code") != NULL);
    ASSERT_TRUE(strstr(res.html, "class=\"language-c\"") != NULL);
    ASSERT_TRUE(strstr(res.html, "int main() { return 0; }") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_unordered_list(void) {
    const char *md = "- Item 1\n- Item 2\n- Item 3";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<ul>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<li>Item 1</li>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<li>Item 2</li>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<li>Item 3</li>") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_ordered_list(void) {
    const char *md = "1. First step\n2. Second step";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<ol>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<li>First step</li>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<li>Second step</li>") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_blockquote(void) {
    const char *md = "> This is a quote\n> Second line of quote";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<blockquote>") != NULL);
    ASSERT_TRUE(strstr(res.html, "This is a quote") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_blockquote_empty(void) {
    const char *md = ">\n\ns\n\n";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<blockquote>\n<p></p>\n</blockquote>\n") != NULL);
    ASSERT_TRUE(strstr(res.html, "<p>s</p>\n") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_link(void) {
    const char *md = "Check [Google](https://google.com) now.";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<a href=\"https://google.com\">Google</a>") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_image(void) {
    const char *md = "Look at ![Logo](logo.png) here.";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<img src=\"logo.png\" alt=\"Logo\" />") != NULL ||
                strstr(res.html, "<img src=\"logo.png\" alt=\"Logo\">") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_horizontal_rule(void) {
    const char *md = "Before\n\n---\n\nAfter";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<hr />") != NULL || strstr(res.html, "<hr>") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_empty_input(void) {
    const char *md = "";
    md_parse_result_t res = md_to_html(md, 0);
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_table(void) {
    const char *md = "| Name | Age |\n|---|---|\n| Alice | 30 |\n| Bob | 25 |\n";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<table>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<th>Name</th>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<th>Age</th>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<td>Alice</td>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<td>30</td>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<td>Bob</td>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<td>25</td>") != NULL);
    ASSERT_TRUE(strstr(res.html, "</table>") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_escapes_and_special_chars(void) {
    const char *md = "A & B < C > D 'E' \"F\" \\*escaped\\* \\_not\\_";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "&amp;") != NULL);
    ASSERT_TRUE(strstr(res.html, "&lt;") != NULL);
    ASSERT_TRUE(strstr(res.html, "&gt;") != NULL);
    ASSERT_TRUE(strstr(res.html, "&quot;") != NULL);
    ASSERT_TRUE(strstr(res.html, "&#39;") != NULL);
    ASSERT_TRUE(strstr(res.html, "*escaped*") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_triple_emphasis(void) {
    const char *md = "***bold and italic***";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<strong><em>bold and italic</em></strong>") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_all_heading_levels(void) {
    const char *md = "# H1\n\n## H2\n\n### H3\n\n#### H4\n\n##### H5\n\n###### H6\n";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<h1>H1</h1>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<h2>H2</h2>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<h3>H3</h3>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<h4>H4</h4>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<h5>H5</h5>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<h6>H6</h6>") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_multiline_blockquote(void) {
    const char *md = "> Line 1\n> Line 2\n> Line 3\n";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<blockquote>") != NULL);
    ASSERT_TRUE(strstr(res.html, "Line 1") != NULL);
    ASSERT_TRUE(strstr(res.html, "Line 2") != NULL);
    ASSERT_TRUE(strstr(res.html, "Line 3") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_nested_inlines(void) {
    const char *md = "This has [**bold link**](https://example.com) and *`code in italic`*.";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<a href=\"https://example.com\"><strong>bold link</strong></a>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<em><code>code in italic</code></em>") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_code_block_with_lang(void) {
    const char *md = "```python\nprint('hello')\n```\n";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<pre><code class=\"language-python\">print(&#39;hello&#39;)\n</code></pre>") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_whitespace_lines(void) {
    const char *md = "   \n\t\n\nParagraph 1\n\n   \n\nParagraph 2\n\n";
    md_parse_result_t res = md_to_html(md, strlen(md));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.html);
    ASSERT_TRUE(strstr(res.html, "<p>Paragraph 1</p>") != NULL);
    ASSERT_TRUE(strstr(res.html, "<p>Paragraph 2</p>") != NULL);
    md_parse_result_free(&res);
    return true;
}

bool test_parser_errors(void) {
    const char *bad = "```c\nint main() { return 0; }\n";
    md_parse_result_t bad_res = md_to_html(bad, strlen(bad));
    ASSERT_TRUE(!bad_res.success);
    ASSERT_NOT_NULL(bad_res.error_msg);
    ASSERT_TRUE(strstr(bad_res.error_msg, "unterminated code fence") != NULL);
    md_parse_result_free(&bad_res);
    return true;
}

int main(void) {
    RUN_TEST(test_parser_headings);
    RUN_TEST(test_parser_all_heading_levels);
    RUN_TEST(test_parser_bold_and_italic);
    RUN_TEST(test_parser_triple_emphasis);
    RUN_TEST(test_parser_inline_code);
    RUN_TEST(test_parser_code_block);
    RUN_TEST(test_parser_code_block_with_lang);
    RUN_TEST(test_parser_unordered_list);
    RUN_TEST(test_parser_ordered_list);
    RUN_TEST(test_parser_blockquote);
    RUN_TEST(test_parser_blockquote_empty);
    RUN_TEST(test_parser_multiline_blockquote);
    RUN_TEST(test_parser_link);
    RUN_TEST(test_parser_image);
    RUN_TEST(test_parser_horizontal_rule);
    RUN_TEST(test_parser_table);
    RUN_TEST(test_parser_empty_input);
    RUN_TEST(test_parser_whitespace_lines);
    RUN_TEST(test_parser_escapes_and_special_chars);
    RUN_TEST(test_parser_nested_inlines);
    RUN_TEST(test_parser_errors);
    printf("\nTest Summary: %d run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
