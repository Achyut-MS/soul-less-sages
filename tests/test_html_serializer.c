#include "test_harness.h"
#include "../src-c/html_serializer.h"
#include <string.h>

TEST_INIT()

/* -------------------------------------------------------------------------
 * Basic Tag Tests
 * ------------------------------------------------------------------------- */

bool test_heading_levels(void) {
    const char *html = "<h1>Heading 1</h1><h2>Heading 2</h2><h3>Heading 3</h3><h4>Heading 4</h4><h5>Heading 5</h5><h6>Heading 6</h6>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "# Heading 1\n\n## Heading 2\n\n### Heading 3\n\n#### Heading 4\n\n##### Heading 5\n\n###### Heading 6\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_bold_and_italic(void) {
    const char *html = "<p><strong>bold</strong> and <em>italic</em></p>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "**bold** and *italic*\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_b_and_i_tags(void) {
    const char *html = "<p><b>bold</b> and <i>italic</i></p>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "**bold** and *italic*\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_combined_bold_italic(void) {
    const char *html = "<p><strong><em>bold italic</em></strong></p>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "***bold italic***\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_inline_code(void) {
    const char *html = "<p>Call <code>printf(\"Hello\\n\");</code> to print.</p>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "Call `printf(\"Hello\\n\");` to print.\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_code_block_with_lang(void) {
    const char *html = "<pre><code class=\"language-c\">int main(void) {\n    return 0;\n}</code></pre>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "```c\nint main(void) {\n    return 0;\n}\n```\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_code_block_no_lang(void) {
    const char *html = "<pre><code>plain code block</code></pre>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "```\nplain code block\n```\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_unordered_list(void) {
    const char *html = "<ul><li>one</li><li>two</li><li>three</li></ul>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "- one\n- two\n- three\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_ordered_list_renumbering(void) {
    const char *html = "<ol><li>first</li><li>second</li><li>third</li></ol>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "1. first\n2. second\n3. third\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_blockquote(void) {
    const char *html = "<blockquote>quote text</blockquote>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "> quote text\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_blockquote_multiline(void) {
    const char *html = "<blockquote>line one\nline two</blockquote>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "> line one\n> line two\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_blockquote_empty(void) {
    const char *html = "<blockquote><p></p></blockquote><p>s</p>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, ">\n\ns\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_link(void) {
    const char *html = "<a href=\"https://example.com\">link text</a>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "[link text](https://example.com)");
    html_serialize_result_free(&res);
    return true;
}

bool test_image(void) {
    const char *html = "<img src=\"logo.png\" alt=\"Logo\">";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "![Logo](logo.png)");
    html_serialize_result_free(&res);
    return true;
}

bool test_horizontal_rule(void) {
    const char *html = "<hr>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "---\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_line_break(void) {
    const char *html = "<p>First line<br>Second line</p>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "First line  \nSecond line\n\n");
    html_serialize_result_free(&res);
    return true;
}

/* -------------------------------------------------------------------------
 * Nested Structure Tests
 * ------------------------------------------------------------------------- */

bool test_nested_unordered_lists(void) {
    const char *html = "<ul><li>parent<ul><li>child 1</li><li>child 2</li></ul></li><li>parent 2</li></ul>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "- parent\n  - child 1\n  - child 2\n- parent 2\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_nested_ordered_lists(void) {
    const char *html = "<ol><li>step 1<ol><li>substep a</li><li>substep b</li></ol></li><li>step 2</li></ol>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "1. step 1\n   1. substep a\n   2. substep b\n2. step 2\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_nested_blockquote(void) {
    const char *html = "<blockquote><blockquote>deep quote</blockquote></blockquote>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "> > deep quote\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_bold_in_link(void) {
    const char *html = "<p><a href=\"http://example.com\"><strong>bold link</strong></a></p>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "[**bold link**](http://example.com)\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_list_in_blockquote(void) {
    const char *html = "<blockquote><ul><li>item 1</li><li>item 2</li></ul></blockquote>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "> - item 1\n> - item 2\n\n");
    html_serialize_result_free(&res);
    return true;
}

/* -------------------------------------------------------------------------
 * Edge Cases & Error Handling Tests
 * ------------------------------------------------------------------------- */

bool test_empty_html(void) {
    const char *html = "";
    html_serialize_result_t res = html_to_md(html, 0);
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "");
    html_serialize_result_free(&res);
    return true;
}

bool test_null_input(void) {
    html_serialize_result_t res = html_to_md(NULL, 0);
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "");
    html_serialize_result_free(&res);
    return true;
}

bool test_html_entities(void) {
    const char *html = "<p>&lt;tag&gt; &amp; &quot;quotes&quot; &#39;apostrophe&#39; &copy;</p>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "\\<tag> & \"quotes\" 'apostrophe' &copy;\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_unicode_preservation(void) {
    const char *html = "<p>Hello 世界 🌍 café résumé</p>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "Hello 世界 🌍 café résumé\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_malformed_unclosed_tags(void) {
    const char *html = "<p>Unclosed <b>bold <i>italic text";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "Unclosed **bold *italic text***\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_comment_skipping(void) {
    const char *html = "<p>Before <!-- comment here -->After</p>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "Before After\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_metacharacter_escaping(void) {
    const char *html = "<p>2 * 3 = 6, variable_name, array[0]</p>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "2 \\* 3 = 6, variable\\_name, array\\[0\\]\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_table(void) {
    const char *html = "<table><thead><tr><th>Header 1</th><th>Header 2</th></tr></thead><tbody><tr><td>Cell 1</td><td>Cell 2</td></tr></tbody></table>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_TRUE(strstr(res.markdown, "| Header 1 | Header 2 |") != NULL);
    ASSERT_TRUE(strstr(res.markdown, "|---|---|") != NULL);
    ASSERT_TRUE(strstr(res.markdown, "| Cell 1 | Cell 2 |") != NULL);
    html_serialize_result_free(&res);
    return true;
}

/* -------------------------------------------------------------------------
 * Main Test Runner
 * ------------------------------------------------------------------------- */
bool test_strikethrough_del(void) {
    const char *html = "<p>Here is <del>deleted text</del> and <s>struck text</s></p>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_STR_EQ(res.markdown, "Here is ~~deleted text~~ and ~~struck text~~\n\n");
    html_serialize_result_free(&res);
    return true;
}

bool test_mermaid_div(void) {
    const char *html = "<div class=\"mermaid\">graph TD;\n    A-->B;</div>";
    html_serialize_result_t res = html_to_md(html, strlen(html));
    ASSERT_TRUE(res.success);
    ASSERT_NOT_NULL(res.markdown);
    ASSERT_TRUE(strstr(res.markdown, "```mermaid\ngraph TD;\n    A-->B;\n```") != NULL);
    html_serialize_result_free(&res);
    return true;
}

int main(void) {
    RUN_TEST(test_heading_levels);
    RUN_TEST(test_bold_and_italic);
    RUN_TEST(test_b_and_i_tags);
    RUN_TEST(test_combined_bold_italic);
    RUN_TEST(test_strikethrough_del);
    RUN_TEST(test_mermaid_div);
    RUN_TEST(test_inline_code);
    RUN_TEST(test_code_block_with_lang);
    RUN_TEST(test_code_block_no_lang);
    RUN_TEST(test_unordered_list);
    RUN_TEST(test_ordered_list_renumbering);
    RUN_TEST(test_blockquote);
    RUN_TEST(test_blockquote_multiline);
    RUN_TEST(test_blockquote_empty);
    RUN_TEST(test_link);
    RUN_TEST(test_image);
    RUN_TEST(test_horizontal_rule);
    RUN_TEST(test_line_break);
    RUN_TEST(test_nested_unordered_lists);
    RUN_TEST(test_nested_ordered_lists);
    RUN_TEST(test_nested_blockquote);
    RUN_TEST(test_bold_in_link);
    RUN_TEST(test_list_in_blockquote);
    RUN_TEST(test_empty_html);
    RUN_TEST(test_null_input);
    RUN_TEST(test_html_entities);
    RUN_TEST(test_unicode_preservation);
    RUN_TEST(test_malformed_unclosed_tags);
    RUN_TEST(test_comment_skipping);
    RUN_TEST(test_metacharacter_escaping);
    RUN_TEST(test_table);

    printf("\nTest Summary: %d run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}


