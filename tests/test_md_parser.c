#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src-c/md_parser.h"

int main(void) {
    const char *md = "# Hello\n\nThis is **bold** and *italic*.\n\n- item one\n- item two\n";
    md_parse_result_t res = md_to_html(md, strlen(md));
    assert(res.success);
    assert(strstr(res.html, "<h1>") != NULL);
    assert(strstr(res.html, "<strong>") != NULL);
    assert(strstr(res.html, "<em>") != NULL);
    assert(strstr(res.html, "<ul>") != NULL);

    const char *bad = "**oops";
    md_parse_result_t bad_res = md_to_html(bad, strlen(bad));
    assert(!bad_res.success);
    assert(bad_res.error_msg != NULL);
    assert(strstr(bad_res.error_msg, "**") != NULL);

    md_parse_result_free(&res);
    md_parse_result_free(&bad_res);
    puts("member 1 parser tests passed");
    return 0;
}
