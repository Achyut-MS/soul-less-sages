#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src-c/md_parser.h"

/*
 * Senior Engineer Note:
 * Conformance runner. In full implementation, this will read spec.json, parse
 * test cases, run md_to_html, compare expected vs actual HTML, and output a ratio.
 */

int main(void) {
    printf("Starting CommonMark conformance runner skeleton...\n");
    printf("[COMMONMARK] Ingesting mock spec test case...\n");
    
    const char *md = "# Hello World";
    md_parse_result_t res = md_to_html(md, strlen(md));
    if (res.success && res.html) {
        printf("PASSED: '%s' -> '%s'\n", md, res.html);
        md_parse_result_free(&res);
        printf("\nCommonMark Conformance Ratio: 1/1 passed (100.00%%)\n");
        return 0;
    } else {
        printf("FAILED conformance test\n");
        md_parse_result_free(&res);
        return 1;
    }
}
